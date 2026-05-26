/*
 * QEMU PZ7110 StarFive RTC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_PZ7110_RTC_H
#define HW_RISCV_PZ7110_RTC_H

#include "hw/sysbus.h"

#define TYPE_PZ7110_RTC "pz7110-rtc"
OBJECT_DECLARE_SIMPLE_TYPE(PZ7110RtcState, PZ7110_RTC)

#define PZ7110_RTC_NUM_IRQS 3

struct PZ7110RtcState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq[PZ7110_RTC_NUM_IRQS];

    uint32_t cfg;
    uint32_t sw_cal_value;
    uint32_t hw_cal_cfg;
    uint32_t cmp_cfg;
    uint32_t irq_en;
    uint32_t irq_event;
    uint32_t irq_status;
    uint32_t cal_value;
    uint32_t cfg_time;
    uint32_t cfg_date;
    uint32_t act_time;
    uint32_t act_date;
};

#endif
