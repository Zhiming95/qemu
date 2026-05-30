/*
 * QEMU PZ7110 PMU power controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_PZ7110_PMU_H
#define HW_RISCV_PZ7110_PMU_H

#include "hw/sysbus.h"

#define TYPE_PZ7110_PMU "pz7110-pmu"
OBJECT_DECLARE_SIMPLE_TYPE(PZ7110PmuState, PZ7110_PMU)

struct PZ7110PmuState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t power_mode;
};

#endif
