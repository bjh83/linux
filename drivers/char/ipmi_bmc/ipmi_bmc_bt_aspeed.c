/*
 * Copyright (c) 2015-2016, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ipmi_bmc.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/sched.h>
#include <linux/timer.h>

#define DEVICE_NAME "ipmi-bmc-bt-aspeed"

#define BT_IO_BASE	0xe4
#define BT_IRQ		10

#define BT_CR0		0x0
#define   BT_CR0_IO_BASE		16
#define   BT_CR0_IRQ			12
#define   BT_CR0_EN_CLR_SLV_RDP		0x8
#define   BT_CR0_EN_CLR_SLV_WRP		0x4
#define   BT_CR0_ENABLE_IBT		0x1
#define BT_CR1		0x4
#define   BT_CR1_IRQ_H2B	0x01
#define   BT_CR1_IRQ_HBUSY	0x40
#define BT_CR2		0x8
#define   BT_CR2_IRQ_H2B	0x01
#define   BT_CR2_IRQ_HBUSY	0x40
#define BT_CR3		0xc
#define BT_CTRL		0x10
#define   BT_CTRL_B_BUSY		0x80
#define   BT_CTRL_H_BUSY		0x40
#define   BT_CTRL_OEM0			0x20
#define   BT_CTRL_SMS_ATN		0x10
#define   BT_CTRL_B2H_ATN		0x08
#define   BT_CTRL_H2B_ATN		0x04
#define   BT_CTRL_CLR_RD_PTR		0x02
#define   BT_CTRL_CLR_WR_PTR		0x01
#define BT_BMC2HOST	0x14
#define BT_INTMASK	0x18
#define   BT_INTMASK_B2H_IRQEN		0x01
#define   BT_INTMASK_B2H_IRQ		0x02
#define   BT_INTMASK_BMC_HWRST		0x80

#define BT_BMC_BUFFER_SIZE 256

struct bt_bmc {
	struct ipmi_bmc_bus	bus;
	struct device		dev;
	struct ipmi_bmc_ctx	*bmc_ctx;
	struct bt_msg		request;
	struct regmap		*map;
	int			offset;
	int			irq;
	struct timer_list	poll_timer;
	spinlock_t		lock;
};

static const struct regmap_config bt_regmap_cfg = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static u8 bt_inb(struct bt_bmc *bt_bmc, int reg)
{
	uint32_t val = 0;
	int rc;

	rc = regmap_read(bt_bmc->map, bt_bmc->offset + reg, &val);
	WARN(rc != 0, "regmap_read() failed: %d\n", rc);

	return rc == 0 ? (u8) val : 0;
}

static void bt_outb(struct bt_bmc *bt_bmc, u8 data, int reg)
{
	int rc;

	rc = regmap_write(bt_bmc->map, bt_bmc->offset + reg, data);
	WARN(rc != 0, "regmap_write() failed: %d\n", rc);
}

static void clr_rd_ptr(struct bt_bmc *bt_bmc)
{
	bt_outb(bt_bmc, BT_CTRL_CLR_RD_PTR, BT_CTRL);
}

static void clr_wr_ptr(struct bt_bmc *bt_bmc)
{
	bt_outb(bt_bmc, BT_CTRL_CLR_WR_PTR, BT_CTRL);
}

static void clr_h2b_atn(struct bt_bmc *bt_bmc)
{
	bt_outb(bt_bmc, BT_CTRL_H2B_ATN, BT_CTRL);
}

static void set_b_busy(struct bt_bmc *bt_bmc)
{
	if (!(bt_inb(bt_bmc, BT_CTRL) & BT_CTRL_B_BUSY))
		bt_outb(bt_bmc, BT_CTRL_B_BUSY, BT_CTRL);
}

static void clr_b_busy(struct bt_bmc *bt_bmc)
{
	if (bt_inb(bt_bmc, BT_CTRL) & BT_CTRL_B_BUSY)
		bt_outb(bt_bmc, BT_CTRL_B_BUSY, BT_CTRL);
}

static void set_b2h_atn(struct bt_bmc *bt_bmc)
{
	bt_outb(bt_bmc, BT_CTRL_B2H_ATN, BT_CTRL);
}

static u8 bt_read(struct bt_bmc *bt_bmc)
{
	return bt_inb(bt_bmc, BT_BMC2HOST);
}

static ssize_t bt_readn(struct bt_bmc *bt_bmc, u8 *buf, size_t n)
{
	int i;

	for (i = 0; i < n; i++)
		buf[i] = bt_read(bt_bmc);
	return n;
}

static void bt_write(struct bt_bmc *bt_bmc, u8 c)
{
	bt_outb(bt_bmc, c, BT_BMC2HOST);
}

static ssize_t bt_writen(struct bt_bmc *bt_bmc, u8 *buf, size_t n)
{
	int i;

	for (i = 0; i < n; i++)
		bt_write(bt_bmc, buf[i]);
	return n;
}

/* TODO(benjaminfair): support ioctl BT_BMC_IOCTL_SMS_ATN */
static void set_sms_atn(struct bt_bmc *bt_bmc)
{
	bt_outb(bt_bmc, BT_CTRL_SMS_ATN, BT_CTRL);
}

