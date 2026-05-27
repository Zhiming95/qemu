/*
 * QEMU PZ7110 StarFive Watchdog Timer
 *
 * Copyright (c) 2024 StarSix, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/riscv/pz7110_wdt.h"
#include "migration/vmstate.h"
#include "system/runstate.h"

#define PZ7110_WDT_DEFAULT_FREQ_HZ 24000000

static void pz7110_wdt_update_irq(PZ7110WdtState *s)
{
    qemu_set_irq(s->irq, (s->control & PZ7110_WDT_ENABLE) && s->int_status);
}

static uint64_t pz7110_wdt_timeout_ns(PZ7110WdtState *s)
{
    return muldiv64(s->load ? s->load : UINT32_MAX, NANOSECONDS_PER_SECOND,
                    s->freq);
}

static void pz7110_wdt_reload(PZ7110WdtState *s)
{
    if (s->control & PZ7110_WDT_ENABLE) {
        timer_mod(s->timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  pz7110_wdt_timeout_ns(s));
    }
}

static void pz7110_wdt_tick(void *opaque)
{
    PZ7110WdtState *s = PZ7110_WDT(opaque);

    if (!(s->control & PZ7110_WDT_ENABLE)) {
        return;
    }

    if (!s->int_status) {
        s->int_status = 1;
        pz7110_wdt_update_irq(s);
        pz7110_wdt_reload(s);
        return;
    }

    if (s->control & PZ7110_WDT_RESET_EN) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        return;
    }

    pz7110_wdt_update_irq(s);
    pz7110_wdt_reload(s);
}

static uint32_t pz7110_wdt_current_value(PZ7110WdtState *s)
{
    int64_t now;
    int64_t expire;
    int64_t remaining_ns;

    if (!(s->control & PZ7110_WDT_ENABLE) || !timer_pending(s->timer)) {
        return s->load;
    }

    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    expire = timer_expire_time_ns(s->timer);
    remaining_ns = expire - now;
    if (remaining_ns <= 0) {
        return 0;
    }

    return muldiv64(remaining_ns, s->freq, NANOSECONDS_PER_SECOND);
}

static uint64_t pz7110_wdt_read(void *opaque, hwaddr addr, unsigned int size)
{
    PZ7110WdtState *s = PZ7110_WDT(opaque);

    switch (addr) {
    case PZ7110_WDT_LOAD:
        return s->load;
    case PZ7110_WDT_VALUE:
        return pz7110_wdt_current_value(s);
    case PZ7110_WDT_CONTROL:
        return s->control;
    case PZ7110_WDT_RIS:
    case PZ7110_WDT_IMS:
        return s->int_status;
    case PZ7110_WDT_INTCLR:
        return 0;
    case PZ7110_WDT_LOCK:
        return s->locked ? 1 : 0;
    default:
        return 0;
    }
}

static void pz7110_wdt_write(void *opaque, hwaddr addr, uint64_t value,
                             unsigned int size)
{
    PZ7110WdtState *s = PZ7110_WDT(opaque);
    uint32_t val = value;
    bool was_enabled;

    if (addr == PZ7110_WDT_LOCK) {
        s->locked = val != PZ7110_WDT_UNLOCK_KEY;
        return;
    }

    if (s->locked) {
        return;
    }

    switch (addr) {
    case PZ7110_WDT_LOAD:
        s->load = val;
        pz7110_wdt_reload(s);
        break;
    case PZ7110_WDT_CONTROL:
        was_enabled = s->control & PZ7110_WDT_ENABLE;
        s->control = val & (PZ7110_WDT_ENABLE | PZ7110_WDT_RESET_EN);
        if ((s->control & PZ7110_WDT_ENABLE) && !was_enabled) {
            pz7110_wdt_reload(s);
        } else if (!(s->control & PZ7110_WDT_ENABLE)) {
            timer_del(s->timer);
            s->int_status = 0;
            pz7110_wdt_update_irq(s);
        }
        break;
    case PZ7110_WDT_INTCLR:
        if (val & PZ7110_WDT_INTCLR_VAL) {
            s->int_status = 0;
            pz7110_wdt_update_irq(s);
            pz7110_wdt_reload(s);
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pz7110_wdt_ops = {
    .read = pz7110_wdt_read,
    .write = pz7110_wdt_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void pz7110_wdt_reset(DeviceState *dev)
{
    PZ7110WdtState *s = PZ7110_WDT(dev);

    timer_del(s->timer);
    s->load = 0;
    s->control = 0;
    s->int_status = 0;
    s->locked = true;
    s->freq = PZ7110_WDT_DEFAULT_FREQ_HZ;
    pz7110_wdt_update_irq(s);
}

static void pz7110_wdt_init(Object *obj)
{
    PZ7110WdtState *s = PZ7110_WDT(obj);

    memory_region_init_io(&s->mmio, obj, &pz7110_wdt_ops, s,
                          "pz7110.wdt", 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, pz7110_wdt_tick, s);
}

static const VMStateDescription vmstate_pz7110_wdt = {
    .name = TYPE_PZ7110_WDT,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(load, PZ7110WdtState),
        VMSTATE_UINT32(control, PZ7110WdtState),
        VMSTATE_UINT32(int_status, PZ7110WdtState),
        VMSTATE_BOOL(locked, PZ7110WdtState),
        VMSTATE_UINT32(freq, PZ7110WdtState),
        VMSTATE_TIMER_PTR(timer, PZ7110WdtState),
        VMSTATE_END_OF_LIST(),
    },
};

static void pz7110_wdt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, pz7110_wdt_reset);
    dc->vmsd = &vmstate_pz7110_wdt;
}

static const TypeInfo pz7110_wdt_info = {
    .name          = TYPE_PZ7110_WDT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110WdtState),
    .instance_init = pz7110_wdt_init,
    .class_init    = pz7110_wdt_class_init,
};

static void pz7110_wdt_register_types(void)
{
    type_register_static(&pz7110_wdt_info);
}

type_init(pz7110_wdt_register_types)
