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

#define pr_fmt(fmt)        "ipmi-bt-i2c: " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/ipmi_smi.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/types.h>

#define IPMI_BT_I2C_TIMEOUT (msecs_to_jiffies(1000))

/* If we don't have netfn_lun, seq, and cmd, we might as well have nothing. */
#define IPMI_BT_I2C_LEN_MIN 3
/* We need at least netfn_lun, seq, cmd, and completion. */
#define IPMI_BT_I2C_RESPONSE_LEN_MIN 4
#define IPMI_BT_I2C_MSG_MAX_PAYLOAD_SIZE 252

struct ipmi_bt_i2c_msg {
	u8 len;
	u8 netfn_lun;
	u8 seq;
	u8 cmd;
	u8 payload[IPMI_BT_I2C_MSG_MAX_PAYLOAD_SIZE];
} __packed;

#define IPMI_BT_I2C_MAX_SMI_SIZE 254 /* Need extra byte for seq. */
#define IPMI_BT_I2C_SMI_MSG_HEADER_SIZE 2

struct ipmi_bt_i2c_smi_msg {
	u8 netfn_lun;
	u8 cmd;
	u8 payload[IPMI_MAX_MSG_LENGTH - 2];
} __packed;

static inline u32 bt_msg_len(struct ipmi_bt_i2c_msg *bt_request)
{
	return bt_request->len + 1;
}

#define IPMI_BT_I2C_SEQ_MAX 256

struct ipmi_bt_i2c_seq_entry {
	struct ipmi_smi_msg		*msg;
	unsigned long			send_time;
};

struct ipmi_bt_i2c_master {
	struct ipmi_device_id		ipmi_id;
	struct i2c_client		*client;
	ipmi_smi_t			intf;
	spinlock_t			lock;
	struct ipmi_bt_i2c_seq_entry	seq_msg_map[IPMI_BT_I2C_SEQ_MAX];
	struct work_struct		ipmi_bt_i2c_recv_work;
	struct work_struct		ipmi_bt_i2c_send_work;
	struct ipmi_smi_msg		*msg_to_send;
};

static const unsigned long write_timeout = 25;

static int ipmi_bt_i2c_send_request(struct ipmi_bt_i2c_master *master,
				    struct ipmi_bt_i2c_msg *request)
{
	struct i2c_client *client = master->client;
	unsigned long timeout, read_time;
	u8 *buf = (u8 *) request;
	int ret;

	timeout = jiffies + msecs_to_jiffies(write_timeout);
	do {
		read_time = jiffies;
		ret = i2c_master_send(client, buf, bt_msg_len(request));
		if (ret >= 0)
			return 0;
		usleep_range(1000, 1500);
	} while (time_before(read_time, timeout));
	return ret;
}

static int ipmi_bt_i2c_receive_response(struct ipmi_bt_i2c_master *master,
					struct ipmi_bt_i2c_msg *response)
{
	struct i2c_client *client = master->client;
	unsigned long timeout, read_time;
	u8 *buf = (u8 *) response;
	u8 len = 0;
	int ret;

	/*
	 * Slave may not NACK when not ready, so we peek at the first byte to
	 * see if it is a valid length.
	 */
	ret = i2c_master_recv(client, &len, 1);
	while (ret != 1 || len == 0) {
		if (ret < 0)
			return ret;

		usleep_range(1000, 1500);

		/* Signal received: quit syscall. */
		if (signal_pending(current))
			return -ERESTARTSYS;

		ret = i2c_master_recv(client, &len, 1);
	}

	timeout = jiffies + msecs_to_jiffies(write_timeout);
	do {
		read_time = jiffies;
		ret = i2c_master_recv(client, buf, len + 1);
		if (ret >= 0)
			return 0;
		usleep_range(1000, 1500);
	} while (time_before(read_time, timeout));
	return ret;
}

static int ipmi_bt_i2c_start_processing(void *data, ipmi_smi_t intf)
{
	struct ipmi_bt_i2c_master *master = data;

	master->intf = intf;

	return 0;
}

