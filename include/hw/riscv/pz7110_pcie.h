/*
 * QEMU PZ7110 PLDA XpressRICH3 PCIe Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_PZ7110_PCIE_H
#define HW_RISCV_PZ7110_PCIE_H

#include "hw/sysbus.h"

#define TYPE_PZ7110_PCIE "pz7110-pcie"
OBJECT_DECLARE_SIMPLE_TYPE(PZ7110PcieState, PZ7110_PCIE)

#define PCIE_APB_ID     0x000
#define PCIE_APB_CLASS  0x008
#define PCIE_APB_STATUS 0x010
#define PCIE_APB_CTRL   0x014

typedef struct PZ7110PcieState {
    SysBusDevice parent;

    MemoryRegion iomem;
    MemoryRegion cfg;
    qemu_irq irq;

    uint32_t apb_status;
    uint32_t apb_ctrl;
} PZ7110PcieState;

#endif
