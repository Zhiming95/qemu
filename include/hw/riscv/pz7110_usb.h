/*
 * QEMU PZ7110 Cadence USB3 Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_PZ7110_USB_H
#define HW_RISCV_PZ7110_USB_H

#include "hw/sysbus.h"

#define TYPE_PZ7110_USB "pz7110-usb"
OBJECT_DECLARE_SIMPLE_TYPE(PZ7110UsbState, PZ7110_USB)

#define USB_IRQSTATUS 0x00c
#define USB_IRQENABLE 0x010
#define USB_EPCSTATUS 0x014
#define USB_EPCDATA   0x018

typedef struct PZ7110UsbState {
    SysBusDevice parent;

    MemoryRegion iomem;
    qemu_irq irq;
    uint32_t irqstatus;
    uint32_t irqenable;
} PZ7110UsbState;

#endif
