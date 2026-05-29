/*
 * QEMU PZ7110 OpenCores PWM Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_PZ7110_PWM_H
#define HW_RISCV_PZ7110_PWM_H

#include "hw/sysbus.h"

#define TYPE_PZ7110_PWM "pz7110-pwm"
OBJECT_DECLARE_SIMPLE_TYPE(PZ7110PwmState, PZ7110_PWM)

#define PWM_PERIOD 0x00
#define PWM_DUTY   0x04
#define PWM_ENABLE 0x08

typedef struct PZ7110PwmState {
    SysBusDevice parent;

    MemoryRegion iomem;
    uint32_t period;
    uint32_t duty;
    uint32_t enable;
} PZ7110PwmState;

#endif
