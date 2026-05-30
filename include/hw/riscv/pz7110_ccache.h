/*
 * QEMU PZ7110 cache controller model
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_PZ7110_CCACHE_H
#define HW_RISCV_PZ7110_CCACHE_H

#include "hw/sysbus.h"

#define TYPE_PZ7110_CCACHE "pz7110-ccache"
OBJECT_DECLARE_SIMPLE_TYPE(PZ7110CcacheState, PZ7110_CCACHE)

struct PZ7110CcacheState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    uint32_t wayenable;
};

#endif
