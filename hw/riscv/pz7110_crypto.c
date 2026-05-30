/*
 * QEMU PZ7110 security engine stub
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/riscv/pz7110_crypto.h"
#include "migration/vmstate.h"

#define JH7110_CRYPTO_CACR 0x400
#define JH7110_CRYPTO_CASR 0x404

#define CRYPTO_CACR_START BIT(0)
#define CRYPTO_CACR_IE    BIT(2)
#define CRYPTO_CASR_DONE  BIT(0)

static uint64_t pz7110_crypto_read(void *opaque, hwaddr addr,
                                   unsigned int size)
{
    switch (addr) {
    case JH7110_CRYPTO_CASR:
        return CRYPTO_CASR_DONE;
    default:
        return 0;
    }
}

static void pz7110_crypto_write(void *opaque, hwaddr addr, uint64_t value,
                                unsigned int size)
{
    PZ7110CryptoState *s = opaque;

    if (addr == JH7110_CRYPTO_CACR &&
        (value & CRYPTO_CACR_START) &&
        (value & CRYPTO_CACR_IE)) {
        qemu_irq_pulse(s->secirq);
    }
}

static const MemoryRegionOps pz7110_crypto_ops = {
    .read = pz7110_crypto_read,
    .write = pz7110_crypto_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void pz7110_crypto_init(Object *obj)
{
    PZ7110CryptoState *s = PZ7110_CRYPTO(obj);

    memory_region_init_io(&s->mmio, obj, &pz7110_crypto_ops, s,
                          "pz7110.crypto", 0x4000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->secirq);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->dmairq);
}

static const VMStateDescription vmstate_pz7110_crypto = {
    .name = TYPE_PZ7110_CRYPTO,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_END_OF_LIST(),
    },
};

static void pz7110_crypto_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_pz7110_crypto;
}

static const TypeInfo pz7110_crypto_info = {
    .name          = TYPE_PZ7110_CRYPTO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110CryptoState),
    .instance_init = pz7110_crypto_init,
    .class_init    = pz7110_crypto_class_init,
};

static void pz7110_crypto_register_types(void)
{
    type_register_static(&pz7110_crypto_info);
}

type_init(pz7110_crypto_register_types)
