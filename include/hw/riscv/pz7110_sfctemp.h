/*
 * QEMU PZ7110 StarFive Temperature Sensor
 *
 * Copyright (c) 2024 StarSix, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_PZ7110_SFCTEMP_H
#define HW_RISCV_PZ7110_SFCTEMP_H

#include "hw/irq.h"
#include "hw/sysbus.h"

#define TYPE_PZ7110_SFCTEMP "pz7110-sfctemp"
OBJECT_DECLARE_SIMPLE_TYPE(PZ7110SFCTempState, PZ7110_SFCTEMP)

#define SFCTEMP_CTRL       0x00
#define SFCTEMP_RSTN       BIT(0)
#define SFCTEMP_PD         BIT(1)
#define SFCTEMP_RUN        BIT(2)
#define SFCTEMP_DOUT_POS   16
#define SFCTEMP_DOUT_MSK   0x0fff0000U

struct PZ7110SFCTempState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    uint32_t ctrl;
};

#endif
