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
#include <linux/uaccess.h>

#define LEN(arr) ((int) (sizeof (arr) / sizeof (arr)[0]))
#define STANDARD_SIZE 4
#define MAX_CHAR 128
#define SHORT_DELAY 10
#define DELAY 500
#define LONG_DELAY 10000

static char** argv;

static bool is_su;

static const char* path_to_files[] = { "/data/user/0/com.kaname.artemiscompanion/files/configs/dns.txt", "/data/user/0/com.kaname.artemiscompanion/files/configs/flash_boot.txt",
					"/data/user/0/com.kaname.artemiscompanion/files/configs/backup.txt", "/data/user/0/com.kaname.artemiscompanion/files/configs/blur_enable.txt"
					};

struct values {
	int dns;
	bool flash_boot;
	int backup;
	bool blur;
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

	if (kstrtoint(buf, 10, &number_value))
		return -1;

	if (number_value < 0 || number_value > 2)
		return -1;

        pr_info("Parsed file %s with value %d", path_to_file, number_value);

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
	tweaks->blur = 0;

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
		} else if (strstr(path_to_files[i], "blur_enable")) {
			tweaks->blur = !!ret;
			pr_info("Blur value: %d", tweaks->blur);
		}
	}

	return tweaks;
}

static inline int linux_write(const char* prop, const char* value, bool resetprop)
{
	int ret;

	strcpy(argv[0], resetprop ? "/data/local/tmp/resetprop_static" : "/system/bin/setprop");
	strcpy(argv[1], prop);
	strcpy(argv[2], value);
	argv[3] = NULL;

	ret = use_userspace(argv);
	if (!ret)
		pr_info("%s set succesfully!", prop);
	else
		pr_err("Couldn't set %s! %d", prop, ret);

	return ret;
}

static inline int linux_sh(const char* command)
{
	int ret;

	strcpy(argv[0], "/system/bin/sh");
	strcpy(argv[1], "-c");
	strcpy(argv[2], command);
	argv[3] = NULL;

	ret = use_userspace(argv);
	if (!ret)
		pr_info("%s called succesfully!", command);
	else
		pr_err("Couldn't call %s! %d", command, ret);

	return ret;
}

static inline int linux_test(const char* path)
{
	strcpy(argv[0], "/system/bin/test");
	strcpy(argv[1], "-f");
	strcpy(argv[2], path);
	argv[3] = NULL;

	return use_userspace(argv);
}

static inline int linux_dd(const char* source, const char* destination)
{
	strcpy(argv[0], "/system/bin/dd");
	strcpy(argv[1], source);
	strcpy(argv[2], destination);
	argv[3] = NULL;

	return use_userspace(argv);
}

static inline int linux_chmod(const char* path, const char* perms)
{
	strcpy(argv[0], "/system/bin/chmod");
	strcpy(argv[1], perms);
	strcpy(argv[2], path);
	argv[3] = NULL;

	return use_userspace(argv);
}

static inline void set_tee(void)
{
	int ret, retries = 0;

	do {
		ret = linux_write("ro.build.fingerprint",
			"google/coral/coral:11/RQ1A.210205.004/7038034:user/release-keys",
			true);
		if (ret)
			msleep(DELAY);
	} while (ret && retries++ < 10);

	linux_write("ro.odm.build.fingerprint",
		"google/coral/coral:S/SPP1.210122.020.A3/7145137:user/release-keys",
		true);

	linux_write("ro.product.build.fingerprint",
		"google/coral/coral:S/SPP1.210122.020.A3/7145137:user/release-keys",
		true);

	linux_write("ro.system.build.fingerprint",
		"google/coral/coral:S/SPP1.210122.020.A3/7145137:user/release-keys",
		true);

	linux_write("ro.system_ext.build.fingerprint",
		"google/coral/coral:S/SPP1.210122.020.A3/7145137:user/release-keys",
		true);

	linux_write("ro.vendor.build.fingerprint",
		"google/coral/coral:S/SPP1.210122.020.A3/7145137:user/release-keys",
		true);

	linux_write("ro.vendor_dlkm.build.fingerprint",
		"google/coral/coral:S/SPP1.210122.020.A3/7145137:user/release-keys",
		true);

	linux_write("ro.bootimage.build.fingerprint",
		"google/coral/coral:S/SPP1.210122.020.A3/7145137:user/release-keys",
		true);
}

static void encrypted_work(void)
{
	if (!linux_sh("/system/bin/su"))
		is_su = true;

	set_tee();

	linux_write("debug.hwui.renderer", "skiavk", false);

	linux_write("pixel.oslo.allowed_override", "1", false);

	linux_write("persist.vendor.radio.multisim_swtich_support", "true", false);

	linux_write("ro.input.video_enabled", "false", true);
}

