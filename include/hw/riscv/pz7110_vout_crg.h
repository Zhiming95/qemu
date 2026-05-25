/*
 * PZ7110 VOUT CRG (Display Clock & Reset Generator) definitions
 * Minimal register block at 0x295C0000, 64KB.
 *
 * Copyright (c) 2024 StarSix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#ifndef HW_RISCV_PZ7110_VOUT_CRG_H
#define HW_RISCV_PZ7110_VOUT_CRG_H

#include "hw/sysbus.h"

#define TYPE_PZ7110_VOUT_CRG "pz7110.vout-crg"
OBJECT_DECLARE_SIMPLE_TYPE(PZ7110VOUTCRGState, PZ7110_VOUT_CRG)

/* Only 4 registers defined in TRM; the rest are reserved/unused. */
#define PZ7110_VOUT_CRG_REG_COUNT 4

struct PZ7110VOUTCRGState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t regs[PZ7110_VOUT_CRG_REG_COUNT];
};

#endif