/* Called with bt_bmc->lock held */
static bool __is_request_avail(struct bt_bmc *bt_bmc)
{
	return bt_inb(bt_bmc, BT_CTRL) & BT_CTRL_H2B_ATN;
}

static bool is_request_avail(struct bt_bmc *bt_bmc)
{
	unsigned long flags;
	bool result;

	spin_lock_irqsave(&bt_bmc->lock, flags);
	result = __is_request_avail(bt_bmc);
	spin_unlock_irqrestore(&bt_bmc->lock, flags);

	return result;
}

/*
 * The BT (Block Transfer) interface means that entire messages are
 * buffered by the host before a notification is sent to the BMC that
 * there is data to be read. The first byte is the length and the
 * message data follows. The read operation just tries to capture the
 * whole before returning it to userspace.
 *
 * BT Message format :
 *
 *    Byte 1  Byte 2     Byte 3  Byte 4  Byte 5:N
 *    Length  NetFn/LUN  Seq     Cmd     Data
 *
 */
static void get_request(struct bt_bmc *bt_bmc)
{
	u8 *request_buf = (u8 *) &bt_bmc->request;
	unsigned long flags;

	spin_lock_irqsave(&bt_bmc->lock, flags);

	if (!__is_request_avail(bt_bmc)) {
		spin_unlock_irqrestore(&bt_bmc->lock, flags);
		return;
	}

	set_b_busy(bt_bmc);
	clr_h2b_atn(bt_bmc);
	clr_rd_ptr(bt_bmc);

	bt_bmc->request.len = bt_read(bt_bmc);
	bt_readn(bt_bmc, request_buf + 1, bt_bmc->request.len);

	clr_b_busy(bt_bmc);
	ipmi_bmc_handle_request(bt_bmc->bmc_ctx, &bt_bmc->request);

	spin_unlock_irqrestore(&bt_bmc->lock, flags);
}

static bool bt_bmc_is_response_open(struct ipmi_bmc_bus *bus)
{
	struct bt_bmc *bt_bmc = container_of(bus, struct bt_bmc, bus);
	bool response_in_progress;
	unsigned long flags;

	spin_lock_irqsave(&bt_bmc->lock, flags);
	response_in_progress = bt_inb(bt_bmc, BT_CTRL) & (BT_CTRL_H_BUSY |
							  BT_CTRL_B2H_ATN);
	spin_unlock_irqrestore(&bt_bmc->lock, flags);

	return !response_in_progress;
}

/*
 * BT Message response format :
 *
 *    Byte 1  Byte 2     Byte 3  Byte 4  Byte 5  Byte 6:N
 *    Length  NetFn/LUN  Seq     Cmd     Code    Data
 */
static int bt_bmc_send_response(struct ipmi_bmc_bus *bus,
				struct bt_msg *bt_response)
{
	struct bt_bmc *bt_bmc = container_of(bus, struct bt_bmc, bus);
	unsigned long flags;

	spin_lock_irqsave(&bt_bmc->lock, flags);
	if (!bt_bmc_is_response_open(bus)) {
		spin_unlock_irqrestore(&bt_bmc->lock, flags);
		return -EAGAIN;
	}

	clr_wr_ptr(bt_bmc);
	bt_writen(bt_bmc, (u8 *) bt_response, bt_msg_len(bt_response));
	set_b2h_atn(bt_bmc);

	spin_unlock_irqrestore(&bt_bmc->lock, flags);
	return 0;
}

static void poll_timer(unsigned long data)
{
	struct bt_bmc *bt_bmc = (void *)data;

	bt_bmc->poll_timer.expires += msecs_to_jiffies(500);

	if (bt_bmc_is_response_open(&bt_bmc->bus))
		ipmi_bmc_signal_response_open(bt_bmc->bmc_ctx);

	if (is_request_avail(bt_bmc))
		get_request(bt_bmc);

	add_timer(&bt_bmc->poll_timer);
}

static irqreturn_t bt_bmc_irq(int irq, void *arg)
{
	struct bt_bmc *bt_bmc = arg;
	u32 reg;
	int rc;

	rc = regmap_read(bt_bmc->map, bt_bmc->offset + BT_CR2, &reg);
	if (rc)
		return IRQ_NONE;

	reg &= BT_CR2_IRQ_H2B | BT_CR2_IRQ_HBUSY;
	if (!reg)
		return IRQ_NONE;

	/* ack pending IRQs */
	regmap_write(bt_bmc->map, bt_bmc->offset + BT_CR2, reg);

	if (bt_bmc_is_response_open(&bt_bmc->bus))
		ipmi_bmc_signal_response_open(bt_bmc->bmc_ctx);

	if (is_request_avail(bt_bmc))
		get_request(bt_bmc);

	return IRQ_HANDLED;
}

