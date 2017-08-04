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

#ifndef __LINUX_IPMI_BMC_H
#define __LINUX_IPMI_BMC_H

#include <linux/bug.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/types.h>

#define BT_MSG_PAYLOAD_LEN_MAX 252
#define BT_MSG_SEQ_MAX 255

/*
 * The bit in this mask is set in the netfn_lun field of an IPMI message to
 * indicate that it is a response.
 */
#define IPMI_NETFN_LUN_RESPONSE_MASK (1 << 2)

/**
 * struct bt_msg - Block Transfer IPMI message.
 * @len: Length of the message, not including this field.
 * @netfn_lun: 6-bit netfn field definining the category of message and 2-bit
 *             lun field used for routing.
 * @seq: Sequence number used to associate requests with responses.
 * @cmd: Command within a netfn category.
 * @payload: Variable length field. May have specific requirements based on
 *           netfn/cmd pair.
 *
 * Use bt_msg_len() to determine the total length of a message (including
 * the @len field) rather than reading it directly.
 */
struct bt_msg {
	u8 len;
	u8 netfn_lun;
	u8 seq;
	u8 cmd;
	u8 payload[BT_MSG_PAYLOAD_LEN_MAX];
} __packed;

/**
 * struct ipmi_bmc_device - Device driver that wants to receive ipmi requests.
 * @link: Used to make a linked list of devices.
 * @match_request: Used to determine whether a request can be handled by this
 *                 device. Note that the matchers are checked in an arbitrary
 *                 order.
 * @handle_request: Called when a request is received for this device.
 * @signal_response_open: Called when a response is done being sent and another
 *                        device can send a message. Make sure to check that the
 *                        bus isn't busy even after this is called, because all
 *                        devices receive this call and another one may have
 *                        already submitted a new response.
 *
 * This collection of callback functions represents an upper-level handler of
 * IPMI requests.
 *
 * Note that the callbacks may be called from an interrupt context.
 */
struct ipmi_bmc_device {
	struct list_head link;

	bool (*match_request)(struct ipmi_bmc_device *device,
			      struct bt_msg *bt_request);
	int (*handle_request)(struct ipmi_bmc_device *device,
			      struct bt_msg *bt_request);
	void (*signal_response_open)(struct ipmi_bmc_device *device);
};

/**
 * struct ipmi_bmc_bus - Bus driver that exchanges messages with the host.
 * @send_response: Submits the given response to be sent to the host. May return
 *                 -EBUSY if a response is already in progress, in which case
 *                 the caller should wait for the response_open() callback to be
 *                 called.
 * @is_response_open: Determines whether a response can currently be sent. Note
 *                    that &ipmi_bmc_bus->send_response() may still fail with
 *                    -EBUSY even after this returns true.
 *
 * This collection of callback functions represents a lower-level driver which
 * acts as a connection to the host.
 */
struct ipmi_bmc_bus {
	int (*send_response)(struct ipmi_bmc_bus *bus,
			     struct bt_msg *bt_response);
	bool (*is_response_open)(struct ipmi_bmc_bus *bus);
};

/**
 * struct ipmi_bmc_ctx - Context object used to interact with the IPMI BMC
 *                       core driver.
 * @bus: Pointer to the bus which is currently registered, or NULL for none.
 * @default_device: Pointer to a device which will receive messages that match
 *                  no other devices, or NULL if none is registered.
 * @devices: List of devices which are currently registered, besides the default
 *           device.
 * @drivers_mutex: Mutex which protects against concurrent editing of the
 *                 bus driver, default device driver, and devices list.
 *
 * This struct should only be modified by the IPMI BMC core code and not by bus
 * or device drivers.
 */
struct ipmi_bmc_ctx {
	struct ipmi_bmc_bus __rcu	*bus;
	struct ipmi_bmc_device __rcu	*default_device;
	struct list_head		devices;
	struct mutex			drivers_mutex;
};

/**
 * ipmi_bmc_get_global_ctx() - Get a pointer to the global ctx.
 *
 * This function gets a reference to the global ctx object which is used by
 * bus and device drivers to interact with the IPMI BMC core driver.
 *
 * Return: Pointer to the global ctx object.
 */
struct ipmi_bmc_ctx *ipmi_bmc_get_global_ctx(void);

/**
 * bt_msg_len() - Determine the total length of a Block Transfer message.
 * @bt_msg: Pointer to the message.
 *
 * This function calculates the length of an IPMI Block Transfer message
 * including the length field itself.
 *
 * Return: Length of @bt_msg.
 */
static inline u32 bt_msg_len(struct bt_msg *bt_msg)
{
	return bt_msg->len + 1;
}

