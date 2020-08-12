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
#include <linux/userland.h>

#include "../security/selinux/include/security.h"
#include "../security/selinux/include/avc_ss_reset.h"

#define MAX_CHAR 128
#define INITIAL_SIZE 4
#define DELAY 500

static struct delayed_work userland_work;

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

	pr_info("Calling userspace!");

	return call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
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
			free_memory(argv, i);
			return NULL;
		}
	}

	return argv;
}

static void userland_worker(struct work_struct *work)
{
	char** argv;
	int ret;
	bool is_enforcing;

	is_enforcing = enforcing_enabled(extern_state);
	if (is_enforcing)
		set_selinux(0);

	argv = alloc_memory(INITIAL_SIZE);
	if (!argv)
		return;

	strcpy(argv[0], "/system/bin/setprop");
	strcpy(argv[1], "pixel.oslo.allowed_override");
	strcpy(argv[2], "1");
	argv[3] = NULL;

	ret = use_userspace(argv);
	if (!ret)
		pr_info("Props set succesfully!");
	else
		pr_err("Couldn't set props! %d", ret);

	free_memory(argv, INITIAL_SIZE);

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

static void userland_worket_exit(void)
{
	return;
}

module_init(userland_worker_entry);
module_exit(userland_worket_exit);
