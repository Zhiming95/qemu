/*
 * QEMU PZ7110 StarFive TRNG
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/riscv/pz7110_trng.h"
#include "migration/vmstate.h"
#include "qemu/guest-random.h"

#define PZ7110_TRNG_CTRL      0x00
#define PZ7110_TRNG_STAT      0x04
#define PZ7110_TRNG_MODE      0x08
#define PZ7110_TRNG_SMODE     0x0c
#define PZ7110_TRNG_IE        0x10
#define PZ7110_TRNG_ISTAT     0x14
#define PZ7110_TRNG_RAND0     0x20
#define PZ7110_TRNG_RAND7     0x3c
#define PZ7110_TRNG_AUTO_RQSTS 0x60
#define PZ7110_TRNG_AUTO_AGE  0x64

#define PZ7110_TRNG_CTRL_RANDNUM BIT(0)
#define PZ7110_TRNG_CTRL_RESEED  BIT(1)
#define PZ7110_TRNG_STAT_R256    BIT(3)
#define PZ7110_TRNG_STAT_MISSION BIT(8)
#define PZ7110_TRNG_STAT_SEEDED  BIT(9)
#define PZ7110_TRNG_IE_RAND_RDY  BIT(0)
#define PZ7110_TRNG_IE_SEED_DONE BIT(1)
#define PZ7110_TRNG_IE_GLOBAL    BIT(31)
#define PZ7110_TRNG_ISTAT_RAND_RDY  BIT(0)
#define PZ7110_TRNG_ISTAT_SEED_DONE BIT(1)

static void pz7110_trng_update_irq(PZ7110TrngState *s)
{
    bool enabled = s->ie & PZ7110_TRNG_IE_GLOBAL;
    bool pending = ((s->istat & PZ7110_TRNG_ISTAT_RAND_RDY) &&
                    (s->ie & PZ7110_TRNG_IE_RAND_RDY)) ||
                   ((s->istat & PZ7110_TRNG_ISTAT_SEED_DONE) &&
                    (s->ie & PZ7110_TRNG_IE_SEED_DONE));

    qemu_set_irq(s->irq, enabled && pending);
}

static uint64_t pz7110_trng_read(void *opaque, hwaddr addr, unsigned int size)
{
    PZ7110TrngState *s = PZ7110_TRNG(opaque);

    switch (addr) {
    case PZ7110_TRNG_CTRL:
        return s->ctrl;
    case PZ7110_TRNG_STAT:
        return s->stat;
    case PZ7110_TRNG_MODE:
        return s->mode;
    case PZ7110_TRNG_SMODE:
        return s->smode;
    case PZ7110_TRNG_IE:
        return s->ie;
    case PZ7110_TRNG_ISTAT:
        return s->istat;
    case PZ7110_TRNG_AUTO_RQSTS:
        return s->auto_rqsts;
    case PZ7110_TRNG_AUTO_AGE:
        return s->auto_age;
    default:
        if (addr >= PZ7110_TRNG_RAND0 && addr <= PZ7110_TRNG_RAND7) {
            uint64_t val;

            qemu_guest_getrandom_nofail(&val, sizeof(val));
            return val;
        }
        return 0;
    }
}

static void pz7110_trng_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned int size)
{
    PZ7110TrngState *s = PZ7110_TRNG(opaque);

    switch (addr) {
    case PZ7110_TRNG_CTRL:
        s->ctrl = value;
        if (value & PZ7110_TRNG_CTRL_RESEED) {
            s->stat |= PZ7110_TRNG_STAT_SEEDED |
                       PZ7110_TRNG_STAT_MISSION |
                       PZ7110_TRNG_STAT_R256;
            s->istat |= PZ7110_TRNG_ISTAT_SEED_DONE;
        }
        if (value & PZ7110_TRNG_CTRL_RANDNUM) {
            s->istat |= PZ7110_TRNG_ISTAT_RAND_RDY;
        }
        pz7110_trng_update_irq(s);
        break;
    case PZ7110_TRNG_MODE:
        s->mode = value;
        if (value & PZ7110_TRNG_STAT_R256) {
            s->stat |= PZ7110_TRNG_STAT_R256;
        } else {
            s->stat &= ~PZ7110_TRNG_STAT_R256;
        }
        break;
    case PZ7110_TRNG_SMODE:
        s->smode = value;
        break;
    case PZ7110_TRNG_IE:
        s->ie = value;
        pz7110_trng_update_irq(s);
        break;
    case PZ7110_TRNG_ISTAT:
        s->istat &= ~value;
        pz7110_trng_update_irq(s);
        break;
    case PZ7110_TRNG_AUTO_RQSTS:
        s->auto_rqsts = value;
        break;
    case PZ7110_TRNG_AUTO_AGE:
        s->auto_age = value;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pz7110_trng_ops = {
    .read = pz7110_trng_read,
    .write = pz7110_trng_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static void pz7110_trng_reset(DeviceState *dev)
{
    PZ7110TrngState *s = PZ7110_TRNG(dev);

    s->ctrl = 0;
    s->stat = PZ7110_TRNG_STAT_R256 | PZ7110_TRNG_STAT_MISSION |
              PZ7110_TRNG_STAT_SEEDED;
    s->mode = PZ7110_TRNG_STAT_R256;
    s->smode = PZ7110_TRNG_STAT_MISSION;
    s->ie = 0;
    s->istat = 0;
    s->auto_rqsts = 0;
    s->auto_age = 0;
    pz7110_trng_update_irq(s);
}

static void pz7110_trng_init(Object *obj)
{
    PZ7110TrngState *s = PZ7110_TRNG(obj);

    memory_region_init_io(&s->mmio, obj, &pz7110_trng_ops, s,
                          "pz7110.trng", 0x4000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static const VMStateDescription vmstate_pz7110_trng = {
    .name = TYPE_PZ7110_TRNG,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ctrl, PZ7110TrngState),
        VMSTATE_UINT32(stat, PZ7110TrngState),
        VMSTATE_UINT32(mode, PZ7110TrngState),
        VMSTATE_UINT32(smode, PZ7110TrngState),
        VMSTATE_UINT32(ie, PZ7110TrngState),
        VMSTATE_UINT32(istat, PZ7110TrngState),
        VMSTATE_UINT32(auto_rqsts, PZ7110TrngState),
        VMSTATE_UINT32(auto_age, PZ7110TrngState),
        VMSTATE_END_OF_LIST(),
    },
};

static void pz7110_trng_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, pz7110_trng_reset);
    dc->vmsd = &vmstate_pz7110_trng;
}

static const TypeInfo pz7110_trng_info = {
    .name          = TYPE_PZ7110_TRNG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110TrngState),
    .instance_init = pz7110_trng_init,
    .class_init    = pz7110_trng_class_init,
};

static void pz7110_trng_register_types(void)
{
    type_register_static(&pz7110_trng_info);
}

type_init(pz7110_trng_register_types)
