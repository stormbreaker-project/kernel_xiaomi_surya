// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Vlad Adumitroaie <celtare21@gmail.com>.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/userland.h>

#define MAX_DEV 1
#define NAME "userland_listener"
#define DATA_LEN 2

static int userland_open(struct inode *inode, struct file *file);
static int userland_release(struct inode *inode, struct file *file);
static ssize_t userland_read(struct file *file, char __user *buf, size_t count, loff_t *offset);
static ssize_t userland_write(struct file *file, const char __user *buf, size_t count, loff_t *offset);

static const struct file_operations userland_fops = {
	.owner      =	THIS_MODULE,
	.open       =	userland_open,
	.release    =	userland_release,
	.read       =	userland_read,
	.write      =	userland_write
};

struct userland_device_data {
	struct cdev cdev;
};

static int dev_major = 0;
static struct class *userland_class = NULL;
static struct userland_device_data userland_data[MAX_DEV];

static int userland_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	add_uevent_var(env, "DEVMODE=%#o", 0666);

	return 0;
}

static int __init userland_init(void)
{
	dev_t dev;

	if (alloc_chrdev_region(&dev, 0, MAX_DEV, NAME))
		return 0;

	dev_major = MAJOR(dev);

	userland_class = class_create(THIS_MODULE, NAME);
	userland_class->dev_uevent = userland_uevent;

        cdev_init(&userland_data[0].cdev, &userland_fops);
        userland_data[0].cdev.owner = THIS_MODULE;

        cdev_add(&userland_data[0].cdev, MKDEV(dev_major, 0), 1);

        device_create(userland_class, NULL, MKDEV(dev_major, 0), NULL, "userland_listener-%d", 0);

	return 0;
}

static ssize_t userland_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	return count;
}

static int userland_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int userland_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t userland_write(struct file *file, const char __user *buf, size_t count, loff_t *offset)
{
	size_t len = DATA_LEN;
	uint8_t databuf[DATA_LEN];
	int value;

	if (count < DATA_LEN)
		len = count;

	if (copy_from_user(databuf, buf, len))
		return -EFAULT;

	databuf[1] = '\0';

	if (kstrtoint(databuf, DATA_LEN, &value))
		return count;

	switch (value)
	{
		case 1:
			pr_info("Disabling root.");
			restore_syscalls();
			set_selinux(1);
			break;
	}

	return count;
}

module_init(userland_init);
