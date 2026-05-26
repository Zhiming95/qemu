/*
 * QEMU PZ7110 StarFive timer
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/riscv/pz7110_timer.h"
#include "migration/vmstate.h"
#include "qemu/timer.h"

#define PZ7110_TIMER_FREQ_HZ 24000000

static void pz7110_timer_update_irq(PZ7110TimerState *s, unsigned int ch)
{
    PZ7110TimerChannel *c = &s->channels[ch];

    qemu_set_irq(s->irq[ch], c->pending && !c->intmask);
}

static uint64_t pz7110_timer_ticks_to_ns(uint32_t ticks)
{
    uint64_t count = ticks ? ticks : UINT32_MAX;

    return muldiv64(count, NANOSECONDS_PER_SECOND, PZ7110_TIMER_FREQ_HZ);
}

static void pz7110_timer_start(PZ7110TimerState *s, unsigned int ch)
{
    PZ7110TimerChannel *c = &s->channels[ch];
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    c->expire_time = now + pz7110_timer_ticks_to_ns(c->load);
    timer_mod(c->timer, c->expire_time);
}

static void pz7110_timer_stop(PZ7110TimerChannel *c)
{
    timer_del(c->timer);
    c->expire_time = 0;
}

static void pz7110_timer_expire(void *opaque)
{
    PZ7110TimerChannel *c = opaque;
    PZ7110TimerState *s = c->parent;
    unsigned int ch = c - s->channels;

    if (!c->enable) {
        return;
    }

    c->pending = true;
    pz7110_timer_update_irq(s, ch);

    if (c->ctrl == PZ7110_TIMER_MODE_CONTINUOUS) {
        pz7110_timer_start(s, ch);
    } else {
        c->enable = false;
        c->expire_time = 0;
    }
}

static uint32_t pz7110_timer_value(PZ7110TimerChannel *c)
{
    int64_t now;
    int64_t remaining_ns;

    if (!c->enable || !timer_pending(c->timer)) {
        return c->load;
    }

    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    remaining_ns = c->expire_time - now;
    if (remaining_ns <= 0) {
        return 0;
    }

    return muldiv64(remaining_ns, PZ7110_TIMER_FREQ_HZ,
                    NANOSECONDS_PER_SECOND);
}

static uint64_t pz7110_timer_read(void *opaque, hwaddr addr, unsigned int size)
{
    PZ7110TimerState *s = PZ7110_TIMER(opaque);
    unsigned int ch = addr / PZ7110_TIMER_CHANNEL_SIZE;
    hwaddr offset = addr % PZ7110_TIMER_CHANNEL_SIZE;
    PZ7110TimerChannel *c;

    if (ch >= PZ7110_TIMER_NUM_CHANNELS) {
        return 0;
    }

    c = &s->channels[ch];

    switch (offset) {
    case PZ7110_TIMER_INT_STATUS:
        return c->pending ? 1 : 0;
    case PZ7110_TIMER_CTRL:
        return c->ctrl;
    case PZ7110_TIMER_LOAD:
        return c->load;
    case PZ7110_TIMER_ENABLE:
        return c->enable ? 1 : 0;
    case PZ7110_TIMER_VALUE:
        return pz7110_timer_value(c);
    case PZ7110_TIMER_INT_CLR:
        /*
         * The Linux driver polls bit 1 until the clear path is available.
         * Returning 0 makes the clear path immediately available.
         */
        return 0;
    case PZ7110_TIMER_INT_MASK:
        return c->intmask;
    default:
        return 0;
    }
}

static void pz7110_timer_write(void *opaque, hwaddr addr, uint64_t value,
                               unsigned int size)
{
    PZ7110TimerState *s = PZ7110_TIMER(opaque);
    unsigned int ch = addr / PZ7110_TIMER_CHANNEL_SIZE;
    hwaddr offset = addr % PZ7110_TIMER_CHANNEL_SIZE;
    PZ7110TimerChannel *c;

    if (ch >= PZ7110_TIMER_NUM_CHANNELS) {
        return;
    }

    c = &s->channels[ch];

    switch (offset) {
    case PZ7110_TIMER_CTRL:
        c->ctrl = value & 1;
        break;
    case PZ7110_TIMER_LOAD:
        c->load = value;
        if (c->enable) {
            pz7110_timer_start(s, ch);
        }
        break;
    case PZ7110_TIMER_ENABLE:
        c->enable = value & 1;
        if (c->enable) {
            pz7110_timer_start(s, ch);
        } else {
            pz7110_timer_stop(c);
        }
        break;
    case PZ7110_TIMER_RELOAD:
        if (c->enable) {
            pz7110_timer_start(s, ch);
        }
        break;
    case PZ7110_TIMER_INT_CLR:
        if (value & 1) {
            c->pending = false;
            pz7110_timer_update_irq(s, ch);
        }
        break;
    case PZ7110_TIMER_INT_MASK:
        c->intmask = value & 1;
        pz7110_timer_update_irq(s, ch);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pz7110_timer_ops = {
    .read = pz7110_timer_read,
    .write = pz7110_timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void pz7110_timer_reset(DeviceState *dev)
{
    PZ7110TimerState *s = PZ7110_TIMER(dev);

    for (unsigned int ch = 0; ch < PZ7110_TIMER_NUM_CHANNELS; ch++) {
        PZ7110TimerChannel *c = &s->channels[ch];

        pz7110_timer_stop(c);
        c->load = 0;
        c->ctrl = PZ7110_TIMER_MODE_CONTINUOUS;
        c->enable = false;
        c->pending = false;
        c->intmask = 1;
        pz7110_timer_update_irq(s, ch);
    }
}

static void pz7110_timer_init(Object *obj)
{
    PZ7110TimerState *s = PZ7110_TIMER(obj);

    memory_region_init_io(&s->mmio, obj, &pz7110_timer_ops, s,
                          "pz7110.timer", 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    for (unsigned int ch = 0; ch < PZ7110_TIMER_NUM_CHANNELS; ch++) {
        PZ7110TimerChannel *c = &s->channels[ch];

        c->parent = s;
        c->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, pz7110_timer_expire, c);
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[ch]);
    }
}

static const VMStateDescription vmstate_pz7110_timer_channel = {
    .name = "pz7110.timer.channel",
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(load, PZ7110TimerChannel),
        VMSTATE_UINT32(ctrl, PZ7110TimerChannel),
        VMSTATE_BOOL(enable, PZ7110TimerChannel),
        VMSTATE_BOOL(pending, PZ7110TimerChannel),
        VMSTATE_UINT32(intmask, PZ7110TimerChannel),
        VMSTATE_INT64(expire_time, PZ7110TimerChannel),
        VMSTATE_TIMER_PTR(timer, PZ7110TimerChannel),
        VMSTATE_END_OF_LIST(),
    },
};

static const VMStateDescription vmstate_pz7110_timer = {
    .name = TYPE_PZ7110_TIMER,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(channels, PZ7110TimerState,
                             PZ7110_TIMER_NUM_CHANNELS, 1,
                             vmstate_pz7110_timer_channel,
                             PZ7110TimerChannel),
        VMSTATE_END_OF_LIST(),
    },
};

static void pz7110_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, pz7110_timer_reset);
    dc->vmsd = &vmstate_pz7110_timer;
}

static const TypeInfo pz7110_timer_info = {
    .name          = TYPE_PZ7110_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110TimerState),
    .instance_init = pz7110_timer_init,
    .class_init    = pz7110_timer_class_init,
};

static void pz7110_timer_register_types(void)
{
    type_register_static(&pz7110_timer_info);
}

type_init(pz7110_timer_register_types)
