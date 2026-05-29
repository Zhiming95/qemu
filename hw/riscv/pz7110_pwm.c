/*
 * QEMU PZ7110 OpenCores PWM Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/riscv/pz7110_pwm.h"
#include "migration/vmstate.h"

static uint64_t pz7110_pwm_read(void *opaque, hwaddr addr, unsigned int size)
{
    PZ7110PwmState *s = PZ7110_PWM(opaque);

    switch (addr) {
    case PWM_PERIOD:
        return s->period;
    case PWM_DUTY:
        return s->duty;
    case PWM_ENABLE:
        return s->enable;
    default:
        return 0;
    }
}

static void pz7110_pwm_write(void *opaque, hwaddr addr, uint64_t value,
                             unsigned int size)
{
    PZ7110PwmState *s = PZ7110_PWM(opaque);

    switch (addr) {
    case PWM_PERIOD:
        s->period = value;
        break;
    case PWM_DUTY:
        s->duty = value;
        break;
    case PWM_ENABLE:
        s->enable = value;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pz7110_pwm_ops = {
    .read = pz7110_pwm_read,
    .write = pz7110_pwm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void pz7110_pwm_reset(DeviceState *dev)
{
    PZ7110PwmState *s = PZ7110_PWM(dev);

    s->period = 0;
    s->duty = 0;
    s->enable = 0;
}

static void pz7110_pwm_init(Object *obj)
{
    PZ7110PwmState *s = PZ7110_PWM(obj);

    memory_region_init_io(&s->iomem, obj, &pz7110_pwm_ops,
                          s, "pz7110-pwm", 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription pz7110_pwm_vmstate = {
    .name = TYPE_PZ7110_PWM,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(period, PZ7110PwmState),
        VMSTATE_UINT32(duty, PZ7110PwmState),
        VMSTATE_UINT32(enable, PZ7110PwmState),
        VMSTATE_END_OF_LIST()
    }
};

static void pz7110_pwm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, pz7110_pwm_reset);
    dc->vmsd = &pz7110_pwm_vmstate;
}

static const TypeInfo pz7110_pwm_info = {
    .name = TYPE_PZ7110_PWM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110PwmState),
    .instance_init = pz7110_pwm_init,
    .class_init = pz7110_pwm_class_init,
};

static void pz7110_pwm_register_types(void)
{
    type_register_static(&pz7110_pwm_info);
}

type_init(pz7110_pwm_register_types)
