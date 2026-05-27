/*
 * QEMU PZ7110 StarFive Watchdog Timer
 *
 * Copyright (c) 2024 StarSix, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_PZ7110_WDT_H
#define HW_RISCV_PZ7110_WDT_H

#include "hw/sysbus.h"
#include "qemu/timer.h"

#define TYPE_PZ7110_WDT "pz7110-wdt"
OBJECT_DECLARE_SIMPLE_TYPE(PZ7110WdtState, PZ7110_WDT)

#define PZ7110_WDT_LOAD       0x000
#define PZ7110_WDT_VALUE      0x004
#define PZ7110_WDT_CONTROL    0x008
#define PZ7110_WDT_INTCLR     0x00c
#define PZ7110_WDT_RIS        0x010
#define PZ7110_WDT_IMS        0x014
#define PZ7110_WDT_LOCK       0xc00

#define PZ7110_WDT_ENABLE     BIT(0)
#define PZ7110_WDT_RESET_EN   BIT(1)
#define PZ7110_WDT_UNLOCK_KEY 0x1acce551
#define PZ7110_WDT_INTCLR_VAL 0x1

struct PZ7110WdtState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    QEMUTimer *timer;

    uint32_t load;
    uint32_t control;
    uint32_t int_status;
    bool locked;
    uint32_t freq;
};

#endif
