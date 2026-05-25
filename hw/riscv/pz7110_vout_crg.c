/*
 * PZ7110 VOUT CRG (Display Clock & Reset Generator)
 *
 * Minimal register block at 0x295C0000, 64KB.
 * Only 4 clock divider registers are defined (TRM Table 5-5 .. 5-8).
 * Any access beyond offset 0x0c returns 0 silently.
 *
 * No HDMI / MIPI DSI / DC8200 functional modelling.
 *
 * Copyright (c) 2024 StarSix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/riscv/pz7110_vout_crg.h"

/* TRM Table 5-5 .. 5-8: reset values for the 4 defined clocks */
static const uint32_t vout_crg_reset[PZ7110_VOUT_CRG_REG_COUNT] = {
    0x00000004, /* VOUT_CLK_APB        offset 0x00 */
    0x00000004, /* VOUT_CLK_DC8200_PIX0 offset 0x04 */
    0x00000004, /* VOUT_CLK_DSI_SYS    offset 0x08 */
    0x0000000c, /* VOUT_CLK_TX_ESC     offset 0x0c */
};

static uint64_t pz7110_vout_crg_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    PZ7110VOUTCRGState *s = PZ7110_VOUT_CRG(opaque);
    uint32_t idx = addr / 4;

    if (idx < PZ7110_VOUT_CRG_REG_COUNT) {
        return s->regs[idx];
    }
    return 0;
}

static void pz7110_vout_crg_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    PZ7110VOUTCRGState *s = PZ7110_VOUT_CRG(opaque);
    uint32_t idx = addr / 4;

    if (idx < PZ7110_VOUT_CRG_REG_COUNT) {
        s->regs[idx] = (uint32_t)val64;
    }
}

static const MemoryRegionOps pz7110_vout_crg_ops = {
    .read = pz7110_vout_crg_read,
    .write = pz7110_vout_crg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void pz7110_vout_crg_reset(DeviceState *dev)
{
    PZ7110VOUTCRGState *s = PZ7110_VOUT_CRG(dev);
    unsigned int i;

    for (i = 0; i < PZ7110_VOUT_CRG_REG_COUNT; i++) {
        s->regs[i] = vout_crg_reset[i];
    }
}

static void pz7110_vout_crg_init(Object *obj)
{
    PZ7110VOUTCRGState *s = PZ7110_VOUT_CRG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &pz7110_vout_crg_ops,
                          s, "pz7110-vout-crg", 0x10000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_pz7110_vout_crg = {
    .name = TYPE_PZ7110_VOUT_CRG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, PZ7110VOUTCRGState,
                             PZ7110_VOUT_CRG_REG_COUNT),
        VMSTATE_END_OF_LIST(),
    }
};

static void pz7110_vout_crg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PZ7110 VOUT CRG (Display Clock & Reset)";
    dc->vmsd = &vmstate_pz7110_vout_crg;
    device_class_set_legacy_reset(dc, pz7110_vout_crg_reset);
}

static const TypeInfo pz7110_vout_crg_info = {
    .name          = TYPE_PZ7110_VOUT_CRG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110VOUTCRGState),
    .class_init    = pz7110_vout_crg_class_init,
    .instance_init = pz7110_vout_crg_init,
};

static void pz7110_vout_crg_register_types(void)
{
    type_register_static(&pz7110_vout_crg_info);
}

type_init(pz7110_vout_crg_register_types)
