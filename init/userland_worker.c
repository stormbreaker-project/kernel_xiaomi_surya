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

#include "../security/selinux/include/security.h"
#include "../security/selinux/include/avc_ss_reset.h"

#define LEN(arr) ((int) (sizeof (arr) / sizeof (arr)[0]))
#define INITIAL_SIZE 4
#define MAX_CHAR 128
#define DELAY 500

static const char* path_to_files[] = { "/sdcard/Artemis/configs/dns.txt", "/sdcard/Artemis/configs/flash_boot.txt" };

struct values {
	int dns;
	bool flash_boot;
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

static struct values *alloc_and_populate(void)
{
	struct file* __file = NULL;
	struct values* tweaks;
	struct path path;
	char buf[MAX_CHAR];
	int number_value, retries, size, ret, i;
	loff_t pos;

	tweaks = kmalloc(sizeof(struct values), GFP_KERNEL);
	if (!tweaks) {
		pr_err("Couldn't allocate memory!");
		return NULL;
	}

	tweaks->dns = 0;
	tweaks->flash_boot = 0;

	size = LEN(path_to_files);
	for (i = 0; i < size; i++) {
		if (path_to_files[i] == NULL)
			continue;

		retries = 0;

                do {
                        ret = kern_path(path_to_files[i], LOOKUP_FOLLOW, &path);
                        if (ret)
                                msleep(DELAY);
                } while (ret && (retries++ < 10));

		if (ret) {
			pr_err("Couldn't find file %s", path_to_files[i]);
			continue;
		}

		__file = file_open(path_to_files[i], O_RDONLY | O_LARGEFILE, 0);
		if (__file == NULL || IS_ERR_OR_NULL(__file->f_path.dentry)) {
			pr_err("Couldn't open file %s", path_to_files[i]);
			continue;
		}

		memset(buf, 0, sizeof(buf));
		pos = 0;

		mdelay(10);
		ret = kernel_read(__file, buf, MAX_CHAR, &pos);
		mdelay(10);
		filp_close(__file, NULL);

		if (ret < 0) {
			pr_err("Couldn't read buffer!");
			continue;
		}

		pr_info("Parsed file %s with value %s", path_to_files[i], buf);

		if (kstrtoint(buf, 10, &number_value))
			continue;

		if (strstr(path_to_files[i], "dns")) {
			tweaks->dns = number_value;
			pr_info("DNS value: %d", tweaks->dns);
		} else if (strstr(path_to_files[i], "flash_boot")) {
			tweaks->flash_boot = !!number_value;
			pr_info("flash_boot value: %d", tweaks->flash_boot);
		}
	}

	return tweaks;
}

static void call_sh(const char* command)
{
	char** argv;
	int ret;

	argv = alloc_memory(INITIAL_SIZE);
	if (!argv) {
		pr_err("Couldn't allocate memory!");
		return;
	}

	strcpy(argv[0], "/system/bin/sh");
	strcpy(argv[1], "-c");
	strcpy(argv[2], command);
	argv[3] = NULL;

	ret = use_userspace(argv);
	if (!ret)
		pr_info("Sh called successfully!");
	else
		pr_err("Couldn't call sh! %d", ret);

	free_memory(argv, INITIAL_SIZE);
}

static void create_dirs(void)
{
	char** argv;
	int ret;

	argv = alloc_memory(INITIAL_SIZE - 1);
	if (!argv) {
		pr_err("Couldn't allocate memory!");
		return;
	}

	strcpy(argv[0], "/system/bin/mkdir");
	strcpy(argv[1], "/sdcard/Artemis");
	argv[2] = NULL;

	ret = use_userspace(argv);
	if (!ret)
		pr_info("Artemis folder doesn't exist! Creating.");
	else
		pr_info("Artemis folder already created");

	strcpy(argv[0], "/system/bin/mkdir");
	strcpy(argv[1], "/sdcard/Artemis/configs");
	argv[2] = NULL;

	ret = use_userspace(argv);
	if (!ret)
		pr_info("Configs folder doesn't exist! Creating.");
	else
		pr_info("Configs folder already created");

	strcpy(argv[0], "/system/bin/ls");
	strcpy(argv[1], "/sdcard/Artemis/configs/dns.txt");
	argv[2] = NULL;

	ret = use_userspace(argv);
	if (ret) {
		strcpy(argv[0], "/system/bin/touch");
		strcpy(argv[1], "/sdcard/Artemis/configs/dns.txt");
		argv[2] = NULL;

		ret = use_userspace(argv);
		if (!ret) {
			pr_info("DNS file created!");

			call_sh("/system/bin/printf 0 > /sdcard/Artemis/configs/dns.txt");
		} else {
			pr_err("Couldn't create dns file! %d", ret);
		}
	} else {
		pr_info("Dns file exists!");
	}

	strcpy(argv[0], "/system/bin/ls");
	strcpy(argv[1], "/sdcard/Artemis/configs/flash_boot.txt");
	argv[2] = NULL;

	ret = use_userspace(argv);
	if (ret) {
		strcpy(argv[0], "/system/bin/touch");
		strcpy(argv[1], "/sdcard/Artemis/configs/flash_boot.txt");
		argv[2] = NULL;

		ret = use_userspace(argv);
		if (!ret) {
			pr_info("flash_boot file created!");

			call_sh("/system/bin/printf 0 > /sdcard/Artemis/configs/flash_boot.txt");
		} else {
			pr_err("Couldn't create flash_boot file! %d", ret);
		}
	} else {
		pr_info("flash_boot file exists!");
	}

	free_memory(argv, INITIAL_SIZE - 1);

	// Wait for RCU grace period to end for the files to sync
	rcu_barrier();
	msleep(100);
}

static inline void set_selinux(int value)
{
	pr_info("Setting selinux state: %d", value);

	enforcing_set(extern_state, value);
	if (value)
		avc_ss_reset(extern_state->avc, 0);
	selnl_notify_setenforce(value);
	selinux_status_update_setenforce(extern_state, value);
	if (!value)
		call_lsm_notifier(LSM_POLICY_CHANGE, NULL);
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
			msleep(1000);
		msleep(10000);
		pr_info("Fs decrypted!");
	}

