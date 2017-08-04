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
#include <linux/i2c.h>
#include <linux/ipmi_bmc.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define PFX "IPMI BMC BT-I2C: "

struct bt_i2c_slave {
	struct ipmi_bmc_bus	bus;
	struct i2c_client	*client;
	struct ipmi_bmc_ctx	*bmc_ctx;
	struct bt_msg		request;
	struct bt_msg		response;
	bool			response_in_progress;
	size_t			msg_idx;
	spinlock_t		lock;
};

static bool bt_i2c_is_response_open(struct ipmi_bmc_bus *bus)
{
	struct bt_i2c_slave *bt_slave;
	bool response_in_progress;
	unsigned long flags;

	bt_slave = container_of(bus, struct bt_i2c_slave, bus);

	spin_lock_irqsave(&bt_slave->lock, flags);
	response_in_progress = bt_slave->response_in_progress;
	spin_unlock_irqrestore(&bt_slave->lock, flags);

	return !response_in_progress;
}

static int bt_i2c_send_response(struct ipmi_bmc_bus *bus,
				struct bt_msg *bt_response)
{
	struct bt_i2c_slave *bt_slave;
	unsigned long flags;

	bt_slave = container_of(bus, struct bt_i2c_slave, bus);

	spin_lock_irqsave(&bt_slave->lock, flags);
	if (bt_slave->response_in_progress) {
		spin_unlock_irqrestore(&bt_slave->lock, flags);
		return -EAGAIN;
	}

	memcpy(&bt_slave->response, bt_response, sizeof(*bt_response));
	bt_slave->response_in_progress = true;
	spin_unlock_irqrestore(&bt_slave->lock, flags);
	return 0;
}

/* Called with bt_slave->lock held. */
static int complete_response(struct bt_i2c_slave *bt_slave)
{
	/* Invalidate response in buffer to denote it having been sent. */
	bt_slave->response.len = 0;
	bt_slave->response_in_progress = false;
	ipmi_bmc_signal_response_open(bt_slave->bmc_ctx);
	return 0;
}

static int bt_i2c_slave_cb(struct i2c_client *client,
			   enum i2c_slave_event event, u8 *val)
{
	struct bt_i2c_slave *bt_slave = i2c_get_clientdata(client);
	u8 *buf;

	spin_lock(&bt_slave->lock);
	switch (event) {
	case I2C_SLAVE_WRITE_REQUESTED:
		bt_slave->msg_idx = 0;
		break;

	case I2C_SLAVE_WRITE_RECEIVED:
		buf = (u8 *) &bt_slave->request;
		if (bt_slave->msg_idx >= sizeof(struct bt_msg))
			break;

		buf[bt_slave->msg_idx++] = *val;
		if (bt_slave->msg_idx >= bt_msg_len(&bt_slave->request))
			ipmi_bmc_handle_request(bt_slave->bmc_ctx,
						&bt_slave->request);
		break;

	case I2C_SLAVE_READ_REQUESTED:
		buf = (u8 *) &bt_slave->response;
		bt_slave->msg_idx = 0;
		*val = buf[bt_slave->msg_idx];
		/*
		 * Do not increment buffer_idx here, because we don't know if
		 * this byte will be actually used. Read Linux I2C slave docs
		 * for details.
		 */
		break;

	case I2C_SLAVE_READ_PROCESSED:
		buf = (u8 *) &bt_slave->response;
		if (bt_slave->response.len &&
		    bt_slave->msg_idx < bt_msg_len(&bt_slave->response)) {
			*val = buf[++bt_slave->msg_idx];
		} else {
			*val = 0;
		}
		if (bt_slave->msg_idx + 1 >= bt_msg_len(&bt_slave->response))
			complete_response(bt_slave);
		break;

	case I2C_SLAVE_STOP:
		bt_slave->msg_idx = 0;
		break;

	default:
		break;
	}
	spin_unlock(&bt_slave->lock);

	return 0;
}

static int bt_i2c_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct bt_i2c_slave *bt_slave;
	int ret;

	bt_slave = devm_kzalloc(&client->dev, sizeof(*bt_slave),
				GFP_KERNEL);
	if (!bt_slave)
		return -ENOMEM;

	spin_lock_init(&bt_slave->lock);
	bt_slave->response_in_progress = false;
	bt_slave->bus.send_response = bt_i2c_send_response;
	bt_slave->bus.is_response_open = bt_i2c_is_response_open;

	bt_slave->bmc_ctx = ipmi_bmc_get_global_ctx();

	ret = ipmi_bmc_register_bus(bt_slave->bmc_ctx, &bt_slave->bus);
	if (ret) {
		pr_err(PFX "Failed to register IPMI BMC bus\n");
		return ret;
	}

	bt_slave->client = client;
	i2c_set_clientdata(client, bt_slave);
	ret = i2c_slave_register(client, bt_i2c_slave_cb);

	if (ret) {
		ipmi_bmc_unregister_bus(bt_slave->bmc_ctx, &bt_slave->bus);
		return ret;
	}

	return 0;
}

static int bt_i2c_remove(struct i2c_client *client)
{
	struct bt_i2c_slave *bt_slave = i2c_get_clientdata(client);

	i2c_slave_unregister(client);
	return ipmi_bmc_unregister_bus(bt_slave->bmc_ctx, &bt_slave->bus);
}

static const struct i2c_device_id bt_i2c_id[] = {
	{"ipmi-bmc-bt-i2c", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, bt_i2c_id);

static struct i2c_driver bt_i2c_driver = {
	.driver = {
		.name		= "ipmi-bmc-bt-i2c",
	},
	.probe		= bt_i2c_probe,
	.remove		= bt_i2c_remove,
	.id_table	= bt_i2c_id,
};
module_i2c_driver(bt_i2c_driver);

MODULE_AUTHOR("Brendan Higgins <brendanhiggins@google.com>");
MODULE_DESCRIPTION("BMC-side IPMI Block Transfer over I2C.");
MODULE_LICENSE("GPL v2");