static void __ipmi_bt_i2c_error_reply(struct ipmi_bt_i2c_master *master,
				      struct ipmi_smi_msg *msg,
				      u8 completion_code)
{
	struct ipmi_bt_i2c_smi_msg *response;
	struct ipmi_bt_i2c_smi_msg *request;

	response = (struct ipmi_bt_i2c_smi_msg *) msg->rsp;
	request = (struct ipmi_bt_i2c_smi_msg *) msg->data;

	response->netfn_lun = request->netfn_lun | 0x4;
	response->cmd = request->cmd;
	response->payload[0] = completion_code;
	msg->rsp_size = 3;
	ipmi_smi_msg_received(master->intf, msg);
}

static void ipmi_bt_i2c_error_reply(struct ipmi_bt_i2c_master *master,
				    struct ipmi_smi_msg *msg,
				    u8 completion_code)
{
	unsigned long flags;

	spin_lock_irqsave(&master->lock, flags);
	__ipmi_bt_i2c_error_reply(master, msg, completion_code);
	spin_unlock_irqrestore(&master->lock, flags);
}

/*
 * ipmi_bt_i2c_smi_msg contains a payload and 2 header fields, each 1 byte:
 * netfn_lun and cmd. They're passed to OpenIPMI within an ipmi_smi_msg struct
 * along with their length.
 *
 * ipmi_bt_i2c_msg contains a payload and 4 header fields: the two above in
 * addition to seq and len. However, len is not included in the length count so
 * this message encapsulation is considered 1 byte longer than the other.
 */
static u8 ipmi_bt_i2c_smi_to_bt_len(u8 smi_msg_len)
{
	/* Only field that BT adds to the header is seq. */
	return smi_msg_len + 1;
}

static u8 ipmi_bt_i2c_bt_to_smi_len(struct ipmi_bt_i2c_msg *bt_msg)
{
	/* Subtract one byte for seq (opposite of above) */
	return bt_msg->len - 1;
}

static size_t ipmi_bt_i2c_payload_len(struct ipmi_bt_i2c_msg *bt_msg)
{
	/* Subtract one byte for each: netfn_lun, seq, cmd. */
	return bt_msg->len - 3;
}

static bool ipmi_bt_i2c_assign_seq(struct ipmi_bt_i2c_master *master,
				   struct ipmi_smi_msg *msg, u8 *ret_seq)
{
	struct ipmi_bt_i2c_seq_entry *entry;
	bool did_cleanup = false;
	unsigned long flags;
	u8 seq;

	spin_lock_irqsave(&master->lock, flags);
retry:
	for (seq = 0; seq < IPMI_BT_I2C_SEQ_MAX; seq++) {
		if (!master->seq_msg_map[seq].msg) {
			master->seq_msg_map[seq].msg = msg;
			master->seq_msg_map[seq].send_time = jiffies;
			spin_unlock_irqrestore(&master->lock, flags);
			*ret_seq = seq;
			return true;
		}
	}

	if (did_cleanup) {
		spin_unlock_irqrestore(&master->lock, flags);
		return false;
	}

	/*
	 * TODO: we should do cleanup at times other than only when we run out
	 * of sequence numbers.
	 */
	for (seq = 0; seq < IPMI_BT_I2C_SEQ_MAX; seq++) {
		entry = &master->seq_msg_map[seq];
		if (entry->msg &&
		    time_after(entry->send_time + IPMI_BT_I2C_TIMEOUT,
			       jiffies)) {
			__ipmi_bt_i2c_error_reply(master, entry->msg,
						  IPMI_TIMEOUT_ERR);
			entry->msg = NULL;
		}
	}
	did_cleanup = true;
	goto retry;
}

static struct ipmi_smi_msg *ipmi_bt_i2c_find_msg(
		struct ipmi_bt_i2c_master *master, u8 seq)
{
	struct ipmi_smi_msg *msg;
	unsigned long flags;

