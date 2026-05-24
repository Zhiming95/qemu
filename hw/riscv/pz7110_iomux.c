/*
 * PZ7110 IOMUX + GPIO
 *
 * SYS IOMUX (0x13040000): FMUX pin mux + GPIO0-63 interrupt model
 * AON IOMUX (0x17020000): FMUX pin mux + RGPIO0-3 interrupt model
 *
 * FMUX registers (DOEN, DOUT, SIG_IN) are pure storage — reads return
 * stored values, writes store values.
 *
 * GPIO interrupt registers follow PL061-style behavior:
 *   RIS  = raw interrupt status (set by external pin events)
 *   MIS  = RIS & IE & GPIOEN (masked status, read-only)
 *   IC   = write 1 to clear RIS bits
 *   IS   = sense: 0=level, 1=edge
 *   IBE  = both-edge: 0=single, 1=both
 *   IEV  = event polarity
 *   IE   = interrupt mask
 *   DIN  = data input (current pin state, read-only)
 *
 * Since QEMU has no real GPIO pins, DIN reads return 0 and RIS must be
 * set externally (e.g., via set_irq). The interrupt model is complete
 * for software that configures and checks interrupt status.
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
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/riscv/pz7110_iomux.h"

/* ================================================================
 * SYS IOMUX + GPIO (0x13040000, 64 GPIOs)
 * ================================================================ */

static void sys_iomux_update_irq(PZ7110SysIOMUXState *s)
{
    /* MIS = RIS & IE & GPIOEN */
    uint32_t mis0 = s->ris[0] & s->ie[0] & (s->gpioen ? 0xFFFFFFFF : 0);
    uint32_t mis1 = s->ris[1] & s->ie[1] & (s->gpioen ? 0xFFFFFFFF : 0);
    int level = (mis0 || mis1) ? 1 : 0;
    qemu_set_irq(s->irq, level);
}

static uint64_t pz7110_sys_iomux_read(void *opaque, hwaddr addr,
                                       unsigned int size)
{
    PZ7110SysIOMUXState *s = opaque;

    switch (addr) {
    case SYS_IOMUX_GPIOEN:
        return s->gpioen;
    case SYS_IOMUX_GPIOIS0:
        return s->is[0];
    case SYS_IOMUX_GPIOIS1:
        return s->is[1];
    case SYS_IOMUX_GPIOIBE0:
        return s->ibe[0];
    case SYS_IOMUX_GPIOIBE1:
        return s->ibe[1];
    case SYS_IOMUX_GPIOIEV0:
        return s->iev[0];
    case SYS_IOMUX_GPIOIEV1:
        return s->iev[1];
    case SYS_IOMUX_GPIOIE0:
        return s->ie[0];
    case SYS_IOMUX_GPIOIE1:
        return s->ie[1];
    case SYS_IOMUX_GPIORIS0:
        return s->ris[0];
    case SYS_IOMUX_GPIORIS1:
        return s->ris[1];
    case SYS_IOMUX_GPIOMIS0:
        return s->ris[0] & s->ie[0] & (s->gpioen ? 0xFFFFFFFF : 0);
    case SYS_IOMUX_GPIOMIS1:
        return s->ris[1] & s->ie[1] & (s->gpioen ? 0xFFFFFFFF : 0);
    case SYS_IOMUX_DIN0:
        return s->din[0];
    case SYS_IOMUX_DIN1:
        return s->din[1];
    default:
        /* FMUX, PADCFG, func_sel: flat storage */
        return s->regs[addr >> 2];
    }
}

