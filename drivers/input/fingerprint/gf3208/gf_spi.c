/*
 * TEE driver for goodix fingerprint sensor
 * Copyright (C) 2016 Goodix
 * Copyright (C) 2021 Jebaitedneko <jebaitedneko@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/ioctl.h>
#include <linux/irq.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <net/netlink.h>
#include <net/sock.h>

#define GF_IOC_MAGIC 'g'
#define GF_IOC_INIT _IOR(GF_IOC_MAGIC, 0, uint8_t)
#define GF_IOC_RESET _IO(GF_IOC_MAGIC, 2)
#define GF_IOC_ENABLE_IRQ _IO(GF_IOC_MAGIC, 3)
#define GF_IOC_DISABLE_IRQ _IO(GF_IOC_MAGIC, 4)

#define GF_SPIDEV_NAME "goodix,fingerprint"
#define GF_DEV_NAME "goodix_fp"
#define GF_INPUT_NAME "uinput-goodix"
#define CHRD_DRIVER_NAME "goodix_fp_spi"
#define CLASS_NAME "goodix_fp"
#define N_SPI_MINORS 256

#define GF_NET_EVENT_IRQ 1
#define NETLINK_TEST 25
#define MAX_MSGSIZE 16

struct gf_dev {
	dev_t devt;
	struct list_head device_entry;
	struct platform_device *spi;
	struct input_dev *input;
	unsigned users;
	signed irq_gpio;
	signed reset_gpio;
	int irq;
	int irq_enabled;
};

static int SPIDEV_MAJOR;
static DECLARE_BITMAP(minors, N_SPI_MINORS);
static LIST_HEAD(device_list);
static struct gf_dev gf;

static int pid = -1;
static struct sock *nl_sk = NULL;

extern int fpsensor;

static inline void sendnlmsg(char *message) {
	struct sk_buff *skb_1;
	struct nlmsghdr *nlh;
	int len = NLMSG_SPACE(MAX_MSGSIZE);
	int slen = 0;
	if (!message || !nl_sk)
		return;
	skb_1 = alloc_skb(len, GFP_KERNEL);
	if (!skb_1)
		return;
	slen = strlen(message);
	nlh = nlmsg_put(skb_1, 0, 0, 0, MAX_MSGSIZE, 0);
	NETLINK_CB(skb_1).portid = 0;
	NETLINK_CB(skb_1).dst_group = 0;
	message[slen] = '\0';
	memcpy(NLMSG_DATA(nlh), message, slen + 1);
	netlink_unicast(nl_sk, skb_1, pid, MSG_DONTWAIT);
}

static inline void nl_data_ready(struct sk_buff *__skb) {
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	char str[MAX_MSGSIZE];
	skb = skb_get(__skb);
	if (skb->len >= NLMSG_SPACE(0)) {
		nlh = nlmsg_hdr(skb);
		memcpy(str, NLMSG_DATA(nlh), sizeof(str));
		pid = nlh->nlmsg_pid;
		kfree_skb(skb);
	}
}

static inline void netlink_init(void) {
	struct netlink_kernel_cfg netlink_cfg;
	netlink_cfg.groups = 0;
	netlink_cfg.flags = 0;
	netlink_cfg.input = nl_data_ready;
	netlink_cfg.cb_mutex = NULL;
	nl_sk = netlink_kernel_create(&init_net, NETLINK_TEST, &netlink_cfg);
}

static inline void netlink_exit(void) {
	if (!nl_sk)
		return;
	netlink_kernel_release(nl_sk);
	nl_sk = NULL;
}

static inline int gf_parse_dts(struct gf_dev *gf_dev) {
	struct device *dev = &gf_dev->spi->dev;
	gf_dev->reset_gpio =
		of_get_named_gpio(gf_dev->spi->dev.of_node, "fp-gpio-reset", 0);
	devm_gpio_request(dev, gf_dev->reset_gpio, "goodix_reset");
	gpio_direction_output(gf_dev->reset_gpio, 1);
	gf_dev->irq_gpio =
		of_get_named_gpio(gf_dev->spi->dev.of_node, "fp-gpio-irq", 0);
	devm_gpio_request(dev, gf_dev->irq_gpio, "goodix_irq");
	gpio_direction_input(gf_dev->irq_gpio);
	return 0;
}

static inline void gf_cleanup(struct gf_dev *gf_dev) {
	struct device *dev = &gf_dev->spi->dev;
	if (gpio_is_valid(gf_dev->irq_gpio))
		devm_gpio_free(dev, gf_dev->irq_gpio);
	if (gpio_is_valid(gf_dev->reset_gpio))
		devm_gpio_free(dev, gf_dev->reset_gpio);
}

static inline irqreturn_t gf_irq(int irq, void *handle) {
	char msg[2] = {0x0};
	msg[0] = GF_NET_EVENT_IRQ;
	sendnlmsg(msg);
	return IRQ_HANDLED;
}

static inline int irq_setup(struct gf_dev *gf_dev) {
	struct device *dev = &gf_dev->spi->dev;
	int status;
	gf_dev->irq = gpio_to_irq(gf_dev->irq_gpio);
	status =
		devm_request_threaded_irq(dev, gf_dev->irq, NULL, gf_irq,
							 IRQF_TRIGGER_RISING | IRQF_ONESHOT, "gf", gf_dev);
	if (status)
		return status;
	if (!gf_dev->irq_enabled)
		enable_irq_wake(gf_dev->irq);
	gf_dev->irq_enabled = 1;
	return status;
}

static inline void irq_cleanup(struct gf_dev *gf_dev) {
	struct device *dev = &gf_dev->spi->dev;
	if (gf_dev->irq_enabled)
		disable_irq_wake(gf_dev->irq);
	disable_irq(gf_dev->irq);
	devm_free_irq(dev, gf_dev->irq, gf_dev);
	gf_dev->irq_enabled = 0;
}

static inline long gf_ioctl(struct file *filp, unsigned int cmd,
							unsigned long arg) {
	struct gf_dev *gf_dev = &gf;
	int retval = 0;
	u8 netlink_route = NETLINK_TEST;
	switch (cmd) {
	case GF_IOC_INIT:
		if (copy_to_user((void __user *)arg, (void *)&netlink_route,
						 sizeof(u8))) {
			retval = -EFAULT;
			break;
		}
		break;
	case GF_IOC_DISABLE_IRQ:
		if (gf_dev->irq_enabled)
			disable_irq(gf_dev->irq);
		gf_dev->irq_enabled = 0;
		break;
	case GF_IOC_ENABLE_IRQ:
		if (!gf_dev->irq_enabled)
			enable_irq(gf_dev->irq);
		gf_dev->irq_enabled = 1;
		break;
	case GF_IOC_RESET:
		gpio_direction_output(gf_dev->reset_gpio, 1);
		gpio_set_value(gf_dev->reset_gpio, 0);
		mdelay(3);
		gpio_set_value(gf_dev->reset_gpio, 1);
		mdelay(3);
		break;
	default:
		break;
	}
	return retval;
}

static inline int gf_open(struct inode *inode, struct file *filp) {
	struct gf_dev *gf_dev = &gf;
	int status = -ENXIO;
	list_for_each_entry(gf_dev, &device_list, device_entry) {
		if (gf_dev->devt == inode->i_rdev) {
			status = 0;
			break;
		}
	}
	if (status == 0) {
		gf_dev->users++;
		filp->private_data = gf_dev;
		nonseekable_open(inode, filp);
		if (gf_dev->users == 1) {
			status = gf_parse_dts(gf_dev);
			if (status)
				return status;
			status = irq_setup(gf_dev);
			if (status)
				gf_cleanup(gf_dev);
		}
		gpio_direction_output(gf_dev->reset_gpio, 1);
		gpio_set_value(gf_dev->reset_gpio, 0);
		mdelay(3);
		gpio_set_value(gf_dev->reset_gpio, 1);
		mdelay(3);
	}
	return status;
}

static inline int gf_release(struct inode *inode, struct file *filp) {
	struct gf_dev *gf_dev = &gf;
	int status = 0;
	gf_dev = filp->private_data;
	filp->private_data = NULL;
	gf_dev->users--;
	if (!gf_dev->users) {
		irq_cleanup(gf_dev);
		gf_cleanup(gf_dev);
	}
	return status;
}

static const struct file_operations gf_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = gf_ioctl,
	.open = gf_open,
	.release = gf_release,
};

static struct class *gf_class;
static inline int gf_probe(struct platform_device *pdev) {
	struct gf_dev *gf_dev = &gf;
	int status = -EINVAL;
	unsigned long minor;
	INIT_LIST_HEAD(&gf_dev->device_entry);
	gf_dev->spi = pdev;
	gf_dev->irq_gpio = -EINVAL;
	gf_dev->reset_gpio = -EINVAL;
	minor = find_first_zero_bit(minors, N_SPI_MINORS);
	if (minor < N_SPI_MINORS) {
		struct device *dev;
		gf_dev->devt = MKDEV(SPIDEV_MAJOR, minor);
		dev = device_create(gf_class, &gf_dev->spi->dev, gf_dev->devt, gf_dev,
							GF_DEV_NAME);
		status = IS_ERR(dev) ? PTR_ERR(dev) : 0;
	} else {
		status = -ENODEV;
		return status;
	}
	if (status == 0) {
		set_bit(minor, minors);
		list_add(&gf_dev->device_entry, &device_list);
	} else {
		gf_dev->devt = 0;
		return status;
	}
	gf_dev->input = input_allocate_device();
	if (gf_dev->input == NULL) {
		status = -ENOMEM;
		if (gf_dev->input != NULL)
			input_free_device(gf_dev->input);
	}
	gf_dev->input->name = GF_INPUT_NAME;
	status = input_register_device(gf_dev->input);
	if (status) {
		if (gf_dev->devt != 0) {
			list_del(&gf_dev->device_entry);
			device_destroy(gf_class, gf_dev->devt);
			clear_bit(MINOR(gf_dev->devt), minors);
		}
	}
	return status;
}

static inline int gf_remove(struct platform_device *pdev) {
	struct gf_dev *gf_dev = &gf;
	if (gf_dev->input)
		input_unregister_device(gf_dev->input);
	input_free_device(gf_dev->input);
	list_del(&gf_dev->device_entry);
	device_destroy(gf_class, gf_dev->devt);
	clear_bit(MINOR(gf_dev->devt), minors);
	return 0;
}

static const struct of_device_id gx_match_table[] = {
	{.compatible = GF_SPIDEV_NAME},
	{},
};

static struct platform_driver gf_driver = {
	.driver =
		{
			.name = GF_DEV_NAME,
			.owner = THIS_MODULE,
			.of_match_table = gx_match_table,
		},
	.probe = gf_probe,
	.remove = gf_remove,
};

static inline int __init gf_init(void) {
	if(fpsensor != 2)
		return -1;
	int status;
	BUILD_BUG_ON(N_SPI_MINORS > 256);
	status = register_chrdev(SPIDEV_MAJOR, CHRD_DRIVER_NAME, &gf_fops);
	if (status < 0)
		return status;
	SPIDEV_MAJOR = status;
	gf_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(gf_class)) {
		unregister_chrdev(SPIDEV_MAJOR, gf_driver.driver.name);
		return PTR_ERR(gf_class);
	}
	status = platform_driver_register(&gf_driver);
	if (status < 0) {
		class_destroy(gf_class);
		unregister_chrdev(SPIDEV_MAJOR, gf_driver.driver.name);
	}
	netlink_init();
	return 0;
}
module_init(gf_init);

static inline void __exit gf_exit(void) {
	netlink_exit();
	platform_driver_unregister(&gf_driver);
	class_destroy(gf_class);
	unregister_chrdev(SPIDEV_MAJOR, gf_driver.driver.name);
}
module_exit(gf_exit);

MODULE_AUTHOR("Jiangtao Yi, <yijiangtao@goodix.com>");
MODULE_AUTHOR("Jandy Gou, <gouqingsong@goodix.com>");
MODULE_AUTHOR("Jebaitedneko, <jebaitedneko@gmail.com>");
MODULE_DESCRIPTION("goodix fingerprint sensor device driver");
MODULE_LICENSE("GPL");
