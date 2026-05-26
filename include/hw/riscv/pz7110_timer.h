/*
 * QEMU PZ7110 StarFive timer
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_PZ7110_TIMER_H
#define HW_RISCV_PZ7110_TIMER_H

#include "hw/sysbus.h"
#include "qemu/timer.h"

#define TYPE_PZ7110_TIMER "pz7110-timer"
OBJECT_DECLARE_SIMPLE_TYPE(PZ7110TimerState, PZ7110_TIMER)

#define PZ7110_TIMER_NUM_CHANNELS 4
#define PZ7110_TIMER_CHANNEL_SIZE 0x40

#define PZ7110_TIMER_INT_STATUS 0x00
#define PZ7110_TIMER_CTRL       0x04
#define PZ7110_TIMER_LOAD       0x08
#define PZ7110_TIMER_ENABLE     0x10
#define PZ7110_TIMER_RELOAD     0x14
#define PZ7110_TIMER_VALUE      0x18
#define PZ7110_TIMER_INT_CLR    0x20
#define PZ7110_TIMER_INT_MASK   0x24

#define PZ7110_TIMER_MODE_CONTINUOUS 0
#define PZ7110_TIMER_MODE_SINGLE     1

typedef struct PZ7110TimerState PZ7110TimerState;

typedef struct PZ7110TimerChannel {
    PZ7110TimerState *parent;
    QEMUTimer *timer;
    uint32_t load;
    uint32_t ctrl;
    bool enable;
    bool pending;
    uint32_t intmask;
    int64_t expire_time;
} PZ7110TimerChannel;

struct PZ7110TimerState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq[PZ7110_TIMER_NUM_CHANNELS];
    PZ7110TimerChannel channels[PZ7110_TIMER_NUM_CHANNELS];
};

#endif