static void pz7110_sys_iomux_write(void *opaque, hwaddr addr,
                                    uint64_t val64, unsigned int size)
{
    PZ7110SysIOMUXState *s = opaque;
    uint32_t val = (uint32_t)val64;

    switch (addr) {
    case SYS_IOMUX_GPIOEN:
        s->gpioen = val & 1;
        sys_iomux_update_irq(s);
        return;
    case SYS_IOMUX_GPIOIS0:
        s->is[0] = val;
        return;
    case SYS_IOMUX_GPIOIS1:
        s->is[1] = val;
        return;
    case SYS_IOMUX_GPIOIC0:
        /* Write 1 to clear RIS bits */
        s->ris[0] &= ~val;
        sys_iomux_update_irq(s);
        return;
    case SYS_IOMUX_GPIOIC1:
        s->ris[1] &= ~val;
        sys_iomux_update_irq(s);
        return;
    case SYS_IOMUX_GPIOIBE0:
        s->ibe[0] = val;
        return;
    case SYS_IOMUX_GPIOIBE1:
        s->ibe[1] = val;
        return;
    case SYS_IOMUX_GPIOIEV0:
        s->iev[0] = val;
        return;
    case SYS_IOMUX_GPIOIEV1:
        s->iev[1] = val;
        return;
    case SYS_IOMUX_GPIOIE0:
        s->ie[0] = val;
        sys_iomux_update_irq(s);
        return;
    case SYS_IOMUX_GPIOIE1:
        s->ie[1] = val;
        sys_iomux_update_irq(s);
        return;
    default:
        /* FMUX, PADCFG, func_sel: flat storage */
        s->regs[addr >> 2] = val;
        return;
    }
}

static const MemoryRegionOps pz7110_sys_iomux_ops = {
    .read = pz7110_sys_iomux_read,
    .write = pz7110_sys_iomux_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void pz7110_sys_iomux_reset_enter(Object *obj, ResetType type)
{
    PZ7110SysIOMUXState *s = PZ7110_SYS_IOMUX(obj);
    memset(s->regs, 0, sizeof(s->regs));
    s->gpioen = 0;
    memset(s->is, 0, sizeof(s->is));
    memset(s->ibe, 0, sizeof(s->ibe));
    memset(s->iev, 0, sizeof(s->iev));
    memset(s->ie, 0, sizeof(s->ie));
    memset(s->ris, 0, sizeof(s->ris));
    memset(s->din, 0, sizeof(s->din));
}

static void pz7110_sys_iomux_init(Object *obj)
{
    PZ7110SysIOMUXState *s = PZ7110_SYS_IOMUX(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &pz7110_sys_iomux_ops,
                          s, "pz7110.sys-iomux",
                          PZ7110_SYS_IOMUX_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
}

static const VMStateDescription vmstate_pz7110_sys_iomux = {
    .name = TYPE_PZ7110_SYS_IOMUX,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, PZ7110SysIOMUXState,
                             PZ7110_SYS_IOMUX_SIZE / 4),
        VMSTATE_UINT32(gpioen, PZ7110SysIOMUXState),
        VMSTATE_UINT32_ARRAY(is, PZ7110SysIOMUXState, 2),
        VMSTATE_UINT32_ARRAY(ibe, PZ7110SysIOMUXState, 2),
        VMSTATE_UINT32_ARRAY(iev, PZ7110SysIOMUXState, 2),
        VMSTATE_UINT32_ARRAY(ie, PZ7110SysIOMUXState, 2),
        VMSTATE_UINT32_ARRAY(ris, PZ7110SysIOMUXState, 2),
        VMSTATE_UINT32_ARRAY(din, PZ7110SysIOMUXState, 2),
        VMSTATE_END_OF_LIST(),
    }
};

static void pz7110_sys_iomux_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PZ7110 SYS IOMUX + GPIO (64 GPIOs)";
    dc->vmsd = &vmstate_pz7110_sys_iomux;
    rc->phases.enter = pz7110_sys_iomux_reset_enter;
}

static const TypeInfo pz7110_sys_iomux_info = {
    .name          = TYPE_PZ7110_SYS_IOMUX,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110SysIOMUXState),
    .class_init    = pz7110_sys_iomux_class_init,
    .instance_init = pz7110_sys_iomux_init,
};

/* ================================================================
 * AON IOMUX + RGPIO (0x17020000, 4 RGPIOs)
 * ================================================================ */

static void aon_iomux_update_irq(PZ7110AONIOMUXState *s)
{
    uint32_t mis = s->ris & s->ie & (s->gpioen ? 0xFFFFFFFF : 0);
    qemu_set_irq(s->irq, mis ? 1 : 0);
}

