// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Vlad Adumitroaie <celtare21@gmail.com>.
 */

#define pr_fmt(fmt) "userland_worker: " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/security.h>
#include <linux/namei.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/userland.h>

#define LEN(arr) ((int) (sizeof (arr) / sizeof (arr)[0]))
#define INITIAL_SIZE 4
#define MAX_CHAR 128
#define SHORT_DELAY 10
#define DELAY 500
#define LONG_DELAY 10000

static const char* path_to_files[] = { "/data/user/0/com.kaname.artemiscompanion/files/configs/dns.txt", "/data/user/0/com.kaname.artemiscompanion/files/configs/flash_boot.txt",
					 "/data/user/0/com.kaname.artemiscompanion/files/configs/backup.txt" };

struct values {
	int dns;
	bool flash_boot;
	int backup;
};

static struct delayed_work userland_work;

static const struct file_operations proc_file_fops = {
	.owner = THIS_MODULE,
};

static void free_memory(char** argv, int size)
{
	int i;

	for (i = 0; i < size; i++)
		kfree(argv[i]);
	kfree(argv);
}

static char** alloc_memory(int size)
{
	char** argv;
	int i;

	argv = kmalloc(size * sizeof(char*), GFP_KERNEL);
	if (!argv) {
		pr_err("Couldn't allocate memory!");
		return NULL;
	}

	for (i = 0; i < size; i++) {
		argv[i] = kmalloc(MAX_CHAR * sizeof(char), GFP_KERNEL);
		if (!argv[i]) {
			pr_err("Couldn't allocate memory!");
			kfree(argv);
			return NULL;
		}
	}

	return argv;
}

static int use_userspace(char** argv)
{
	static char* envp[] = {
		"SHELL=/bin/sh",
		"HOME=/",
		"USER=shell",
		"TERM=xterm-256color",
		"PATH=/product/bin:/apex/com.android.runtime/bin:/apex/com.android.art/bin:/system_ext/bin:/system/bin:/system/xbin:/odm/bin:/vendor/bin:/vendor/xbin",
		"DISPLAY=:0",
		NULL
	};

	return call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
}

static struct file *file_open(const char *path, int flags, umode_t rights)
{
	struct file *filp;
	mm_segment_t oldfs;

	oldfs = get_fs();
	set_fs(get_ds());

	filp = filp_open(path, flags, rights);

	if (!S_ISREG(file_inode(filp)->i_mode) || filp->f_pos < 0) {
		filp_close(filp, NULL);
		set_fs(oldfs);
		return NULL;
	}

	set_fs(oldfs);

	if (IS_ERR(filp))
		return NULL;

	return filp;
}

static int read_file_value(const char *path_to_file)
{
	struct file* __file = NULL;
	struct path path;
	char buf[MAX_CHAR];
	int number_value, ret, retries = 0;
	loff_t pos = 0;

	do {
		ret = kern_path(path_to_file, LOOKUP_FOLLOW, &path);
		if (ret)
			msleep(DELAY);
	} while (ret && (retries++ < 10));

	if (ret) {
		pr_err("Couldn't find file %s", path_to_file);
		return -1;
	}

	__file = file_open(path_to_file, O_RDONLY | O_LARGEFILE, 0);
	if (__file == NULL || IS_ERR_OR_NULL(__file->f_path.dentry)) {
		pr_err("Couldn't open file %s", path_to_file);
		return -1;
	}

	memset(buf, 0, sizeof(buf));

	mdelay(SHORT_DELAY);
	ret = kernel_read(__file, buf, MAX_CHAR, &pos);
	mdelay(SHORT_DELAY);
	filp_close(__file, NULL);

	if (ret < 0) {
		pr_err("Couldn't read buffer!");
		return -1;
	}

	pr_info("Parsed file %s with value %s", path_to_file, buf);

	if (kstrtoint(buf, 10, &number_value))
		return -1;

	return number_value;
}

