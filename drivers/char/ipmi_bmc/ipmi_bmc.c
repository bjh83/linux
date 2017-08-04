/*
 * Copyright 2017 Google Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/errno.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/ipmi_bmc.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>

#define PFX "IPMI BMC core: "

struct ipmi_bmc_ctx *ipmi_bmc_get_global_ctx()
{
	static struct ipmi_bmc_ctx global_ctx;

	return &global_ctx;
}

int ipmi_bmc_send_response(struct ipmi_bmc_ctx *ctx,
			   struct bt_msg *bt_response)
{
	struct ipmi_bmc_bus *bus;
	int ret = -ENODEV;

	rcu_read_lock();
	bus = rcu_dereference(ctx->bus);

	if (bus)
		ret = bus->send_response(bus, bt_response);

	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL(ipmi_bmc_send_response);

bool ipmi_bmc_is_response_open(struct ipmi_bmc_ctx *ctx)
{
	struct ipmi_bmc_bus *bus;
	bool ret = false;

	rcu_read_lock();
	bus = rcu_dereference(ctx->bus);

	if (bus)
		ret = bus->is_response_open(bus);

	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL(ipmi_bmc_is_response_open);

int ipmi_bmc_register_device(struct ipmi_bmc_ctx *ctx,
			     struct ipmi_bmc_device *device_in)
{
	struct ipmi_bmc_device *device;

	mutex_lock(&ctx->drivers_mutex);
	/* Make sure it hasn't already been registered. */
	list_for_each_entry(device, &ctx->devices, link) {
		if (device == device_in) {
			mutex_unlock(&ctx->drivers_mutex);
			return -EINVAL;
		}
	}

	list_add_rcu(&device_in->link, &ctx->devices);
	mutex_unlock(&ctx->drivers_mutex);

	return 0;
}
EXPORT_SYMBOL(ipmi_bmc_register_device);

int ipmi_bmc_unregister_device(struct ipmi_bmc_ctx *ctx,
			       struct ipmi_bmc_device *device_in)
{
	struct ipmi_bmc_device *device;
	bool found = false;

	mutex_lock(&ctx->drivers_mutex);
	/* Make sure it is currently registered. */
	list_for_each_entry(device, &ctx->devices, link) {
		if (device == device_in) {
			found = true;
			break;
		}
	}
	if (!found) {
		mutex_unlock(&ctx->drivers_mutex);
		return -ENXIO;
	}

	list_del_rcu(&device_in->link);
	mutex_unlock(&ctx->drivers_mutex);
	synchronize_rcu();

	return 0;
}
EXPORT_SYMBOL(ipmi_bmc_unregister_device);

int ipmi_bmc_register_default_device(struct ipmi_bmc_ctx *ctx,
				     struct ipmi_bmc_device *device)
{
	int ret;

	mutex_lock(&ctx->drivers_mutex);
	if (!ctx->default_device) {
		ctx->default_device = device;
		ret = 0;
	} else {
		ret = -EBUSY;
	}
	mutex_unlock(&ctx->drivers_mutex);

	return ret;
}
EXPORT_SYMBOL(ipmi_bmc_register_default_device);

int ipmi_bmc_unregister_default_device(struct ipmi_bmc_ctx *ctx,
				       struct ipmi_bmc_device *device)
{
	int ret;

	mutex_lock(&ctx->drivers_mutex);
	if (ctx->default_device == device) {
		ctx->default_device = NULL;
		ret = 0;
	} else {
		ret = -ENXIO;
	}
	mutex_unlock(&ctx->drivers_mutex);
	synchronize_rcu();

	return ret;
}
EXPORT_SYMBOL(ipmi_bmc_unregister_default_device);

static u8 errno_to_ccode(int errno)
{
	switch (errno) {
	case EBUSY:
		return 0xC0; /* Node Busy */
	case EINVAL:
		return 0xC1; /* Invalid Command */
	case ETIMEDOUT:
		return 0xC3; /* Timeout while processing command */
	case ENOMEM:
		return 0xC4; /* Out of space */
	default:
		return 0xFF; /* Unspecified error */
	}
}

