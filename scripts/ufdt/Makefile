# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2018-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.

hostprogs-y	:= ufdt_apply_overlay extract_dtb
always		:= $(hostprogs-y)

common-objects := ufdt_convert.o ufdt_node.o ufdt_node_pool.o ufdt_overlay.o ufdt_prop_dict.o tests/src/util.o sysdeps/libufdt_sysdeps_posix.o ../../dtc-aosp/dtc/libfdt/fdt.o ../../dtc-aosp/dtc/libfdt/fdt_sw.o ../../dtc-aosp/dtc/libfdt/fdt_ro.o ../../dtc-aosp/dtc/libfdt/fdt_rw.o ../../dtc-aosp/dtc/libfdt/fdt_wip.o ../../dtc-aosp/dtc/libfdt/fdt_strerror.o

# We're working with a submodule, so make these all relative to that.
common-objects	:= $(addprefix libufdt/,$(common-objects))

ufdt_apply_overlay-objs := libufdt/tests/src/ufdt_overlay_test_app.o $(common-objects)
extract_dtb-objs := libufdt/tests/src/extract_dtb.o $(common-objects)

HOSTCFLAGS_LIBUFDT := -I$(src)/libufdt/include -I$(src)/libufdt/sysdeps/include -std=c99 -Wno-missing-prototypes -Wno-strict-prototypes -Wno-format -I$(src)/../dtc-aosp/dtc/libfdt
HOSTCFLAGS_ufdt_convert.o := $(HOSTCFLAGS_LIBUFDT)
HOSTCFLAGS_ufdt_node.o := $(HOSTCFLAGS_LIBUFDT)
HOSTCFLAGS_ufdt_node_pool.o := $(HOSTCFLAGS_LIBUFDT)
HOSTCFLAGS_ufdt_overlay.o := $(HOSTCFLAGS_LIBUFDT)
HOSTCFLAGS_ufdt_prop_dict.o := $(HOSTCFLAGS_LIBUFDT)
HOSTCFLAGS_ufdt_overlay_test_app.o := $(HOSTCFLAGS_LIBUFDT)
HOSTCFLAGS_extract_dtb.o := $(HOSTCFLAGS_LIBUFDT)
HOSTCFLAGS_util.o := $(HOSTCFLAGS_LIBUFDT)
HOSTCFLAGS_libufdt_sysdeps_posix.o := $(HOSTCFLAGS_LIBUFDT)

HOSTCFLAGS_LIBFDT := -I$(src)/../dtc-aosp/dtc/libfdt
HOSTCFLAGS_fdt.o := $(HOSTCFLAGS_LIBFDT)
HOSTCFLAGS_fdt_sw.o := $(HOSTCFLAGS_LIBFDT)
HOSTCFLAGS_fdt_ro.o := $(HOSTCFLAGS_LIBFDT)
HOSTCFLAGS_fdt_rw.o := $(HOSTCFLAGS_LIBFDT)
HOSTCFLAGS_fdt_wip.o := $(HOSTCFLAGS_LIBFDT)
HOSTCFLAGS_fdt_strerror.o := $(HOSTCFLAGS_LIBFDT)
