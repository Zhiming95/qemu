/*
 * PZ7110 IOMUX + GPIO definitions
 *
 * SYS IOMUX (0x13040000): FMUX pin mux + GPIO0-63 interrupt model
 * AON IOMUX (0x17020000): FMUX pin mux + RGPIO0-3 interrupt model
 *
 * Copyright (c) 2024 StarSix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#ifndef HW_RISCV_PZ7110_IOMUX_H
#define HW_RISCV_PZ7110_IOMUX_H

#include "hw/sysbus.h"
#include "hw/irq.h"
#include "qemu/typedefs.h"

/*
 * SYS IOMUX: base 0x13040000, size 0x10000
 * Contains FMUX registers + GPIO interrupt regs + PADCFG + func_sel
 *
 * Register layout (byte offsets):
 *   0x000-0x03C : FMUX DOEN  [0..15]   (16 regs, 4 GPIOs each, 6-bit fields)
 *   0x040-0x07C : FMUX DOUT  [0..15]   (16 regs, 4 GPIOs each, 7-bit fields)
 *   0x080-0x0D8 : FMUX SIG_IN[0..22]   (23 regs, 4 signals each, 7-bit fields)
 *   0x0DC       : GPIOEN                  (bit 0: global GPIO IRQ enable)
 *   0x0E0-0x11C : GPIO interrupt regs     (IS, IC, IBE, IEV, IE, RIS, MIS, DIN)
 *   0x120-0x21F : PADCFG [0..63]         (64 regs, 8-bit each, 4 bytes padded)
 *   0x220-0x29B : (reserved / padding)
 *   0x29C-0x78F : func_sel regs          (3-bit per GPIO, 10 GPIOs per reg)
 */
#define TYPE_PZ7110_SYS_IOMUX "pz7110.sys-iomux"
OBJECT_DECLARE_SIMPLE_TYPE(PZ7110SysIOMUXState, PZ7110_SYS_IOMUX)

#define PZ7110_SYS_IOMUX_SIZE    0x10000

/* Offsets for GPIO interrupt registers within SYS IOMUX */
#define SYS_IOMUX_GPIOEN         0x0DC
#define SYS_IOMUX_GPIOIS0        0x0E0
#define SYS_IOMUX_GPIOIS1        0x0E4
#define SYS_IOMUX_GPIOIC0        0x0E8
#define SYS_IOMUX_GPIOIC1        0x0EC
#define SYS_IOMUX_GPIOIBE0       0x0F0
#define SYS_IOMUX_GPIOIBE1       0x0F4
#define SYS_IOMUX_GPIOIEV0       0x0F8
#define SYS_IOMUX_GPIOIEV1       0x0FC
#define SYS_IOMUX_GPIOIE0        0x100
#define SYS_IOMUX_GPIOIE1        0x104
#define SYS_IOMUX_GPIORIS0       0x108
#define SYS_IOMUX_GPIORIS1       0x10C
#define SYS_IOMUX_GPIOMIS0       0x110
#define SYS_IOMUX_GPIOMIS1       0x114
#define SYS_IOMUX_DIN0           0x118
#define SYS_IOMUX_DIN1           0x11C

#define PZ7110_SYS_GPIO_COUNT    64

struct PZ7110SysIOMUXState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    /* FMUX + PADCFG + func_sel: flat storage, one uint32 per 4-byte slot */
    uint32_t regs[PZ7110_SYS_IOMUX_SIZE / 4];

    /* GPIO interrupt state (not memory-mapped directly, computed) */
    uint32_t gpioen;       /* Global GPIO IRQ enable */
    uint32_t is[2];        /* Interrupt sense: 1=edge, 0=level */
    uint32_t ibe[2];       /* Both-edge enable */
    uint32_t iev[2];       /* Event polarity */
    uint32_t ie[2];        /* Interrupt mask */
    uint32_t ris[2];       /* Raw interrupt status */
    uint32_t din[2];       /* GPIO data input (current pin state) */

    /* IRQ output to PLIC */
    qemu_irq irq;
};

/*
 * AON IOMUX: base 0x17020000, size 0x10000
 * Contains FMUX registers + RGPIO0-3 interrupt regs + PADCFG + GMAC config
 *
 * Register layout (byte offsets):
 *   0x000       : FMUX DOEN               (3-bit fields for RGPIO0-3)
 *   0x004       : FMUX DOUT               (4-bit fields for RGPIO0-3)
 *   0x008       : FMUX SIG_IN             (3-bit fields, wakeup routing)
 *   0x00C       : GPIOEN                  (bit 0: global RGPIO IRQ enable)
 *   0x010-0x02C : RGPIO interrupt regs    (IS, IC, IBE, IEV, IE, RIS, MIS, DIN)
 *   0x030-0x040 : PADCFG [0..3]           (4 regs for RGPIO0-3)
 *   0x044-0x7FF : (reserved / GMAC config / padding)
 */
#define TYPE_PZ7110_AON_IOMUX "pz7110.aon-iomux"
OBJECT_DECLARE_SIMPLE_TYPE(PZ7110AONIOMUXState, PZ7110_AON_IOMUX)

#define PZ7110_AON_IOMUX_SIZE   0x10000

/* Offsets for RGPIO interrupt registers within AON IOMUX */
#define AON_IOMUX_GPIOEN        0x00C
#define AON_IOMUX_GPIOIS0       0x010
#define AON_IOMUX_GPIOIC0       0x014
#define AON_IOMUX_GPIOIBE0      0x018
#define AON_IOMUX_GPIOIEV0      0x01C
#define AON_IOMUX_GPIOIE0       0x020
#define AON_IOMUX_GPIORIS0      0x024
#define AON_IOMUX_GPIOMIS0      0x028
#define AON_IOMUX_DIN0          0x02C

#define PZ7110_AON_GPIO_COUNT   4

struct PZ7110AONIOMUXState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    /* FMUX + PADCFG + GMAC config: flat storage */
    uint32_t regs[PZ7110_AON_IOMUX_SIZE / 4];

    /* RGPIO interrupt state */
    uint32_t gpioen;
    uint32_t is;
    uint32_t ibe;
    uint32_t iev;
    uint32_t ie;
    uint32_t ris;
    uint32_t din;

    /* IRQ output to PLIC */
    qemu_irq irq;
};

#endif