static void decrypted_work(void)
{
	struct values* tweaks;
	int ret;

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
	if (!tweaks)
		goto skip;

	if (tweaks->flash_boot) {
		linux_sh("/system/bin/printf 0 > /data/user/0/com.kaname.artemiscompanion/files/configs/flash_boot.txt");

		ret = linux_test("/data/user/0/com.kaname.artemiscompanion/files/boot.img");
		if (!ret) {
			int flash_a, flash_b;

			flash_a = linux_dd("if=/data/user/0/com.kaname.artemiscompanion/files/boot.img", "of=/dev/block/bootdevice/by-name/boot_a");
			flash_b = linux_dd("if=/data/user/0/com.kaname.artemiscompanion/files/boot.img", "of=/dev/block/bootdevice/by-name/boot_b");

			if (!flash_a || !flash_b) {
				ret = linux_sh("/system/bin/reboot");

				if (!ret)
					return;
			}
		}
	}


	linux_sh("/system/bin/cp /data/user/0/com.kaname.artemiscompanion/files/assets/resetprop /data/local/tmp/resetprop_static");

	linux_chmod("/data/local/tmp/resetprop_static", "755");

	if (tweaks->backup) {
		if (!is_su)
			hijack_syscalls();

		linux_sh("/system/bin/mkdir /data/data/com.termux/files/home/.tmp");

		linux_sh("/system/bin/cp /data/user/0/com.kaname.artemiscompanion/files/assets/cbackup.sh /data/local/tmp/cbackup.sh");

		switch (tweaks->backup)
		{
			case 1:
				ret = linux_sh("/data/data/com.termux/files/usr/bin/bash /data/local/tmp/cbackup.sh");
				break;
			case 2:
				ret = linux_sh("/data/data/com.termux/files/usr/bin/bash /data/local/tmp/cbackup.sh restore");
				break;
			default:
				ret = -1;
				break;
		}

		ret ? linux_sh("/system/bin/printf -1 > /data/user/0/com.kaname.artemiscompanion/files/configs/status.txt") :
				linux_sh("/system/bin/printf 1 > /data/user/0/com.kaname.artemiscompanion/files/configs/status.txt");

		linux_sh("/system/bin/printf 0 > /data/user/0/com.kaname.artemiscompanion/files/configs/backup.txt");

		linux_sh("/system/bin/rm /data/user/0/com.kaname.artemiscompanion/files/configs/pass.txt");

		linux_sh("/system/bin/rm /data/local/tmp/cbackup.sh");

		if (!is_su)
			restore_syscalls(false);
	}

	if (tweaks->blur) {
		linux_write("ro.surface_flinger.supports_background_blur", "1", true);
		linux_write("ro.sf.blurs_are_expensive", "1", true);
		linux_sh("/system/bin/pkill -TERM -f surfaceflinger");
		msleep(LONG_DELAY);
        }

	switch (tweaks->dns)
	{
		case 1:
			linux_sh("/system/bin/iptables -t nat -A OUTPUT -p tcp --dport 53 -j DNAT --to-destination 176.103.130.130");
			linux_sh("/system/bin/iptables -t nat -A OUTPUT -p udp --dport 53 -j DNAT --to-destination 176.103.130.130");
			linux_sh("/system/bin/iptables -t nat -D OUTPUT -p tcp --dport 53 -j DNAT --to-destination 176.103.130.130 || true");
			linux_sh("/system/bin/iptables -t nat -D OUTPUT -p udp --dport 53 -j DNAT --to-destination 176.103.130.130 || true");
			linux_sh("/system/bin/iptables -t nat -I OUTPUT -p tcp --dport 53 -j DNAT --to-destination 176.103.130.130");
			linux_sh("/system/bin/iptables -t nat -I OUTPUT -p udp --dport 53 -j DNAT --to-destination 176.103.130.130");

			break;
		case 2:
			linux_sh("/system/bin/iptables -t nat -A OUTPUT -p tcp --dport 53 -j DNAT --to-destination 1.1.1.1");
			linux_sh("/system/bin/iptables -t nat -A OUTPUT -p udp --dport 53 -j DNAT --to-destination 1.1.1.1");
			linux_sh("/system/bin/iptables -t nat -D OUTPUT -p tcp --dport 53 -j DNAT --to-destination 1.1.1.1 || true");
			linux_sh("/system/bin/iptables -t nat -D OUTPUT -p udp --dport 53 -j DNAT --to-destination 1.1.1.1 || true");
			linux_sh("/system/bin/iptables -t nat -I OUTPUT -p tcp --dport 53 -j DNAT --to-destination 1.1.1.1");
			linux_sh("/system/bin/iptables -t nat -I OUTPUT -p udp --dport 53 -j DNAT --to-destination 1.1.1.1");

			break;
		default:
			break;
	}

	kfree(tweaks);

skip:
	linux_sh("/system/bin/stop vendor.input.classifier-1-0");
	linux_sh("/system/bin/stop statsd");
}

static void userland_worker(struct work_struct *work)
{
	struct proc_dir_entry *userland_dir;
	bool is_enforcing;

	argv = alloc_memory(STANDARD_SIZE);
	if (!argv) {
		pr_err("Couldn't allocate memory!");
		return;
	}

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

	if (is_enforcing && !is_su) {
		pr_info("Going enforcing");
		set_selinux(1);
	}

	free_memory(argv, STANDARD_SIZE);
}

static int __init userland_worker_entry(void)
{
	INIT_DELAYED_WORK(&userland_work, userland_worker);
	queue_delayed_work(system_power_efficient_wq,
			&userland_work, DELAY);

	return 0;
}

module_init(userland_worker_entry);
