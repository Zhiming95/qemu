/*
 * QEMU PZ7110 cache controller model
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/riscv/pz7110_ccache.h"
#include "migration/vmstate.h"

static uint64_t pz7110_ccache_read(void *opaque, hwaddr addr, unsigned size)
{
    PZ7110CcacheState *s = opaque;

    switch (addr) {
    case 0x00: /* CONFIG: 1 bank, 16 ways, 2048 sets, 64-byte lines */
        return 0x060b1001;
    case 0x08: /* WAYENABLE: index of the largest enabled way */
        return s->wayenable;
    case 0x108: /* DirError correctable count */
    case 0x128: /* DirError uncorrectable count */
    case 0x148: /* DataError correctable count */
    case 0x168: /* DataError uncorrectable count */
        return 0;
    default:
        return 0;
    }
}

static void pz7110_ccache_write(void *opaque, hwaddr addr, uint64_t value,
                                unsigned size)
{
    PZ7110CcacheState *s = opaque;

    switch (addr) {
    case 0x08: /* WAYENABLE */
        s->wayenable = value & 0xf;
        break;
    case 0x200: /* FLUSH64 */
    case 0x240: /* FLUSH32 */
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pz7110_ccache_ops = {
    .read = pz7110_ccache_read,
    .write = pz7110_ccache_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static void pz7110_ccache_reset(DeviceState *dev)
{
    PZ7110CcacheState *s = PZ7110_CCACHE(dev);

    s->wayenable = 0xf;
}

static void pz7110_ccache_init(Object *obj)
{
    PZ7110CcacheState *s = PZ7110_CCACHE(obj);

    memory_region_init_io(&s->mmio, obj, &pz7110_ccache_ops, s,
                          "pz7110.ccache", 0x40000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static const VMStateDescription vmstate_pz7110_ccache = {
    .name = TYPE_PZ7110_CCACHE,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(wayenable, PZ7110CcacheState),
        VMSTATE_END_OF_LIST(),
    },
};

static void pz7110_ccache_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, pz7110_ccache_reset);
    dc->vmsd = &vmstate_pz7110_ccache;
}

static const TypeInfo pz7110_ccache_info = {
    .name          = TYPE_PZ7110_CCACHE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110CcacheState),
    .instance_init = pz7110_ccache_init,
    .class_init    = pz7110_ccache_class_init,
};

static void pz7110_ccache_register_types(void)
{
    type_register_static(&pz7110_ccache_info);
}

type_init(pz7110_ccache_register_types)
