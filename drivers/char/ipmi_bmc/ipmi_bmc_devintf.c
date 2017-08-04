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

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/ipmi_bmc.h>
#include <linux/kfifo.h>
#include <linux/log2.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#define PFX "IPMI BMC devintf: "

#define DEVICE_NAME "ipmi-bt-host"

/* Must be a power of two */
#define REQUEST_FIFO_SIZE roundup_pow_of_two(BT_MSG_SEQ_MAX)

struct bmc_devintf_data {
	struct miscdevice	miscdev;
	struct ipmi_bmc_device	bmc_device;
	struct ipmi_bmc_ctx	*bmc_ctx;
	wait_queue_head_t	wait_queue;
	/* FIFO of waiting messages */
	DECLARE_KFIFO(requests, struct bt_msg, REQUEST_FIFO_SIZE);
};

static inline struct bmc_devintf_data *file_to_bmc_devintf_data(
		struct file *file)
{
	return container_of(file->private_data, struct bmc_devintf_data,
			    miscdev);
}

static ssize_t ipmi_bmc_devintf_read(struct file *file, char __user *buf,
				     size_t count, loff_t *ppos)
{
	struct bmc_devintf_data *devintf_data = file_to_bmc_devintf_data(file);
	bool non_blocking = file->f_flags & O_NONBLOCK;
	struct bt_msg msg;

	if (non_blocking && kfifo_is_empty(&devintf_data->requests)) {
		return -EAGAIN;
	} else if (!non_blocking) {
		if (wait_event_interruptible(devintf_data->wait_queue,
				!kfifo_is_empty(&devintf_data->requests)))
			return -ERESTARTSYS;
	}

	/* TODO(benjaminfair): eliminate this extra copy */
	if (unlikely(!kfifo_get(&devintf_data->requests, &msg))) {
		pr_err(PFX "Unable to read request from fifo\n");
		return -EIO;
	}

	/* TODO(benjaminfair): handle partial reads of a message */
	if (count > bt_msg_len(&msg))
		count = bt_msg_len(&msg);

	if (copy_to_user(buf, &msg, count))
		return -EFAULT;

	return count;
}

static ssize_t ipmi_bmc_devintf_write(struct file *file, const char __user *buf,
				      size_t count, loff_t *ppos)
{
	struct bmc_devintf_data *devintf_data = file_to_bmc_devintf_data(file);
	bool non_blocking = file->f_flags & O_NONBLOCK;
	struct bt_msg msg;
	ssize_t ret = 0;

	if (count > sizeof(struct bt_msg))
		return -EINVAL;

	if (copy_from_user(&msg, buf, count))
		return -EFAULT;

	if (count != bt_msg_len(&msg))
		return -EINVAL;

	ret = ipmi_bmc_send_response(devintf_data->bmc_ctx, &msg);

	/* Try again if blocking is allowed */
	while (!non_blocking && ret == -EAGAIN) {
		if (wait_event_interruptible(devintf_data->wait_queue,
				ipmi_bmc_is_response_open(
						devintf_data->bmc_ctx)))
			return -ERESTARTSYS;

		ret = ipmi_bmc_send_response(devintf_data->bmc_ctx, &msg);
	}

	if (ret < 0)
		return ret;
	else
		return count;
}

static unsigned int ipmi_bmc_devintf_poll(struct file *file, poll_table *wait)
{
	struct bmc_devintf_data *devintf_data = file_to_bmc_devintf_data(file);
	unsigned int mask = 0;

	poll_wait(file, &devintf_data->wait_queue, wait);

	if (!kfifo_is_empty(&devintf_data->requests))
		mask |= POLLIN;
	if (ipmi_bmc_is_response_open(devintf_data->bmc_ctx))
		mask |= POLLOUT;

	return mask;
}

static const struct file_operations ipmi_bmc_fops = {
	.owner		= THIS_MODULE,
	.read		= ipmi_bmc_devintf_read,
	.write		= ipmi_bmc_devintf_write,
	.poll		= ipmi_bmc_devintf_poll,
};

static inline struct bmc_devintf_data *device_to_bmc_devintf_data(
		struct ipmi_bmc_device *device)
{
	return container_of(device, struct bmc_devintf_data, bmc_device);
}

static int ipmi_bmc_devintf_handle_request(struct ipmi_bmc_device *device,
					   struct bt_msg *bt_request)
{
	struct bmc_devintf_data *devintf_data =
		device_to_bmc_devintf_data(device);

	if (!bt_request->len)
		return -EINVAL;

	if (!kfifo_put(&devintf_data->requests, *bt_request))
		return -EBUSY;

	wake_up_all(&devintf_data->wait_queue);

	return 0;
}

static bool ipmi_bmc_devintf_match_request(struct ipmi_bmc_device *device,
					   struct bt_msg *bt_request)
{
	/* Since this is a default device, match all requests */
	return true;
}

static void ipmi_bmc_devintf_signal_response_open(
		struct ipmi_bmc_device *device)
{
	struct bmc_devintf_data *devintf_data =
		device_to_bmc_devintf_data(device);

	wake_up_all(&devintf_data->wait_queue);
}

/*
 * TODO: if we want to support multiple interfaces, initialize this global
 * variable elsewhere
 */
static struct bmc_devintf_data *devintf_data;

static int __init init_ipmi_bmc_devintf(void)
{
	int ret;

	devintf_data = kzalloc(sizeof(*devintf_data), GFP_KERNEL);
	if (!devintf_data)
		return -ENOMEM;

	init_waitqueue_head(&devintf_data->wait_queue);
	INIT_KFIFO(devintf_data->requests);

	devintf_data->bmc_device.handle_request =
		ipmi_bmc_devintf_handle_request;
	devintf_data->bmc_device.match_request =
		ipmi_bmc_devintf_match_request;
	devintf_data->bmc_device.signal_response_open =
		ipmi_bmc_devintf_signal_response_open;

	devintf_data->miscdev.minor = MISC_DYNAMIC_MINOR;
	devintf_data->miscdev.name = DEVICE_NAME;
	devintf_data->miscdev.fops = &ipmi_bmc_fops;

	devintf_data->bmc_ctx = ipmi_bmc_get_global_ctx();

	ret = ipmi_bmc_register_default_device(devintf_data->bmc_ctx,
					       &devintf_data->bmc_device);
	if (ret) {
		pr_err(PFX "unable to register IPMI BMC device\n");
		return ret;
	}

	ret = misc_register(&devintf_data->miscdev);
	if (ret) {
		ipmi_bmc_unregister_default_device(devintf_data->bmc_ctx,
						   &devintf_data->bmc_device);
		pr_err(PFX "unable to register misc device\n");
		return ret;
	}

	pr_info(PFX "initialized\n");
	return 0;
}
module_init(init_ipmi_bmc_devintf);

static void __exit exit_ipmi_bmc_devintf(void)
{
	misc_deregister(&devintf_data->miscdev);
	WARN_ON(ipmi_bmc_unregister_default_device(devintf_data->bmc_ctx,
						   &devintf_data->bmc_device));
}
module_exit(exit_ipmi_bmc_devintf);

MODULE_AUTHOR("Benjamin Fair <benjaminfair@google.com>");
MODULE_DESCRIPTION("Device file interface to IPMI Block Transfer core.");
MODULE_LICENSE("GPL v2");