static uint64_t pz7110_aon_iomux_read(void *opaque, hwaddr addr,
                                       unsigned int size)
{
    PZ7110AONIOMUXState *s = opaque;

    switch (addr) {
    case AON_IOMUX_GPIOEN:
        return s->gpioen;
    case AON_IOMUX_GPIOIS0:
        return s->is;
    case AON_IOMUX_GPIOIBE0:
        return s->ibe;
    case AON_IOMUX_GPIOIEV0:
        return s->iev;
    case AON_IOMUX_GPIOIE0:
        return s->ie;
    case AON_IOMUX_GPIORIS0:
        return s->ris;
    case AON_IOMUX_GPIOMIS0:
        return s->ris & s->ie & (s->gpioen ? 0xFFFFFFFF : 0);
    case AON_IOMUX_DIN0:
        return s->din;
    default:
        /* FMUX, PADCFG, GMAC config: flat storage */
        return s->regs[addr >> 2];
    }
}

static void pz7110_aon_iomux_write(void *opaque, hwaddr addr,
                                    uint64_t val64, unsigned int size)
{
    PZ7110AONIOMUXState *s = opaque;
    uint32_t val = (uint32_t)val64;

    switch (addr) {
    case AON_IOMUX_GPIOEN:
        s->gpioen = val & 1;
        aon_iomux_update_irq(s);
        return;
    case AON_IOMUX_GPIOIS0:
        s->is = val;
        return;
    case AON_IOMUX_GPIOIC0:
        s->ris &= ~val;
        aon_iomux_update_irq(s);
        return;
    case AON_IOMUX_GPIOIBE0:
        s->ibe = val;
        return;
    case AON_IOMUX_GPIOIEV0:
        s->iev = val;
        return;
    case AON_IOMUX_GPIOIE0:
        s->ie = val;
        aon_iomux_update_irq(s);
        return;
    default:
        /* FMUX, PADCFG, GMAC config: flat storage */
        s->regs[addr >> 2] = val;
        return;
    }
}

static const MemoryRegionOps pz7110_aon_iomux_ops = {
    .read = pz7110_aon_iomux_read,
    .write = pz7110_aon_iomux_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void pz7110_aon_iomux_reset_enter(Object *obj, ResetType type)
{
    PZ7110AONIOMUXState *s = PZ7110_AON_IOMUX(obj);
    memset(s->regs, 0, sizeof(s->regs));
    s->gpioen = 0;
    s->is = 0;
    s->ibe = 0;
    s->iev = 0;
    s->ie = 0;
    s->ris = 0;
    s->din = 0;
    /* DOEN default: 0x1010101 (all OEN = 1, i.e. input mode) */
    s->regs[0] = 0x01010101;
    /* SIG_IN default: wakeup 0-3 routed to GPIO2-5 */
    s->regs[2] = 0x05040302;
}

static void pz7110_aon_iomux_init(Object *obj)
{
    PZ7110AONIOMUXState *s = PZ7110_AON_IOMUX(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &pz7110_aon_iomux_ops,
                          s, "pz7110.aon-iomux",
                          PZ7110_AON_IOMUX_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
}

static const VMStateDescription vmstate_pz7110_aon_iomux = {
    .name = TYPE_PZ7110_AON_IOMUX,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, PZ7110AONIOMUXState,
                             PZ7110_AON_IOMUX_SIZE / 4),
        VMSTATE_UINT32(gpioen, PZ7110AONIOMUXState),
        VMSTATE_UINT32(is, PZ7110AONIOMUXState),
        VMSTATE_UINT32(ibe, PZ7110AONIOMUXState),
        VMSTATE_UINT32(iev, PZ7110AONIOMUXState),
        VMSTATE_UINT32(ie, PZ7110AONIOMUXState),
        VMSTATE_UINT32(ris, PZ7110AONIOMUXState),
        VMSTATE_UINT32(din, PZ7110AONIOMUXState),
        VMSTATE_END_OF_LIST(),
    }
};

static void pz7110_aon_iomux_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PZ7110 AON IOMUX + RGPIO (4 RGPIOs)";
    dc->vmsd = &vmstate_pz7110_aon_iomux;
    rc->phases.enter = pz7110_aon_iomux_reset_enter;
}

static const TypeInfo pz7110_aon_iomux_info = {
    .name          = TYPE_PZ7110_AON_IOMUX,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110AONIOMUXState),
    .class_init    = pz7110_aon_iomux_class_init,
    .instance_init = pz7110_aon_iomux_init,
};

/* ================================================================
 * Type registration
 * ================================================================ */

static void pz7110_iomux_register_types(void)
{
    type_register_static(&pz7110_sys_iomux_info);
    type_register_static(&pz7110_aon_iomux_info);
}

type_init(pz7110_iomux_register_types)