static struct values *alloc_and_populate(void)
{
	struct values* tweaks;
	int size, ret, i;

	tweaks = kmalloc(sizeof(struct values), GFP_KERNEL);
	if (!tweaks) {
		pr_err("Couldn't allocate memory!");
		return NULL;
	}

	tweaks->dns = 0;
	tweaks->flash_boot = 0;
	tweaks->backup = 0;

	size = LEN(path_to_files);
	for (i = 0; i < size; i++) {
		if (path_to_files[i] == NULL)
			continue;

		ret = read_file_value(path_to_files[i]);
		if (ret == -1)
			continue;

		if (strstr(path_to_files[i], "dns")) {
			tweaks->dns = ret;
			pr_info("DNS value: %d", tweaks->dns);
		} else if (strstr(path_to_files[i], "flash_boot")) {
			tweaks->flash_boot = !!ret;
			pr_info("Flash_boot value: %d", tweaks->flash_boot);
		} else if (strstr(path_to_files[i], "backup")) {
			tweaks->backup = ret;
			pr_info("Backup value: %d", tweaks->backup);
		}
	}

	return tweaks;
}

static void fix_TEE(void)
{
	char** argv;
	int ret;

	argv = alloc_memory(INITIAL_SIZE);
	if (!argv) {
		pr_err("Couldn't allocate memory!");
		return;
	}

	msleep(SHORT_DELAY * 2);

	strcpy(argv[0], "/data/local/tmp/resetprop_static");
	strcpy(argv[1], "ro.product.model");
	strcpy(argv[2], "Pixel 4a");
	argv[3] = NULL;

	ret = use_userspace(argv);
	if (!ret)
		pr_info("Device props set succesfully!");
	else
		pr_err("Couldn't set device props! %d", ret);

	strcpy(argv[0], "/data/local/tmp/resetprop_static");
	strcpy(argv[1], "ro.product.system.model");
	strcpy(argv[2], "Pixel 4a");
	argv[3] = NULL;

	ret = use_userspace(argv);
	if (!ret)
		pr_info("Device props set succesfully!");
	else
		pr_err("Couldn't set device props! %d", ret);

	strcpy(argv[0], "/data/local/tmp/resetprop_static");
	strcpy(argv[1], "ro.product.vendor.model");
	strcpy(argv[2], "Pixel 4a");
	argv[3] = NULL;

	ret = use_userspace(argv);
	if (!ret)
		pr_info("Device props set succesfully!");
	else
		pr_err("Couldn't set device props! %d", ret);

	strcpy(argv[0], "/data/local/tmp/resetprop_static");
	strcpy(argv[1], "ro.product.product.model");
	strcpy(argv[2], "Pixel 4a");
	argv[3] = NULL;

	ret = use_userspace(argv);
	if (!ret)
		pr_info("Device props set succesfully!");
	else
		pr_err("Couldn't set device props! %d", ret);

	strcpy(argv[0], "/data/local/tmp/resetprop_static");
	strcpy(argv[1], "ro.product.odm.model");
	strcpy(argv[2], "Pixel 4a");
	argv[3] = NULL;

	ret = use_userspace(argv);
	if (!ret)
		pr_info("Device props set succesfully!");
	else
		pr_err("Couldn't set device props! %d", ret);

	strcpy(argv[0], "/data/local/tmp/resetprop_static");
	strcpy(argv[1], "ro.product.system_ext.model");
	strcpy(argv[2], "Pixel 4a");
	argv[3] = NULL;

	ret = use_userspace(argv);
	if (!ret)
		pr_info("Device props set succesfully!");
	else
		pr_err("Couldn't set device props! %d", ret);

	free_memory(argv, INITIAL_SIZE);
}

