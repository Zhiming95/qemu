/*
 * QEMU PZ7110 StarFive RTC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/riscv/pz7110_rtc.h"
#include "migration/vmstate.h"
#include "qemu/bcd.h"
#include "system/rtc.h"

#define SFT_RTC_CFG          0x00
#define SFT_RTC_SW_CAL_VALUE 0x04
#define SFT_RTC_HW_CAL_CFG   0x08
#define SFT_RTC_CMP_CFG      0x0c
#define SFT_RTC_IRQ_EN       0x10
#define SFT_RTC_IRQ_EVEVT    0x14
#define SFT_RTC_IRQ_STATUS   0x18
#define SFT_RTC_CAL_VALUE    0x24
#define SFT_RTC_CFG_TIME     0x28
#define SFT_RTC_CFG_DATE     0x2c
#define SFT_RTC_ACT_TIME     0x34
#define SFT_RTC_ACT_DATE     0x38
#define SFT_RTC_TIME         0x3c
#define SFT_RTC_DATE         0x40
#define SFT_RTC_TIME_LATCH   0x44
#define SFT_RTC_DATE_LATCH   0x48

#define RTC_CFG_ENABLE       BIT(0)
#define RTC_CFG_HOUR_MODE    BIT(3)

#define RTC_IRQ_CAL_START    BIT(0)
#define RTC_IRQ_CAL_FINISH   BIT(1)
#define RTC_IRQ_CMP          BIT(2)
#define RTC_IRQ_1SEC         BIT(3)
#define RTC_IRQ_ALARM        BIT(4)
#define RTC_IRQ_ALL          (RTC_IRQ_CAL_START | RTC_IRQ_CAL_FINISH | \
                              RTC_IRQ_CMP | RTC_IRQ_1SEC | RTC_IRQ_ALARM)
#define RTC_IRQ_EVT_UPDATE   BIT(31)

static uint32_t pz7110_rtc_time_reg(void)
{
    struct tm tm;

    qemu_get_timedate(&tm, 0);

    return deposit32(0, 0, 7, to_bcd(tm.tm_sec)) |
           deposit32(0, 7, 7, to_bcd(tm.tm_min)) |
           deposit32(0, 14, 7, to_bcd(tm.tm_hour));
}

static uint32_t pz7110_rtc_date_reg(void)
{
    struct tm tm;

    qemu_get_timedate(&tm, 0);

    return deposit32(0, 0, 6, to_bcd(tm.tm_mday)) |
           deposit32(0, 6, 5, to_bcd(tm.tm_mon + 1)) |
           deposit32(0, 11, 8, to_bcd(tm.tm_year - 100));
}

static void pz7110_rtc_update_irq(PZ7110RtcState *s)
{
    qemu_set_irq(s->irq[2], (s->irq_event & s->irq_en & RTC_IRQ_ALL) != 0);
}

static uint64_t pz7110_rtc_read(void *opaque, hwaddr addr, unsigned int size)
{
    PZ7110RtcState *s = PZ7110_RTC(opaque);

    switch (addr) {
    case SFT_RTC_CFG:
        return s->cfg;
    case SFT_RTC_SW_CAL_VALUE:
        return s->sw_cal_value;
    case SFT_RTC_HW_CAL_CFG:
        return s->hw_cal_cfg;
    case SFT_RTC_CMP_CFG:
        return s->cmp_cfg;
    case SFT_RTC_IRQ_EN:
        return s->irq_en;
    case SFT_RTC_IRQ_EVEVT:
        return s->irq_event;
    case SFT_RTC_IRQ_STATUS:
        return s->irq_status;
    case SFT_RTC_CAL_VALUE:
        return s->cal_value;
    case SFT_RTC_CFG_TIME:
        return s->cfg_time;
    case SFT_RTC_CFG_DATE:
        return s->cfg_date;
    case SFT_RTC_ACT_TIME:
        return s->act_time;
    case SFT_RTC_ACT_DATE:
        return s->act_date;
    case SFT_RTC_TIME:
    case SFT_RTC_TIME_LATCH:
        return pz7110_rtc_time_reg();
    case SFT_RTC_DATE:
    case SFT_RTC_DATE_LATCH:
        return pz7110_rtc_date_reg();
    default:
        return 0;
    }
}

static void pz7110_rtc_write(void *opaque, hwaddr addr, uint64_t value,
                             unsigned int size)
{
    PZ7110RtcState *s = PZ7110_RTC(opaque);
    uint32_t val = value;

    switch (addr) {
    case SFT_RTC_CFG:
        s->cfg = val;
        break;
    case SFT_RTC_SW_CAL_VALUE:
        s->sw_cal_value = val & 0xffff;
        s->cal_value = s->sw_cal_value;
        break;
    case SFT_RTC_HW_CAL_CFG:
        s->hw_cal_cfg = val;
        break;
    case SFT_RTC_CMP_CFG:
        s->cmp_cfg = val;
        break;
    case SFT_RTC_IRQ_EN:
        s->irq_en = val & RTC_IRQ_ALL;
        pz7110_rtc_update_irq(s);
        break;
    case SFT_RTC_IRQ_EVEVT:
        if (val & RTC_IRQ_EVT_UPDATE) {
            s->irq_event |= RTC_IRQ_1SEC;
            s->irq_status |= RTC_IRQ_1SEC;
        }
        s->irq_event &= ~(val & RTC_IRQ_ALL);
        pz7110_rtc_update_irq(s);
        break;
    case SFT_RTC_IRQ_STATUS:
        s->irq_status &= ~(val & RTC_IRQ_ALL);
        break;
    case SFT_RTC_CFG_TIME:
        s->cfg_time = val;
        break;
    case SFT_RTC_CFG_DATE:
        s->cfg_date = val;
        break;
    case SFT_RTC_ACT_TIME:
        s->act_time = val;
        break;
    case SFT_RTC_ACT_DATE:
        s->act_date = val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pz7110_rtc_ops = {
    .read = pz7110_rtc_read,
    .write = pz7110_rtc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void pz7110_rtc_reset(DeviceState *dev)
{
    PZ7110RtcState *s = PZ7110_RTC(dev);

    s->cfg = RTC_CFG_ENABLE | RTC_CFG_HOUR_MODE;
    s->sw_cal_value = 0x7fff;
    s->hw_cal_cfg = 0;
    s->cmp_cfg = 0;
    s->irq_en = 0;
    s->irq_event = 0;
    s->irq_status = 0;
    s->cal_value = 0x7fff;
    s->cfg_time = pz7110_rtc_time_reg();
    s->cfg_date = pz7110_rtc_date_reg();
    s->act_time = 0;
    s->act_date = 0;
    pz7110_rtc_update_irq(s);
}

static void pz7110_rtc_init(Object *obj)
{
    PZ7110RtcState *s = PZ7110_RTC(obj);

    memory_region_init_io(&s->mmio, obj, &pz7110_rtc_ops, s,
                          "pz7110.rtc", 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    for (unsigned int i = 0; i < PZ7110_RTC_NUM_IRQS; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[i]);
    }
}

static const VMStateDescription vmstate_pz7110_rtc = {
    .name = TYPE_PZ7110_RTC,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cfg, PZ7110RtcState),
        VMSTATE_UINT32(sw_cal_value, PZ7110RtcState),
        VMSTATE_UINT32(hw_cal_cfg, PZ7110RtcState),
        VMSTATE_UINT32(cmp_cfg, PZ7110RtcState),
        VMSTATE_UINT32(irq_en, PZ7110RtcState),
        VMSTATE_UINT32(irq_event, PZ7110RtcState),
        VMSTATE_UINT32(irq_status, PZ7110RtcState),
        VMSTATE_UINT32(cal_value, PZ7110RtcState),
        VMSTATE_UINT32(cfg_time, PZ7110RtcState),
        VMSTATE_UINT32(cfg_date, PZ7110RtcState),
        VMSTATE_UINT32(act_time, PZ7110RtcState),
        VMSTATE_UINT32(act_date, PZ7110RtcState),
        VMSTATE_END_OF_LIST(),
    },
};

static void pz7110_rtc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, pz7110_rtc_reset);
    dc->vmsd = &vmstate_pz7110_rtc;
}

static const TypeInfo pz7110_rtc_info = {
    .name          = TYPE_PZ7110_RTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110RtcState),
    .instance_init = pz7110_rtc_init,
    .class_init    = pz7110_rtc_class_init,
};

static void pz7110_rtc_register_types(void)
{
    type_register_static(&pz7110_rtc_info);
}

type_init(pz7110_rtc_register_types)