static int bt_bmc_config_irq(struct bt_bmc *bt_bmc,
			     struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int rc;

	bt_bmc->irq = platform_get_irq(pdev, 0);
	if (!bt_bmc->irq)
		return -ENODEV;

	rc = devm_request_irq(dev, bt_bmc->irq, bt_bmc_irq, IRQF_SHARED,
			      DEVICE_NAME, bt_bmc);
	if (rc < 0) {
		dev_warn(dev, "Unable to request IRQ %d\n", bt_bmc->irq);
		bt_bmc->irq = 0;
		return rc;
	}

	/*
	 * Configure IRQs on the bmc clearing the H2B and HBUSY bits;
	 * H2B will be asserted when the bmc has data for us; HBUSY
	 * will be cleared (along with B2H) when we can write the next
	 * message to the BT buffer
	 */
	rc = regmap_update_bits(bt_bmc->map, bt_bmc->offset + BT_CR1,
				(BT_CR1_IRQ_H2B | BT_CR1_IRQ_HBUSY),
				(BT_CR1_IRQ_H2B | BT_CR1_IRQ_HBUSY));

	return rc;
}

static int bt_bmc_probe(struct platform_device *pdev)
{
	struct bt_bmc *bt_bmc;
	struct device *dev;
	int rc;

	if (!pdev || !pdev->dev.of_node)
		return -ENODEV;

	dev = &pdev->dev;
	dev_info(dev, "Found bt bmc device\n");

	bt_bmc = devm_kzalloc(dev, sizeof(*bt_bmc), GFP_KERNEL);
	if (!bt_bmc)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, bt_bmc);

	bt_bmc->map = syscon_node_to_regmap(pdev->dev.parent->of_node);
	if (IS_ERR(bt_bmc->map)) {
		struct resource *res;
		void __iomem *base;

		/*
		 * Assume it's not the MFD-based devicetree description, in
		 * which case generate a regmap ourselves
		 */
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(base))
			return PTR_ERR(base);

		bt_bmc->map = devm_regmap_init_mmio(dev, base, &bt_regmap_cfg);
		bt_bmc->offset = 0;
	} else {
		rc = of_property_read_u32(dev->of_node, "reg", &bt_bmc->offset);
		if (rc)
			return rc;
	}

	spin_lock_init(&bt_bmc->lock);
	bt_bmc->bus.send_response = bt_bmc_send_response;
	bt_bmc->bus.is_response_open = bt_bmc_is_response_open;
	bt_bmc->bmc_ctx = ipmi_bmc_get_global_ctx();

	rc = ipmi_bmc_register_bus(bt_bmc->bmc_ctx, &bt_bmc->bus);
	if (rc) {
		dev_err(dev, "Unable to register IPMI BMC bus\n");
		return rc;
	}

	bt_bmc_config_irq(bt_bmc, pdev);

	if (bt_bmc->irq) {
		dev_info(dev, "Using IRQ %d\n", bt_bmc->irq);
	} else {
		dev_info(dev, "No IRQ; using timer\n");
		setup_timer(&bt_bmc->poll_timer, poll_timer,
			    (unsigned long)bt_bmc);
		bt_bmc->poll_timer.expires = jiffies + msecs_to_jiffies(10);
		add_timer(&bt_bmc->poll_timer);
	}

	regmap_write(bt_bmc->map, bt_bmc->offset + BT_CR0,
		     (BT_IO_BASE << BT_CR0_IO_BASE) |
		     (BT_IRQ << BT_CR0_IRQ) |
		     BT_CR0_EN_CLR_SLV_RDP |
		     BT_CR0_EN_CLR_SLV_WRP |
		     BT_CR0_ENABLE_IBT);

	clr_b_busy(bt_bmc);

	return 0;
}

static int bt_bmc_remove(struct platform_device *pdev)
{
	struct bt_bmc *bt_bmc = dev_get_drvdata(&pdev->dev);
	int rc;

	rc = ipmi_bmc_unregister_bus(bt_bmc->bmc_ctx, &bt_bmc->bus);
	if (!bt_bmc->irq)
		del_timer_sync(&bt_bmc->poll_timer);
	return rc;
}

static const struct of_device_id bt_bmc_match[] = {
	{ .compatible = "aspeed,ast2400-ibt-bmc" },
	{ .compatible = "aspeed,ast2500-ibt-bmc" },
	{ },
};

static struct platform_driver bt_bmc_driver = {
	.driver = {
		.name		= DEVICE_NAME,
		.of_match_table = bt_bmc_match,
	},
	.probe = bt_bmc_probe,
	.remove = bt_bmc_remove,
};

module_platform_driver(bt_bmc_driver);

MODULE_DEVICE_TABLE(of, bt_bmc_match);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alistair Popple <alistair@popple.id.au>");
MODULE_DESCRIPTION("Linux device interface to the IPMI BT interface");