static void encrypted_work(void)
{
	char** argv;
	int ret;

	argv = alloc_memory(INITIAL_SIZE);
	if (!argv) {
		pr_err("Couldn't allocate memory!");
		return;
	}

	fix_TEE();

	strcpy(argv[0], "/system/bin/setprop");
	strcpy(argv[1], "pixel.oslo.allowed_override");
	strcpy(argv[2], "1");
	argv[3] = NULL;

	ret = use_userspace(argv);
	if (!ret)
		pr_info("Props set succesfully! Soli is unlocked!");
	else
		pr_err("Couldn't set Soli props! %d", ret);

	strcpy(argv[0], "/system/bin/setprop");
	strcpy(argv[1], "persist.vendor.radio.multisim_swtich_support");
	strcpy(argv[2], "true");
	argv[3] = NULL;

	ret = use_userspace(argv);
	if (!ret)
		pr_info("Props set succesfully! Multisim is unlocked!");
	else
		pr_err("Couldn't set multisim props! %d", ret);

	strcpy(argv[0], "/system/bin/setprop");
	strcpy(argv[1], "dalvik.vm.dex2oat-cpu-set");
	strcpy(argv[2], "0,1,2,3,4,5,6");
	argv[3] = NULL;

	ret = use_userspace(argv);
	if (!ret)
		pr_info("Dalvik props set succesfully!");
	else
		pr_err("Couldn't set Dalvik props! %d", ret);

	strcpy(argv[0], "/system/bin/setprop");
	strcpy(argv[1], "dalvik.vm.dex2oat-threads");
	strcpy(argv[2], "6");
	argv[3] = NULL;

	ret = use_userspace(argv);
	if (!ret)
		pr_info("Dalvik props set succesfully!");
	else
		pr_err("Couldn't set Dalvik props! %d", ret);

	free_memory(argv, INITIAL_SIZE);
}

