// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright (C) 2018-2019 Sultan Alsawaf <sultan@kerneltoast.com>.
 */

#include <linux/kernel.h>
#include <linux/libfdt.h>
#include <linux/mm.h>
#include <asm/boot.h>

extern u8 *const dtb_embedded[];

static void __init copy_dt_node_props(void *dt_dst, int dst_node,
				      const void *dt_src, int src_node)
{
	int prop;

	for (prop = fdt_first_property_offset(dt_src, src_node);
	     prop >= 0;
	     prop = fdt_next_property_offset(dt_src, prop)) {
		const char *prop_name;
		const void *prop_val;
		int prop_len;

		prop_val = fdt_getprop_by_offset(dt_src, prop,
						 &prop_name, &prop_len);
		fdt_setprop(dt_dst, dst_node, prop_name, prop_val, prop_len);
	}
}

static void __init __copy_entire_dt_node(void *dt_dst, const void *dt_src,
					 const char *path)
{
	int dst_node, src_node, node;

	dst_node = fdt_path_offset(dt_dst, path);
	src_node = fdt_path_offset(dt_src, path);

	/* Copy the current level of properties */
	copy_dt_node_props(dt_dst, dst_node, dt_src, src_node);

	for (node = fdt_first_subnode(dt_src, src_node);
	     node >= 0;
	     node = fdt_next_subnode(dt_src, node)) {
		const char *name = fdt_get_name(dt_src, node, NULL);
		char full_path[SZ_1K];

		/* Create the subnode and copy over its properties */
		fdt_add_subnode(dt_dst, dst_node, name);

		/* Get the full path of the subnode and recurse */
		snprintf(full_path, sizeof(full_path), "%s/%s", path, name);
		__copy_entire_dt_node(dt_dst, dt_src, full_path);
	}
}

static void __init copy_dt_root_subnode(void *dt_dst, const void *dt_src,
					const char *path)
{
	/*
	 * Clear out the destination node first to so nothing remains from it
	 * when the source node and its subnodes are copied over.
	 */
	fdt_del_node(dt_dst, fdt_path_offset(dt_dst, path));
	fdt_add_subnode(dt_dst, fdt_path_offset(dt_dst, "/"), path + 1);

	__copy_entire_dt_node(dt_dst, dt_src, path);
}

static void __init fix_embedded_dt(void *bl_dt, const void *kern_dt)
{
	static u8 kern_dt_new[MAX_FDT_SIZE] __initdata;
	int ret;

	ret = fdt_open_into(kern_dt, kern_dt_new, sizeof(kern_dt_new));
	if (ret) {
		pr_crit("Error: loading kernel dt failed, ret=%d", ret);
		return;
	}

	/*
	 * The DT modifications are considered to be error-free since both the
	 * kernel and bootloader DTs are valid.
	 */
	copy_dt_root_subnode(kern_dt_new, bl_dt, "/chosen");
	copy_dt_root_subnode(kern_dt_new, bl_dt, "/firmware");
	copy_dt_root_subnode(kern_dt_new, bl_dt, "/memory");

	ret = fdt_pack(kern_dt_new);
	if (ret) {
		pr_crit("Error: failed to pack kernel dt, ret=%d", ret);
		return;
	}

	pr_info("Using embedded DTB instead of bootloader DTB");
	memcpy(bl_dt, kern_dt_new, fdt_totalsize(kern_dt_new));
}

void __init select_embedded_dt(void *bl_dt)
{
	const char *const board_id_prop = "qcom,board-id";
	const char *const msm_id_prop = "qcom,msm-id";
	u64 real_board_id, real_msm_id;
	const u64 *prop_val;
	int i, root_node;

	if (!bl_dt)
		return;

	root_node = fdt_path_offset(bl_dt, "/");
	prop_val = fdt_getprop(bl_dt, root_node, board_id_prop, NULL);
	if (!prop_val) {
		pr_crit("Error: %s missing", board_id_prop);
		return;
	}
	real_board_id = *prop_val;

	prop_val = fdt_getprop(bl_dt, root_node, msm_id_prop, NULL);
	if (!prop_val) {
		pr_crit("Error: %s missing", msm_id_prop);
		return;;
	}
	real_msm_id = *prop_val;

	for (i = 0; dtb_embedded[i]; i++) {
		void *curr_dt = dtb_embedded[i];

		root_node = fdt_path_offset(curr_dt, "/");
		prop_val = fdt_getprop(curr_dt, root_node, board_id_prop, NULL);
		if (!prop_val || *prop_val != real_board_id)
			continue;

		prop_val = fdt_getprop(curr_dt, root_node, msm_id_prop, NULL);
		if (!prop_val || *prop_val != real_msm_id)
			continue;

		fix_embedded_dt(bl_dt, curr_dt);
		break;
	}
}
