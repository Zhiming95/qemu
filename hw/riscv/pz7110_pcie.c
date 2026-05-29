/*
 * QEMU PZ7110 PLDA XpressRICH3 PCIe Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/riscv/pz7110_pcie.h"
#include "migration/vmstate.h"

static uint64_t pz7110_pcie_read(void *opaque, hwaddr addr, unsigned int size)
{
    PZ7110PcieState *s = PZ7110_PCIE(opaque);

    switch (addr) {
    case PCIE_APB_ID:
        return 0x00000001;
    case PCIE_APB_CLASS:
        return 0x00600000;
    case PCIE_APB_STATUS:
        return s->apb_status;
    case PCIE_APB_CTRL:
        return s->apb_ctrl;
    default:
        return 0;
    }
}

static void pz7110_pcie_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned int size)
{
    PZ7110PcieState *s = PZ7110_PCIE(opaque);

    switch (addr) {
    case PCIE_APB_STATUS:
        s->apb_status &= ~value;
        break;
    case PCIE_APB_CTRL:
        s->apb_ctrl = value;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pz7110_pcie_ops = {
    .read = pz7110_pcie_read,
    .write = pz7110_pcie_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void pz7110_pcie_reset(DeviceState *dev)
{
    PZ7110PcieState *s = PZ7110_PCIE(dev);

    s->apb_status = 0;
    s->apb_ctrl = 0;
}

static void pz7110_pcie_init(Object *obj)
{
    PZ7110PcieState *s = PZ7110_PCIE(obj);
    g_autofree char *apb_name = g_strdup_printf("pz7110-pcie-apb[%p]", obj);
    g_autofree char *cfg_name = g_strdup_printf("pz7110-pcie-cfg[%p]", obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    memory_region_init_io(&s->iomem, obj, &pz7110_pcie_ops,
                          s, apb_name, 0x100000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

    memory_region_init_ram(&s->cfg, obj, cfg_name, 0x1000000, &error_fatal);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->cfg);
}

static const VMStateDescription pz7110_pcie_vmstate = {
    .name = TYPE_PZ7110_PCIE,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(apb_status, PZ7110PcieState),
        VMSTATE_UINT32(apb_ctrl, PZ7110PcieState),
        VMSTATE_END_OF_LIST()
    }
};

static void pz7110_pcie_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, pz7110_pcie_reset);
    dc->vmsd = &pz7110_pcie_vmstate;
}

static const TypeInfo pz7110_pcie_info = {
    .name = TYPE_PZ7110_PCIE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110PcieState),
    .instance_init = pz7110_pcie_init,
    .class_init = pz7110_pcie_class_init,
};

static void pz7110_pcie_register_types(void)
{
    type_register_static(&pz7110_pcie_info);
}

type_init(pz7110_pcie_register_types)
