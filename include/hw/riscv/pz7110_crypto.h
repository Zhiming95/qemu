/*
 * QEMU PZ7110 security engine stub
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_PZ7110_CRYPTO_H
#define HW_RISCV_PZ7110_CRYPTO_H

#include "hw/sysbus.h"

#define TYPE_PZ7110_CRYPTO "pz7110-crypto"
OBJECT_DECLARE_SIMPLE_TYPE(PZ7110CryptoState, PZ7110_CRYPTO)

struct PZ7110CryptoState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq secirq;
    qemu_irq dmairq;
};

#endif