	create_dirs();
	tweaks = alloc_and_populate();

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

	if (tweaks && tweaks->flash_boot) {
		strcpy(argv[0], "/system/bin/sh");
		strcpy(argv[1], "-c");
		strcpy(argv[2], "/system/bin/rm /sdcard/Artemis/configs/flash_boot.txt");
		argv[3] = NULL;

		ret = use_userspace(argv);
		if (!ret)
			pr_info("Flash_boot file deleted!");
		else
			pr_err("Couldn't delete Flash_boot file! %d", ret);

		strcpy(argv[0], "/system/bin/test");
		strcpy(argv[1], "-f");
		strcpy(argv[2], "/sdcard/Artemis/boot.img");
		argv[3] = NULL;

	        ret = use_userspace(argv);
		if (!ret) {
			int flash_a, flash_b;

			strcpy(argv[0], "/system/bin/dd");
			strcpy(argv[1], "if=/sdcard/Artemis/boot.img");
			strcpy(argv[2], "of=/dev/block/bootdevice/by-name/boot_b");
			argv[3] = NULL;

			flash_b = use_userspace(argv);
			if (!flash_b)
				pr_info("Boot image _b flashed!");
			else
				pr_err("DD failed! %d", flash_b);

			strcpy(argv[0], "/system/bin/dd");
			strcpy(argv[1], "if=/sdcard/Artemis/boot.img");
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
				if (!ret)
					pr_info("Reboot call succesfully! Going down!");
				else
					pr_err("Couldn't reboot! %d", ret);
			}
		} else {
			pr_err("No boot.img found!");
		}

		call_sh("/system/bin/printf 0 > /sdcard/Artemis/configs/flash_boot.txt");
	}


	strcpy(argv[0], "/system/bin/cp");
	strcpy(argv[1], "/storage/emulated/0/resetprop_static");
	strcpy(argv[2], "/data/local/tmp/resetprop_static");
	argv[3] = NULL;

	ret = use_userspace(argv);
	if (!ret) {
		pr_info("Copy called succesfully!");

		strcpy(argv[0], "/system/bin/chmod");
		strcpy(argv[1], "755");
		strcpy(argv[2], "/data/local/tmp/resetprop_static");
		argv[3] = NULL;

		ret = use_userspace(argv);
		if (!ret)
			pr_info("Chmod called succesfully!");
		else
			pr_err("Couldn't call chmod! Exiting %d", ret);
	} else {
		pr_err("Couldn't copy file! %d", ret);
	}

	free_memory(argv, INITIAL_SIZE);
}

static void userland_worker(struct work_struct *work)
{
	struct proc_dir_entry *userland_dir;
	bool is_enforcing = false;

	if (extern_state)
		is_enforcing = enforcing_enabled(extern_state);
	if (is_enforcing)
		set_selinux(0);

	encrypted_work();
	decrypted_work();

	userland_dir = proc_mkdir_data("userland", 0777, NULL, NULL);
	if (userland_dir == NULL)
		pr_err("Couldn't create proc dir!");
	else
		pr_info("Proc dir created successfully!");

	if (is_enforcing)
		set_selinux(1);
}

static int __init userland_worker_entry(void)
{
	INIT_DELAYED_WORK(&userland_work, userland_worker);
	queue_delayed_work(system_power_efficient_wq,
			&userland_work, DELAY);

	return 0;
}

module_init(userland_worker_entry);