/**
 * bt_msg_payload_to_len() - Calculate the len field of a Block Transfer message
 *                           given the length of the payload.
 * @payload_len: Length of the payload.
 *
 * Return: len field of the Block Transfer message which contains this payload.
 */
static inline u8 bt_msg_payload_to_len(u8 payload_len)
{
	if (unlikely(payload_len > BT_MSG_PAYLOAD_LEN_MAX)) {
		payload_len = BT_MSG_PAYLOAD_LEN_MAX;
		WARN(1, "BT message payload is too large. Truncating to %u.\n",
		     BT_MSG_PAYLOAD_LEN_MAX);
	}
	return payload_len + 3;
}

/**
 * ipmi_bmc_send_response() - Send an IPMI response on the current bus.
 * @bt_response: The response message to send.
 *
 * Return: 0 on success, negative errno otherwise.
 */
int ipmi_bmc_send_response(struct ipmi_bmc_ctx *ctx,
			   struct bt_msg *bt_response);
/**
 * ipmi_bmc_is_response_open() - Check whether we can currently send a new
 *                               response.
 *
 * Note that even if this function returns true, ipmi_bmc_send_response() may
 * still fail with -EBUSY if a response is submitted in the time between the two
 * calls.
 *
 * Return: true if we can send a new response, false if one is already in
 *         progress.
 */
bool ipmi_bmc_is_response_open(struct ipmi_bmc_ctx *ctx);
/**
 * ipmi_bmc_register_device() - Register a new device driver.
 * @device: Pointer to the struct which represents this device,
 *
 * Return: 0 on success, negative errno otherwise.
 */
int ipmi_bmc_register_device(struct ipmi_bmc_ctx *ctx,
			     struct ipmi_bmc_device *device);
/**
 * ipmi_bmc_unregister_device() - Unregister an existing device driver.
 * @device: Pointer to the struct which represents the existing device.
 *
 * Return: 0 on success, negative errno otherwise.
 */
int ipmi_bmc_unregister_device(struct ipmi_bmc_ctx *ctx,
			       struct ipmi_bmc_device *device);
/**
 * ipmi_bmc_register_default_device() - Register a new default device driver.
 * @device: Pointer to the struct which represents this device,
 *
 * Make this device the default device. If none of the other devices match on a
 * particular message, this device will receive it instead.  Note that only one
 * default device may be registered at a time.
 *
 * This functionalisty is currently used to allow messages which aren't directly
 * handled by the kernel to be sent to userspace and handled there.
 *
 * Return: 0 on success, negative errno otherwise.
 */
int ipmi_bmc_register_default_device(struct ipmi_bmc_ctx *ctx,
				     struct ipmi_bmc_device *device);
/**
 * ipmi_bmc_unregister_default_device() - Unregister the existing default device
 *                                        driver.
 * @device: Pointer to the struct which represents the existing device.
 *
 * Return: 0 on success, negative errno otherwise.
 */
int ipmi_bmc_unregister_default_device(struct ipmi_bmc_ctx *ctx,
				       struct ipmi_bmc_device *device);
/**
 * ipmi_bmc_handle_request() - Handle a new request that was received.
 * @bt_request: The request that was received.
 *
 * This is called by the bus driver when it receives a new request message.
 *
 * Note that it may be called from an interrupt context.
 */
void ipmi_bmc_handle_request(struct ipmi_bmc_ctx *ctx,
			    struct bt_msg *bt_request);
/**
 * ipmi_bmc_signal_response_open() - Notify the upper layer that a response can
 *                                   be sent.
 *
 * This is called by the bus driver after it finishes sending a response and is
 * ready to begin sending another one.
 */
void ipmi_bmc_signal_response_open(struct ipmi_bmc_ctx *ctx);
/**
 * ipmi_bmc_register_bus() - Register a new bus driver.
 * @bus: Pointer to the struct which represents this bus.
 *
 * Register a bus driver to handle communication with the host.
 *
 * Only one bus driver can be registered at any time.
 *
 * Return: 0 on success, negative errno otherwise.
 */
int ipmi_bmc_register_bus(struct ipmi_bmc_ctx *ctx,
			  struct ipmi_bmc_bus *bus);
/**
 * ipmi_bmc_unregister_bus() - Unregister an existing bus driver.
 * @bus: Pointer to the struct which represents the existing bus.
 *
 * Return: 0 on success, negative errno otherwise.
 */
int ipmi_bmc_unregister_bus(struct ipmi_bmc_ctx *ctx,
			    struct ipmi_bmc_bus *bus);

#endif /* __LINUX_IPMI_BMC_H */