	spin_lock_irqsave(&master->lock, flags);
	msg = master->seq_msg_map[seq].msg;
	spin_unlock_irqrestore(&master->lock, flags);
	return msg;
}

static void ipmi_bt_i2c_free_seq(struct ipmi_bt_i2c_master *master, u8 seq)
{
	unsigned long flags;

	spin_lock_irqsave(&master->lock, flags);
	master->seq_msg_map[seq].msg = NULL;
	spin_unlock_irqrestore(&master->lock, flags);
}

static void ipmi_bt_i2c_send_workfn(struct work_struct *work)
{
	struct ipmi_bt_i2c_smi_msg *smi_msg;
	struct ipmi_bt_i2c_master *master;
	struct ipmi_bt_i2c_msg bt_msg;
	struct ipmi_smi_msg *msg;
	size_t smi_msg_size;
	unsigned long flags;

	master = container_of(work, struct ipmi_bt_i2c_master,
			      ipmi_bt_i2c_send_work);

	msg = master->msg_to_send;
	smi_msg_size = msg->data_size;
	smi_msg = (struct ipmi_bt_i2c_smi_msg *) msg->data;

	if (smi_msg_size > IPMI_BT_I2C_MAX_SMI_SIZE) {
		ipmi_bt_i2c_error_reply(master, msg, IPMI_REQ_LEN_EXCEEDED_ERR);
		return;
	}

	if (smi_msg_size < IPMI_BT_I2C_SMI_MSG_HEADER_SIZE) {
		ipmi_bt_i2c_error_reply(master, msg, IPMI_REQ_LEN_INVALID_ERR);
		return;
	}

	if (!ipmi_bt_i2c_assign_seq(master, msg, &bt_msg.seq)) {
		ipmi_bt_i2c_error_reply(master, msg, IPMI_NODE_BUSY_ERR);
		return;
	}

	bt_msg.len = ipmi_bt_i2c_smi_to_bt_len(smi_msg_size);
	bt_msg.netfn_lun = smi_msg->netfn_lun;
	bt_msg.cmd = smi_msg->cmd;
	memcpy(bt_msg.payload, smi_msg->payload,
	       ipmi_bt_i2c_payload_len(&bt_msg));

	if (ipmi_bt_i2c_send_request(master, &bt_msg) < 0) {
		ipmi_bt_i2c_free_seq(master, bt_msg.seq);
		ipmi_bt_i2c_error_reply(master, msg, IPMI_BUS_ERR);
	}

	spin_lock_irqsave(&master->lock, flags);
	master->msg_to_send = NULL;
	spin_unlock_irqrestore(&master->lock, flags);
}

void ipmi_bt_i2c_recv_workfn(struct work_struct *work)
{
	struct ipmi_bt_i2c_smi_msg *smi_msg;
	struct ipmi_bt_i2c_master *master;
	struct ipmi_bt_i2c_msg bt_msg;
	struct ipmi_smi_msg *msg;

	master = container_of(work, struct ipmi_bt_i2c_master,
			      ipmi_bt_i2c_recv_work);

	if (ipmi_bt_i2c_receive_response(master, &bt_msg) < 0)
		return;

	if (bt_msg.len < IPMI_BT_I2C_LEN_MIN)
		return;

	msg = ipmi_bt_i2c_find_msg(master, bt_msg.seq);
	if (!msg)
		return;

	ipmi_bt_i2c_free_seq(master, bt_msg.seq);

	if (bt_msg.len < IPMI_BT_I2C_RESPONSE_LEN_MIN)
		ipmi_bt_i2c_error_reply(master, msg, IPMI_ERR_MSG_TRUNCATED);

	msg->rsp_size = ipmi_bt_i2c_bt_to_smi_len(&bt_msg);
	smi_msg = (struct ipmi_bt_i2c_smi_msg *) msg->rsp;
	smi_msg->netfn_lun = bt_msg.netfn_lun;
	smi_msg->cmd = bt_msg.cmd;
	memcpy(smi_msg->payload, bt_msg.payload,
	       ipmi_bt_i2c_payload_len(&bt_msg));
	ipmi_smi_msg_received(master->intf, msg);
}

