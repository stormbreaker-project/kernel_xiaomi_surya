// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2018 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright (C) 2020 Vlad Adumitroaie <celtare21@gmail.com>.
 */

#define pr_fmt(fmt) "userland_worker: " fmt

#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mman.h>
#include <linux/ptrace.h>
#include <linux/userland.h>

extern const unsigned long sys_call_table[];

static bool is_su(const char __user *filename)
{
	static const char su_path[] = "/system/bin/su";
	char ufn[sizeof(su_path)];

	return likely(!copy_from_user(ufn, filename, sizeof(ufn))) &&
	       unlikely(!memcmp(ufn, su_path, sizeof(ufn)));
}

static bool is_stub(const char __user *filename)
{
	static const char stub_path[] = "/system/bin/stub";
	char ufn[sizeof(stub_path)];

	return likely(!copy_from_user(ufn, filename, sizeof(ufn))) &&
	       unlikely(!memcmp(ufn, stub_path, sizeof(ufn)));
}

static void __user *userspace_stack_buffer(const void *d, size_t len)
{
	/* To avoid having to mmap a page in userspace, just write below the stack pointer. */
	char __user *p = (void __user *)current_user_stack_pointer() - len;

	return copy_to_user(p, d, len) ? NULL : p;
}

static char __user *sh_user_path(void)
{
	static const char sh_path[] = "/system/bin/sh";

	return userspace_stack_buffer(sh_path, sizeof(sh_path));
}

asmlinkage int (*old_write)(unsigned int, const char __user *, size_t);

asmlinkage long(*old_newfstatat)(int dfd, const char __user *filename,
			     struct stat *statbuf, int flag);
asmlinkage long new_newfstatat(int dfd, const char __user *filename,
			   struct stat __user *statbuf, int flag)
{
	if (!is_su(filename))
		return old_newfstatat(dfd, filename, statbuf, flag);
	return old_newfstatat(dfd, sh_user_path(), statbuf, flag);
}
asmlinkage long stub_newfstatat(int dfd, const char __user *filename,
			   struct stat __user *statbuf, int flag)
{
	if (likely(!is_stub(filename)))
		return old_newfstatat(dfd, filename, statbuf, flag);
	return old_newfstatat(dfd, sh_user_path(), statbuf, flag);
}


asmlinkage long(*old_faccessat)(int dfd, const char __user *filename, int mode);
asmlinkage long new_faccessat(int dfd, const char __user *filename, int mode)
{
	if (!is_su(filename))
		return old_faccessat(dfd, filename, mode);
	return old_faccessat(dfd, sh_user_path(), mode);
}
asmlinkage long stub_faccessat(int dfd, const char __user *filename, int mode)
{
	if (likely(!is_stub(filename)))
		return old_faccessat(dfd, filename, mode);
	return old_faccessat(dfd, sh_user_path(), mode);
}

asmlinkage long (*old_execve)(const char __user *filename,
			  const char __user *const __user *argv,
			  const char __user *const __user *envp);
asmlinkage long new_execve(const char __user *filename,
		       const char __user *const __user *argv,
		       const char __user *const __user *envp)
{
	static const char now_root[] = "You are now root.\n";
	struct cred *cred;

	if (!is_su(filename))
		return old_execve(filename, argv, envp);

	if (!old_execve(filename, argv, envp))
		return 0;

	/* Rather than the usual commit_creds(prepare_kernel_cred(NULL)) idiom,
	 * we manually zero out the fields in our existing one, so that we
	 * don't have to futz with the task's key ring for disk access.
	 */
	cred = (struct cred *)__task_cred(current);
	memset(&cred->uid, 0, sizeof(cred->uid));
	memset(&cred->gid, 0, sizeof(cred->gid));
	memset(&cred->suid, 0, sizeof(cred->suid));
	memset(&cred->euid, 0, sizeof(cred->euid));
	memset(&cred->egid, 0, sizeof(cred->egid));
	memset(&cred->fsuid, 0, sizeof(cred->fsuid));
	memset(&cred->fsgid, 0, sizeof(cred->fsgid));
	memset(&cred->cap_inheritable, 0xff, sizeof(cred->cap_inheritable));
	memset(&cred->cap_permitted, 0xff, sizeof(cred->cap_permitted));
	memset(&cred->cap_effective, 0xff, sizeof(cred->cap_effective));
	memset(&cred->cap_bset, 0xff, sizeof(cred->cap_bset));
	memset(&cred->cap_ambient, 0xff, sizeof(cred->cap_ambient));

	old_write(2, userspace_stack_buffer(now_root, sizeof(now_root)),
		  sizeof(now_root) - 1);
	return old_execve(sh_user_path(), argv, envp);
}
asmlinkage long stub_execve(const char __user *filename,
		       const char __user *const __user *argv,
		       const char __user *const __user *envp)
{
	static const char now_root[] = "You are now root.\n";

	if (likely(!is_stub(filename)))
		return old_execve(filename, argv, envp);

	if (!old_execve(filename, argv, envp))
		return 0;

	hijack_syscalls();
	set_selinux(0);

	old_write(2, userspace_stack_buffer(now_root, sizeof(now_root)),
		  sizeof(now_root) - 1);
	return old_execve(sh_user_path(), argv, envp);
}

void hijack_syscalls(void)
{
	WRITE_ONCE(*((void **)sys_call_table + __NR_newfstatat), new_newfstatat);
	WRITE_ONCE(*((void **)sys_call_table + __NR_faccessat), new_faccessat);
	WRITE_ONCE(*((void **)sys_call_table + __NR_execve), new_execve);
}

void restore_syscalls(bool set_enforce)
{
	if (set_enforce)
		set_selinux(1);

	WRITE_ONCE(*((void **)sys_call_table + __NR_newfstatat), stub_newfstatat);
	WRITE_ONCE(*((void **)sys_call_table + __NR_faccessat), stub_faccessat);
	WRITE_ONCE(*((void **)sys_call_table + __NR_execve), stub_execve);
}

static int __init superuser_init(void)
{
	old_write = READ_ONCE(*((void **)sys_call_table + __NR_write));
	old_newfstatat = READ_ONCE(*((void **)sys_call_table + __NR_newfstatat));
	old_faccessat = READ_ONCE(*((void **)sys_call_table + __NR_faccessat));
	old_execve = READ_ONCE(*((void **)sys_call_table + __NR_execve));

	WRITE_ONCE(*((void **)sys_call_table + __NR_newfstatat), stub_newfstatat);
	WRITE_ONCE(*((void **)sys_call_table + __NR_faccessat), stub_faccessat);
	WRITE_ONCE(*((void **)sys_call_table + __NR_execve), stub_execve);

	return 0;
}

module_init(superuser_init);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Kernel-assisted superuser for Android");
MODULE_AUTHOR("Jason A. Donenfeld <Jason@zx2c4.com>");