static void decrypted_work(void)
{
	struct values* tweaks;
	char** argv;
	int ret;

	argv = alloc_memory(INITIAL_SIZE);
	if (!argv) {
		pr_err("Couldn't allocate memory!");
		return;
	}

	if (!is_decrypted) {
		pr_info("Waiting for fs decryption!");
		while (!is_decrypted)
			msleep(DELAY * 2);
		msleep(LONG_DELAY);
		pr_info("Fs decrypted!");
	}

	// Wait for RCU grace period to end for the files to sync
	rcu_barrier();
	msleep(DELAY / 5);

	tweaks = alloc_and_populate();

	if (tweaks && tweaks->flash_boot) {
		strcpy(argv[0], "/system/bin/sh");
		strcpy(argv[1], "-c");
		strcpy(argv[2], "/system/bin/rm /data/user/0/com.kaname.artemiscompanion/files/configs/flash_boot.txt");
		argv[3] = NULL;

		ret = use_userspace(argv);
		if (!ret)
			pr_info("Flash_boot file deleted!");
		else
			pr_err("Couldn't delete Flash_boot file! %d", ret);

		strcpy(argv[0], "/system/bin/test");
		strcpy(argv[1], "-f");
		strcpy(argv[2], "/data/user/0/com.kaname.artemiscompanion/files/boot.img");
		argv[3] = NULL;

	        ret = use_userspace(argv);
		if (!ret) {
			int flash_a, flash_b;

			strcpy(argv[0], "/system/bin/dd");
			strcpy(argv[1], "if=/data/user/0/com.kaname.artemiscompanion/files/boot.img");
			strcpy(argv[2], "of=/dev/block/bootdevice/by-name/boot_b");
			argv[3] = NULL;

			flash_b = use_userspace(argv);
			if (!flash_b)
				pr_info("Boot image _b flashed!");
			else
				pr_err("DD failed! %d", flash_b);

			strcpy(argv[0], "/system/bin/dd");
			strcpy(argv[1], "if=/data/user/0/com.kaname.artemiscompanion/files/boot.img");
			strcpy(argv[2], "of=/dev/block/bootdevice/by-name/boot_a");
			argv[3] = NULL;

			flash_a = use_userspace(argv);
			if (!flash_a)
				pr_info("Boot image _a flashed!");
			else
				pr_err("DD failed! %d", flash_a);

			if (!flash_a || !flash_b) {
				strcpy(argv[0], "/system/bin/sh");
				strcpy(argv[1], "-c");
				strcpy(argv[2], "/system/bin/reboot");
				argv[3] = NULL;

				ret = use_userspace(argv);
				if (!ret) {
					pr_info("Reboot call succesfully! Going down!");
					return;
				} else {
					pr_err("Couldn't reboot! %d", ret);
				}
			}
		} else {
			pr_err("No boot.img found!");
		}
	}

	if (tweaks && tweaks->backup) {
		strcpy(argv[0], "/system/bin/sh");
		strcpy(argv[1], "-c");
		strcpy(argv[2], "/system/bin/mkdir /data/data/com.termux/files/home/.tmp");
		argv[3] = NULL;

		ret = use_userspace(argv);
		if (!ret)
			pr_info("Termux temp folder created!");
		else
			pr_err("Couldn't create termux temp folder! %d", ret);

		strcpy(argv[0], "/system/bin/sh");
		strcpy(argv[1], "-c");
		strcpy(argv[2], "/system/bin/cp /data/user/0/com.kaname.artemiscompanion/files/assets/cbackup.sh /data/local/tmp/cbackup.sh");
		argv[3] = NULL;

		ret = use_userspace(argv);
		if (!ret)
			pr_info("Cbackup copied!");
		else
			pr_err("Couldn't copy cbackup! %d", ret);

		if (tweaks->backup == 1) {
			strcpy(argv[0], "/system/bin/sh");
			strcpy(argv[1], "-c");
			strcpy(argv[2], "/data/data/com.termux/files/usr/bin/bash /data/local/tmp/cbackup.sh");
			argv[3] = NULL;

			ret = use_userspace(argv);
			if (!ret)
				pr_info("Backup done!");
			else
				pr_err("Couldn't finish backup! %d", ret);
		} else if (tweaks->backup == 2) {
			strcpy(argv[0], "/system/bin/sh");
			strcpy(argv[1], "-c");
			strcpy(argv[2], "/data/data/com.termux/files/usr/bin/bash /data/local/tmp/cbackup.sh restore");
			argv[3] = NULL;

			ret = use_userspace(argv);
			if (!ret)
				pr_info("Restore done!");
			else
				pr_err("Couldn't restore backup! %d", ret);
        	}

		if (!ret) {
			strcpy(argv[0], "/system/bin/sh");
			strcpy(argv[1], "-c");
			strcpy(argv[2], "/system/bin/printf 1 > /data/user/0/com.kaname.artemiscompanion/files/configs/status.txt");
			argv[3] = NULL;

			ret = use_userspace(argv);
			if (!ret)
				pr_info("Status file created with success!");
			else
				pr_err("Couldn't create status file! %d", ret);
		} else {
			strcpy(argv[0], "/system/bin/sh");
			strcpy(argv[1], "-c");
			strcpy(argv[2], "/system/bin/printf -1 > /data/user/0/com.kaname.artemiscompanion/files/configs/status.txt");
			argv[3] = NULL;

			ret = use_userspace(argv);
			if (!ret)
				pr_info("Status file created with failure!");
			else
				pr_err("Couldn't create status file! %d", ret);
		}

		strcpy(argv[0], "/system/bin/sh");
		strcpy(argv[1], "-c");
		strcpy(argv[2], "/system/bin/rm /data/user/0/com.kaname.artemiscompanion/files/configs/backup.txt");
		argv[3] = NULL;

		ret = use_userspace(argv);
		if (!ret)
			pr_info("Backup file removed!");
		else
			pr_err("Couldn't remove backup file! %d", ret);

		strcpy(argv[0], "/system/bin/sh");
		strcpy(argv[1], "-c");
		strcpy(argv[2], "/system/bin/rm /data/user/0/com.kaname.artemiscompanion/files/configs/pass.txt");
		argv[3] = NULL;

		ret = use_userspace(argv);
		if (!ret)
			pr_info("Pass file removed!");
		else
			pr_err("Couldn't remove backup file! %d", ret);

		strcpy(argv[0], "/system/bin/sh");
		strcpy(argv[1], "-c");
		strcpy(argv[2], "/system/bin/rm /data/local/tmp/cbackup.sh");
		argv[3] = NULL;

		if (!ret)
			pr_info("Tmp file deleted!");
		else
			pr_err("Couldn't download tmp file! %d", ret);
	}

	strcpy(argv[0], "/system/bin/sh");
	strcpy(argv[1], "-c");
	strcpy(argv[2], "/system/bin/cp /data/user/0/com.kaname.artemiscompanion/files/assets/resetprop /data/local/tmp/resetprop_static");
	argv[3] = NULL;

	ret = use_userspace(argv);
	if (!ret)
		pr_info("Resetprop copied succesfully!");
	else
		pr_err("Couldn't copy Resetprop! %d", ret);

	strcpy(argv[0], "/system/bin/chmod");
	strcpy(argv[1], "755");
	strcpy(argv[2], "/data/local/tmp/resetprop_static");
	argv[3] = NULL;

	ret = use_userspace(argv);
	if (!ret)
		pr_info("Chmod called succesfully!");
	else
		pr_err("Couldn't call Chmod! %d", ret);

	if (tweaks && tweaks->dns) {
		switch (tweaks->dns)
		{
			case 1:
				strcpy(argv[0], "/system/bin/sh");
				strcpy(argv[1], "-c");
				strcpy(argv[2], "/system/bin/iptables -t nat -A OUTPUT -p tcp --dport 53 -j DNAT --to-destination 176.103.130.130");
				argv[3] = NULL;

				ret = use_userspace(argv);
				if (!ret)
					pr_info("Iptables called succesfully!");
				else
					pr_err("Couldn't call iptables! %d", ret);

				strcpy(argv[0], "/system/bin/sh");
				strcpy(argv[1], "-c");
				strcpy(argv[2], "/system/bin/iptables -t nat -A OUTPUT -p udp --dport 53 -j DNAT --to-destination 176.103.130.130");
				argv[3] = NULL;

				ret = use_userspace(argv);
				if (!ret)
					pr_info("Iptables called succesfully!");
				else
					pr_err("Couldn't call iptables! %d", ret);

				strcpy(argv[0], "/system/bin/sh");
				strcpy(argv[1], "-c");
				strcpy(argv[2], "/system/bin/iptables -t nat -D OUTPUT -p tcp --dport 53 -j DNAT --to-destination 176.103.130.130 || true");
				argv[3] = NULL;

				ret = use_userspace(argv);
				if (!ret)
					pr_info("Iptables called succesfully!");
				else
					pr_err("Couldn't call iptables! %d", ret);

				strcpy(argv[0], "/system/bin/sh");
				strcpy(argv[1], "-c");
				strcpy(argv[2], "/system/bin/iptables -t nat -D OUTPUT -p udp --dport 53 -j DNAT --to-destination 176.103.130.130 || true");
				argv[3] = NULL;

				ret = use_userspace(argv);
				if (!ret)
					pr_info("Iptables called succesfully!");
				else
					pr_err("Couldn't call iptables! %d", ret);

				strcpy(argv[0], "/system/bin/sh");
				strcpy(argv[1], "-c");
				strcpy(argv[2], "/system/bin/iptables -t nat -I OUTPUT -p tcp --dport 53 -j DNAT --to-destination 176.103.130.130");
				argv[3] = NULL;

				ret = use_userspace(argv);
				if (!ret)
					pr_info("Iptables called succesfully!");
				else
					pr_err("Couldn't call iptables! %d", ret);

				strcpy(argv[0], "/system/bin/sh");
				strcpy(argv[1], "-c");
				strcpy(argv[2], "/system/bin/iptables -t nat -I OUTPUT -p udp --dport 53 -j DNAT --to-destination 176.103.130.130");
				argv[3] = NULL;

				ret = use_userspace(argv);
				if (!ret)
					pr_info("Iptables called succesfully!");
				else
					pr_err("Couldn't call iptables! %d", ret);

				break;
			case 2:
				strcpy(argv[0], "/system/bin/sh");
				strcpy(argv[1], "-c");
				strcpy(argv[2], "/system/bin/iptables -t nat -A OUTPUT -p tcp --dport 53 -j DNAT --to-destination 1.1.1.1");
				argv[3] = NULL;

				ret = use_userspace(argv);
				if (!ret)
					pr_info("Iptables called succesfully!");
				else
					pr_err("Couldn't call iptables! %d", ret);

				strcpy(argv[0], "/system/bin/sh");
				strcpy(argv[1], "-c");
				strcpy(argv[2], "/system/bin/iptables -t nat -A OUTPUT -p udp --dport 53 -j DNAT --to-destination 1.1.1.1");
				argv[3] = NULL;

				ret = use_userspace(argv);
				if (!ret)
					pr_info("Iptables called succesfully!");
				else
					pr_err("Couldn't call iptables! %d", ret);

				strcpy(argv[0], "/system/bin/sh");
				strcpy(argv[1], "-c");
				strcpy(argv[2], "/system/bin/iptables -t nat -D OUTPUT -p tcp --dport 53 -j DNAT --to-destination 1.1.1.1 || true");
				argv[3] = NULL;

				ret = use_userspace(argv);
				if (!ret)
					pr_info("Iptables called succesfully!");
				else
					pr_err("Couldn't call iptables! %d", ret);

				strcpy(argv[0], "/system/bin/sh");
				strcpy(argv[1], "-c");
				strcpy(argv[2], "/system/bin/iptables -t nat -D OUTPUT -p udp --dport 53 -j DNAT --to-destination 1.1.1.1 || true");
				argv[3] = NULL;

				ret = use_userspace(argv);
				if (!ret)
					pr_info("Iptables called succesfully!");
				else
					pr_err("Couldn't call iptables! %d", ret);

				strcpy(argv[0], "/system/bin/sh");
				strcpy(argv[1], "-c");
				strcpy(argv[2], "/system/bin/iptables -t nat -I OUTPUT -p tcp --dport 53 -j DNAT --to-destination 1.1.1.1");
				argv[3] = NULL;

				ret = use_userspace(argv);
				if (!ret)
					pr_info("Iptables called succesfully!");
				else
					pr_err("Couldn't call iptables! %d", ret);

				strcpy(argv[0], "/system/bin/sh");
				strcpy(argv[1], "-c");
				strcpy(argv[2], "/system/bin/iptables -t nat -I OUTPUT -p udp --dport 53 -j DNAT --to-destination 1.1.1.1");
				argv[3] = NULL;

				ret = use_userspace(argv);
				if (!ret)
					pr_info("Iptables called succesfully!");
				else
					pr_err("Couldn't call iptables! %d", ret);

				break;
			default:
				break;
		}
	}

	free_memory(argv, INITIAL_SIZE);
}

static void userland_worker(struct work_struct *work)
{
	struct proc_dir_entry *userland_dir;
	bool is_enforcing;

	is_enforcing = get_enforce_value();
	if (is_enforcing) {
		pr_info("Going permissive");
		set_selinux(0);
	}

	encrypted_work();
	decrypted_work();

	userland_dir = proc_mkdir_data("userland", 0777, NULL, NULL);
	if (userland_dir == NULL)
		pr_err("Couldn't create proc dir!");
	else
		pr_info("Proc dir created successfully!");

	if (is_enforcing) {
		pr_info("Going enforcing");
		set_selinux(1);
	}
}

static int __init userland_worker_entry(void)
{
	INIT_DELAYED_WORK(&userland_work, userland_worker);
	queue_delayed_work(system_power_efficient_wq,
			&userland_work, DELAY);

	return 0;
}

module_init(userland_worker_entry);
