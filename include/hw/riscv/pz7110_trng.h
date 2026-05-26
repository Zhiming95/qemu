/*
 * QEMU PZ7110 StarFive TRNG
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_PZ7110_TRNG_H
#define HW_RISCV_PZ7110_TRNG_H

#include "hw/sysbus.h"

#define TYPE_PZ7110_TRNG "pz7110-trng"
OBJECT_DECLARE_SIMPLE_TYPE(PZ7110TrngState, PZ7110_TRNG)

struct PZ7110TrngState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t ctrl;
    uint32_t stat;
    uint32_t mode;
    uint32_t smode;
    uint32_t ie;
    uint32_t istat;
    uint32_t auto_rqsts;
    uint32_t auto_age;
};

#endif
