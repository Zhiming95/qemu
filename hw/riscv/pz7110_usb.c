/*
 * QEMU PZ7110 Cadence USB3 Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/riscv/pz7110_usb.h"
#include "migration/vmstate.h"

static void pz7110_usb_update_irq(PZ7110UsbState *s)
{
    qemu_set_irq(s->irq, (s->irqstatus & s->irqenable) != 0);
}

static uint64_t pz7110_usb_read(void *opaque, hwaddr addr, unsigned int size)
{
    PZ7110UsbState *s = PZ7110_USB(opaque);

    switch (addr) {
    case USB_IRQSTATUS:
        return s->irqstatus;
    case USB_IRQENABLE:
        return s->irqenable;
    case USB_EPCSTATUS:
    case USB_EPCDATA:
    default:
        return 0;
    }
}

static void pz7110_usb_write(void *opaque, hwaddr addr, uint64_t value,
                             unsigned int size)
{
    PZ7110UsbState *s = PZ7110_USB(opaque);

    switch (addr) {
    case USB_IRQSTATUS:
        s->irqstatus &= ~value;
        pz7110_usb_update_irq(s);
        break;
    case USB_IRQENABLE:
        s->irqenable = value;
        pz7110_usb_update_irq(s);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pz7110_usb_ops = {
    .read = pz7110_usb_read,
    .write = pz7110_usb_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void pz7110_usb_reset(DeviceState *dev)
{
    PZ7110UsbState *s = PZ7110_USB(dev);

    s->irqstatus = 0;
    s->irqenable = 0;
}

static void pz7110_usb_init(Object *obj)
{
    PZ7110UsbState *s = PZ7110_USB(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    memory_region_init_io(&s->iomem, obj, &pz7110_usb_ops,
                          s, "pz7110-usb", 0x100000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription pz7110_usb_vmstate = {
    .name = TYPE_PZ7110_USB,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(irqstatus, PZ7110UsbState),
        VMSTATE_UINT32(irqenable, PZ7110UsbState),
        VMSTATE_END_OF_LIST()
    }
};

static void pz7110_usb_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, pz7110_usb_reset);
    dc->vmsd = &pz7110_usb_vmstate;
}

static const TypeInfo pz7110_usb_info = {
    .name = TYPE_PZ7110_USB,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110UsbState),
    .instance_init = pz7110_usb_init,
    .class_init = pz7110_usb_class_init,
};

static void pz7110_usb_register_types(void)
{
    type_register_static(&pz7110_usb_info);
}

type_init(pz7110_usb_register_types)
