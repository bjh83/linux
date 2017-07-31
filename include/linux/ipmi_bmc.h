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

#endif /* __LINUX_IPMI_BMC_H */