static void ipmi_bt_i2c_sender(void *data, struct ipmi_smi_msg *msg)
{
	struct ipmi_bt_i2c_master *master = data;
	unsigned long flags;

	spin_lock_irqsave(&master->lock, flags);
	if (master->msg_to_send) {
		/*
		 * TODO(benjaminfair): Queue messages to send instead of only
		 * keeping one.
		 */
		__ipmi_bt_i2c_error_reply(master, msg, IPMI_NODE_BUSY_ERR);
	} else {
		master->msg_to_send = msg;
		schedule_work(&master->ipmi_bt_i2c_send_work);
	}
	spin_unlock_irqrestore(&master->lock, flags);
}

static void ipmi_bt_i2c_request_events(void *data)
{
	struct ipmi_bt_i2c_master *master = data;

	schedule_work(&master->ipmi_bt_i2c_recv_work);
}

static void ipmi_bt_i2c_set_run_to_completion(void *data,
					      bool run_to_completion)
{
}

static void ipmi_bt_i2c_poll(void *data)
{
	struct ipmi_bt_i2c_master *master = data;

	schedule_work(&master->ipmi_bt_i2c_recv_work);
}

static struct ipmi_smi_handlers ipmi_bt_i2c_smi_handlers = {
	.owner			= THIS_MODULE,
	.start_processing	= ipmi_bt_i2c_start_processing,
	.sender			= ipmi_bt_i2c_sender,
	.request_events		= ipmi_bt_i2c_request_events,
	.set_run_to_completion	= ipmi_bt_i2c_set_run_to_completion,
	.poll			= ipmi_bt_i2c_poll,
};

static int ipmi_bt_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct ipmi_bt_i2c_master *master;
	int ret;

	master = devm_kzalloc(&client->dev, sizeof(struct ipmi_bt_i2c_master),
			      GFP_KERNEL);
	if (!master)
		return -ENOMEM;

	spin_lock_init(&master->lock);
	INIT_WORK(&master->ipmi_bt_i2c_recv_work, ipmi_bt_i2c_recv_workfn);
	INIT_WORK(&master->ipmi_bt_i2c_send_work, ipmi_bt_i2c_send_workfn);
	master->client = client;
	i2c_set_clientdata(client, master);

	/*
	 * TODO(benjaminfair): read ipmi_device_id from BMC to determine version
	 * information and be able to tell multiple BMCs apart
	 */
	ret = ipmi_register_smi(&ipmi_bt_i2c_smi_handlers, master,
				&master->ipmi_id, &client->dev, 0);

	return ret;
}

static int ipmi_bt_i2c_remove(struct i2c_client *client)
{
	struct ipmi_bt_i2c_master *master;

	master = i2c_get_clientdata(client);
	return ipmi_unregister_smi(master->intf);
}

static const struct acpi_device_id ipmi_bt_i2c_acpi_id[] = {
	{"BTMA0001", 0},
	{},
};
MODULE_DEVICE_TABLE(acpi, ipmi_bt_i2c_acpi_id);

static const struct i2c_device_id ipmi_bt_i2c_i2c_id[] = {
	{"ipmi-bt-i2c", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, ipmi_bt_i2c_i2c_id);

static struct i2c_driver ipmi_bt_i2c_driver = {
	.driver = {
		.name = "ipmi-bt-i2c",
		.acpi_match_table = ipmi_bt_i2c_acpi_id,
	},
	.id_table = ipmi_bt_i2c_i2c_id,
	.probe = ipmi_bt_i2c_probe,
	.remove = ipmi_bt_i2c_remove,
};
module_i2c_driver(ipmi_bt_i2c_driver);

MODULE_AUTHOR("Brendan Higgins <brendanhiggins@google.com>");
MODULE_DESCRIPTION("IPMI Block Transfer over I2C.");
MODULE_LICENSE("GPL v2");