static void ipmi_bmc_send_error_response(struct ipmi_bmc_ctx *ctx,
					 struct bt_msg *bt_request,
					 u8 ccode)
{
	struct bt_msg error_response;
	int ret;

	/* Payload contains 1 byte for completion code */
	error_response.len = bt_msg_payload_to_len(1);
	error_response.netfn_lun = bt_request->netfn_lun |
				   IPMI_NETFN_LUN_RESPONSE_MASK;
	error_response.seq = bt_request->seq;
	error_response.cmd = bt_request->cmd;
	error_response.payload[0] = ccode;

	/*
	 * TODO(benjaminfair): Retry sending the response if it fails. The error
	 * response might fail to send if another response is in progress. In
	 * that case, the request will timeout rather than getting a more
	 * specific completion code. This should buffer up responses and send
	 * them when it can. Device drivers will generally handle error
	 * reporting themselves; this code is only a fallback when that's not
	 * available or when the drivers themselves fail.
	 */
	ret = ipmi_bmc_send_response(ctx, &error_response);
	if (ret)
		pr_warn(PFX "Failed to reply with completion code %u: ipmi_bmc_send_response returned %d\n",
			(u32) ccode, ret);
}

void ipmi_bmc_handle_request(struct ipmi_bmc_ctx *ctx,
			     struct bt_msg *bt_request)
{
	struct ipmi_bmc_device *default_device;
	struct ipmi_bmc_device *device;
	int ret = -EINVAL;

	rcu_read_lock();
	list_for_each_entry_rcu(device, &ctx->devices, link) {
		if (device->match_request(device, bt_request)) {
			ret = device->handle_request(device, bt_request);
			goto out;
		}
	}

	/* No specific handler found. Use default handler instead */
	default_device = rcu_dereference(ctx->default_device);
	if (default_device)
		ret = default_device->handle_request(default_device,
						     bt_request);

out:
	rcu_read_unlock();
	if (ret)
		ipmi_bmc_send_error_response(ctx, bt_request,
					     errno_to_ccode(-ret));
}
EXPORT_SYMBOL(ipmi_bmc_handle_request);

void ipmi_bmc_signal_response_open(struct ipmi_bmc_ctx *ctx)
{
	struct ipmi_bmc_device *default_device;
	struct ipmi_bmc_device *device;

	rcu_read_lock();
	list_for_each_entry_rcu(device, &ctx->devices, link) {
		device->signal_response_open(device);
	}

	default_device = rcu_dereference(ctx->default_device);
	if (default_device)
		default_device->signal_response_open(default_device);

	rcu_read_unlock();
}
EXPORT_SYMBOL(ipmi_bmc_signal_response_open);

int ipmi_bmc_register_bus(struct ipmi_bmc_ctx *ctx,
			  struct ipmi_bmc_bus *bus_in)
{
	int ret;

	mutex_lock(&ctx->drivers_mutex);
	if (!ctx->bus) {
		ctx->bus = bus_in;
		ret = 0;
	} else {
		ret = -EBUSY;
	}
	mutex_unlock(&ctx->drivers_mutex);

	return ret;
}
EXPORT_SYMBOL(ipmi_bmc_register_bus);

int ipmi_bmc_unregister_bus(struct ipmi_bmc_ctx *ctx,
			    struct ipmi_bmc_bus *bus_in)
{
	int ret;

	mutex_lock(&ctx->drivers_mutex);
	/* Tried to unregister when another bus is registered */
	if (ctx->bus == bus_in) {
		ctx->bus = NULL;
		ret = 0;
	} else {
		ret = -ENXIO;
	}
	mutex_unlock(&ctx->drivers_mutex);
	synchronize_rcu();

	return ret;
}
EXPORT_SYMBOL(ipmi_bmc_unregister_bus);

static int __init ipmi_bmc_init(void)
{
	struct ipmi_bmc_ctx *ctx = ipmi_bmc_get_global_ctx();

	mutex_init(&ctx->drivers_mutex);
	INIT_LIST_HEAD(&ctx->devices);
	return 0;
}
module_init(ipmi_bmc_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Benjamin Fair <benjaminfair@google.com>");
MODULE_DESCRIPTION("Core IPMI driver for the BMC side");
