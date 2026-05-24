/*
 * PZ7110 SYSCON (System Configuration Register) definitions
 * Covers SYS SYSCON, STG SYSCON, and AON SYSCON.
 *
 * These are pure storage registers - reads return stored values,
 * writes store values. No hardware behavior is simulated.
 *
 * Copyright (c) 2024 StarSix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#ifndef HW_RISCV_PZ7110_SYSCON_H
#define HW_RISCV_PZ7110_SYSCON_H

#include "hw/sysbus.h"

/* SYS SYSCON: SYSCFG 0 - 156, 157 registers, 4 bytes each */
#define TYPE_PZ7110_SYS_SYSCON "pz7110.sys-syscon"
OBJECT_DECLARE_SIMPLE_TYPE(PZ7110SYSSYSCONState, PZ7110_SYS_SYSCON)
#define PZ7110_SYS_SYSCON_REGS  157
#define PZ7110_SYS_SYSCON_SIZE  (PZ7110_SYS_SYSCON_REGS * 4)

struct PZ7110SYSSYSCONState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    uint32_t regs[PZ7110_SYS_SYSCON_REGS];
};

/* STG SYSCON: SYSCFG 0 - 932, 933 registers, 4 bytes each */
#define TYPE_PZ7110_STG_SYSCON "pz7110.stg-syscon"
OBJECT_DECLARE_SIMPLE_TYPE(PZ7110STGSYSCONState, PZ7110_STG_SYSCON)
#define PZ7110_STG_SYSCON_REGS  933
#define PZ7110_STG_SYSCON_SIZE  (PZ7110_STG_SYSCON_REGS * 4)

struct PZ7110STGSYSCONState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    uint32_t regs[PZ7110_STG_SYSCON_REGS];
};

/* AON SYSCON: SYSCFG 0 - 40, 41 registers, 4 bytes each */
#define TYPE_PZ7110_AON_SYSCON "pz7110.aon-syscon"
OBJECT_DECLARE_SIMPLE_TYPE(PZ7110AONSYSCONState, PZ7110_AON_SYSCON)
#define PZ7110_AON_SYSCON_REGS  41
#define PZ7110_AON_SYSCON_SIZE  (PZ7110_AON_SYSCON_REGS * 4)

struct PZ7110AONSYSCONState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    uint32_t regs[PZ7110_AON_SYSCON_REGS];
};

#endif
