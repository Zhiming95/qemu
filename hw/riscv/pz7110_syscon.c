/*
 * PZ7110 SYSCON (System Configuration Registers)
 *
 * Pure storage registers for system configuration. Reads return
 * stored values, writes store values. No hardware behavior modeled.
 *
 * Three instances:
 *   SYS SYSCON  at 0x13030000, 157 regs (SYSCFG 0-156)
 *   STG SYSCON  at 0x10240000, 933 regs (SYSCFG 0-932)
 *   AON SYSCON  at 0x17010000,  41 regs (SYSCFG 0-40)
 *
 * Copyright (c) 2024 StarSix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/riscv/pz7110_syscon.h"

/* Generic SYSCON read/write for any number of registers */
static uint64_t pz7110_syscon_read(void *opaque, hwaddr addr,
                                   unsigned int size)
{
    uint32_t *regs = opaque;
    unsigned int idx = addr >> 2;

    return regs[idx];
}

static void pz7110_syscon_write(void *opaque, hwaddr addr,
                                uint64_t val64, unsigned int size)
{
    uint32_t *regs = opaque;
    unsigned int idx = addr >> 2;

    regs[idx] = (uint32_t)val64;
}

static const MemoryRegionOps pz7110_syscon_ops = {
    .read = pz7110_syscon_read,
    .write = pz7110_syscon_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/* ================================================================
 * SYS SYSCON (0x13030000, 157 regs)
 * ================================================================ */

static void pz7110_sys_syscon_reset_enter(Object *obj, ResetType type)
{
    PZ7110SYSSYSCONState *s = PZ7110_SYS_SYSCON(obj);
    memset(s->regs, 0, sizeof(s->regs));
}

static void pz7110_sys_syscon_init(Object *obj)
{
    PZ7110SYSSYSCONState *s = PZ7110_SYS_SYSCON(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &pz7110_syscon_ops,
                          s->regs, "pz7110.sys-syscon",
                          PZ7110_SYS_SYSCON_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
}

static const VMStateDescription vmstate_pz7110_sys_syscon = {
    .name = TYPE_PZ7110_SYS_SYSCON,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, PZ7110SYSSYSCONState,
                             PZ7110_SYS_SYSCON_REGS),
        VMSTATE_END_OF_LIST(),
    }
};

static void pz7110_sys_syscon_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PZ7110 SYS SYSCON (System Configuration)";
    dc->vmsd = &vmstate_pz7110_sys_syscon;
    rc->phases.enter = pz7110_sys_syscon_reset_enter;
}

static const TypeInfo pz7110_sys_syscon_info = {
    .name          = TYPE_PZ7110_SYS_SYSCON,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110SYSSYSCONState),
    .class_init    = pz7110_sys_syscon_class_init,
    .instance_init = pz7110_sys_syscon_init,
};

/* ================================================================
 * STG SYSCON (0x10240000, 933 regs)
 * ================================================================ */

static void pz7110_stg_syscon_reset_enter(Object *obj, ResetType type)
{
    PZ7110STGSYSCONState *s = PZ7110_STG_SYSCON(obj);
    memset(s->regs, 0, sizeof(s->regs));
}

static void pz7110_stg_syscon_init(Object *obj)
{
    PZ7110STGSYSCONState *s = PZ7110_STG_SYSCON(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &pz7110_syscon_ops,
                          s->regs, "pz7110.stg-syscon",
                          PZ7110_STG_SYSCON_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
}

static const VMStateDescription vmstate_pz7110_stg_syscon = {
    .name = TYPE_PZ7110_STG_SYSCON,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, PZ7110STGSYSCONState,
                             PZ7110_STG_SYSCON_REGS),
        VMSTATE_END_OF_LIST(),
    }
};

static void pz7110_stg_syscon_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PZ7110 STG SYSCON (Storage Configuration)";
    dc->vmsd = &vmstate_pz7110_stg_syscon;
    rc->phases.enter = pz7110_stg_syscon_reset_enter;
}

static const TypeInfo pz7110_stg_syscon_info = {
    .name          = TYPE_PZ7110_STG_SYSCON,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110STGSYSCONState),
    .class_init    = pz7110_stg_syscon_class_init,
    .instance_init = pz7110_stg_syscon_init,
};

/* ================================================================
 * AON SYSCON (0x17010000, 41 regs)
 * ================================================================ */

static void pz7110_aon_syscon_reset_enter(Object *obj, ResetType type)
{
    PZ7110AONSYSCONState *s = PZ7110_AON_SYSCON(obj);
    memset(s->regs, 0, sizeof(s->regs));
}

static void pz7110_aon_syscon_init(Object *obj)
{
    PZ7110AONSYSCONState *s = PZ7110_AON_SYSCON(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &pz7110_syscon_ops,
                          s->regs, "pz7110.aon-syscon",
                          PZ7110_AON_SYSCON_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
}

static const VMStateDescription vmstate_pz7110_aon_syscon = {
    .name = TYPE_PZ7110_AON_SYSCON,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, PZ7110AONSYSCONState,
                             PZ7110_AON_SYSCON_REGS),
        VMSTATE_END_OF_LIST(),
    }
};

static void pz7110_aon_syscon_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PZ7110 AON SYSCON (Always-On Configuration)";
    dc->vmsd = &vmstate_pz7110_aon_syscon;
    rc->phases.enter = pz7110_aon_syscon_reset_enter;
}

static const TypeInfo pz7110_aon_syscon_info = {
    .name          = TYPE_PZ7110_AON_SYSCON,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110AONSYSCONState),
    .class_init    = pz7110_aon_syscon_class_init,
    .instance_init = pz7110_aon_syscon_init,
};

/* ================================================================
 * Type registration
 * ================================================================ */

static void pz7110_syscon_register_types(void)
{
    type_register_static(&pz7110_sys_syscon_info);
    type_register_static(&pz7110_stg_syscon_info);
    type_register_static(&pz7110_aon_syscon_info);
}

type_init(pz7110_syscon_register_types)
