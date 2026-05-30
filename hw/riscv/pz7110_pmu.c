/*
 * QEMU PZ7110 PMU power controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/riscv/pz7110_pmu.h"
#include "migration/vmstate.h"

#define PZ7110_PMU_SW_TURN_ON_POWER_MODE  0x0c
#define PZ7110_PMU_SW_TURN_OFF_POWER_MODE 0x10
#define PZ7110_PMU_CURR_POWER_MODE        0x80
#define PZ7110_PMU_EVENT_STATUS           0x88
#define PZ7110_PMU_INT_STATUS             0x8c

#define PZ7110_PMU_ALWAYS_ON_DOMAINS      0x3

static uint64_t pz7110_pmu_read(void *opaque, hwaddr addr, unsigned int size)
{
    PZ7110PmuState *s = opaque;

    switch (addr) {
    case PZ7110_PMU_CURR_POWER_MODE:
        return s->power_mode;
    case PZ7110_PMU_EVENT_STATUS:
    case PZ7110_PMU_INT_STATUS:
        return 0;
    default:
        return 0;
    }
}

static void pz7110_pmu_write(void *opaque, hwaddr addr, uint64_t value,
                             unsigned int size)
{
    PZ7110PmuState *s = opaque;
    uint32_t mask = value;

    switch (addr) {
    case PZ7110_PMU_SW_TURN_ON_POWER_MODE:
        s->power_mode |= mask;
        break;
    case PZ7110_PMU_SW_TURN_OFF_POWER_MODE:
        s->power_mode &= ~mask;
        /*
         * SYSTOP and CPU domains remain available in the QEMU model.
         * U-Boot/OpenSBI may touch reset paths after failed probes.
         */
        s->power_mode |= PZ7110_PMU_ALWAYS_ON_DOMAINS;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pz7110_pmu_ops = {
    .read = pz7110_pmu_read,
    .write = pz7110_pmu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void pz7110_pmu_reset(DeviceState *dev)
{
    PZ7110PmuState *s = PZ7110_PMU(dev);

    s->power_mode = PZ7110_PMU_ALWAYS_ON_DOMAINS;
}

static void pz7110_pmu_init(Object *obj)
{
    PZ7110PmuState *s = PZ7110_PMU(obj);

    memory_region_init_io(&s->mmio, obj, &pz7110_pmu_ops, s,
                          "pz7110.pmu", 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static const VMStateDescription vmstate_pz7110_pmu = {
    .name = TYPE_PZ7110_PMU,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(power_mode, PZ7110PmuState),
        VMSTATE_END_OF_LIST(),
    },
};

static void pz7110_pmu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, pz7110_pmu_reset);
    dc->vmsd = &vmstate_pz7110_pmu;
}

static const TypeInfo pz7110_pmu_info = {
    .name          = TYPE_PZ7110_PMU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110PmuState),
    .instance_init = pz7110_pmu_init,
    .class_init    = pz7110_pmu_class_init,
};

static void pz7110_pmu_register_types(void)
{
    type_register_static(&pz7110_pmu_info);
}

type_init(pz7110_pmu_register_types)
