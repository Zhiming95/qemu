/*
 * QEMU PZ7110 StarFive Temperature Sensor
 *
 * Copyright (c) 2024 StarSix, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/riscv/pz7110_sfctemp.h"
#include "migration/vmstate.h"

static uint64_t pz7110_sfctemp_read(void *opaque, hwaddr addr, unsigned int size)
{
    PZ7110SFCTempState *s = PZ7110_SFCTEMP(opaque);
    uint32_t val;

    switch (addr) {
    case SFCTEMP_CTRL:
        /*
         * The Linux driver computes:
         * temp(mC) = DOUT * 237500 / 4094 - 81100.
         * DOUT=586 gives about 45 C, below the VF2 thermal trip points.
         */
        val = s->ctrl & ~SFCTEMP_DOUT_MSK;
        val |= (586 << SFCTEMP_DOUT_POS) & SFCTEMP_DOUT_MSK;
        return val;
    default:
        return 0;
    }
}

static void pz7110_sfctemp_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned int size)
{
    PZ7110SFCTempState *s = PZ7110_SFCTEMP(opaque);
    uint32_t old;
    uint32_t val = value;

    switch (addr) {
    case SFCTEMP_CTRL:
        old = s->ctrl;
        s->ctrl = val & (SFCTEMP_RSTN | SFCTEMP_PD | SFCTEMP_RUN);

        /*
         * sfctemp_run_single() writes RSTN|RUN then RSTN.  Pulse the IRQ on
         * the RUN falling edge so the driver's completion handler wakes up.
         */
        if ((old & SFCTEMP_RUN) && !(s->ctrl & SFCTEMP_RUN)) {
            qemu_irq_pulse(s->irq);
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pz7110_sfctemp_ops = {
    .read = pz7110_sfctemp_read,
    .write = pz7110_sfctemp_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void pz7110_sfctemp_reset(DeviceState *dev)
{
    PZ7110SFCTempState *s = PZ7110_SFCTEMP(dev);

    s->ctrl = SFCTEMP_PD;
}

static void pz7110_sfctemp_init(Object *obj)
{
    PZ7110SFCTempState *s = PZ7110_SFCTEMP(obj);

    memory_region_init_io(&s->mmio, obj, &pz7110_sfctemp_ops, s,
                          "pz7110.sfctemp", 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static const VMStateDescription vmstate_pz7110_sfctemp = {
    .name = TYPE_PZ7110_SFCTEMP,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ctrl, PZ7110SFCTempState),
        VMSTATE_END_OF_LIST(),
    },
};

static void pz7110_sfctemp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, pz7110_sfctemp_reset);
    dc->vmsd = &vmstate_pz7110_sfctemp;
}

static const TypeInfo pz7110_sfctemp_info = {
    .name          = TYPE_PZ7110_SFCTEMP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110SFCTempState),
    .instance_init = pz7110_sfctemp_init,
    .class_init    = pz7110_sfctemp_class_init,
};

static void pz7110_sfctemp_register_types(void)
{
    type_register_static(&pz7110_sfctemp_info);
}

type_init(pz7110_sfctemp_register_types)
