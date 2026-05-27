/*
 * QEMU PZ7110 DesignWare AXI DMA Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/riscv/pz7110_dma.h"
#include "migration/vmstate.h"
#include "qapi/error.h"

static void pz7110_dma_update_irq(PZ7110DmaState *s)
{
    qemu_set_irq(s->irq, (s->intstatus & s->intsignal_ena) != 0);
}

static uint64_t pz7110_dma_read(void *opaque, hwaddr addr, unsigned int size)
{
    PZ7110DmaState *s = PZ7110_DMA(opaque);
    int ch;
    uint32_t offset;

    if (addr < PZ7110_DMA_NUM_CHANNELS * CHAN_REG_LEN) {
        switch (addr) {
        case DMAC_ID:
            return s->dmac_id;
        case DMAC_COMPVER:
            return s->dmac_compver;
        case DMAC_CFG:
            return s->dmac_cfg;
        case DMAC_CHEN:
            return s->chen;
        case DMAC_CHSUSPREG:
            return s->chsuspend;
        case DMAC_INTSTATUS:
            return s->intstatus;
        case DMAC_INTSTATUS_ENA:
            return s->intstatus_ena;
        case DMAC_INTSIGNAL_ENA:
            return s->intsignal_ena;
        case DMAC_COMMON_INTSTATUS:
            return s->common_intstatus;
        case DMAC_RESET:
            return 0;
        default:
            return 0;
        }
    }

    ch = (addr - PZ7110_DMA_NUM_CHANNELS * CHAN_REG_LEN) / CHAN_REG_LEN;
    offset = addr % CHAN_REG_LEN;

    if (ch >= PZ7110_DMA_NUM_CHANNELS) {
        return 0;
    }

    switch (offset) {
    case CH_SAR:
        return s->channels[ch].sar;
    case CH_DAR:
        return s->channels[ch].dar;
    case CH_BLOCK_TS:
        return s->channels[ch].block_ts;
    case CH_CTL:
        return s->channels[ch].ctl;
    case CH_CFG:
        return s->channels[ch].cfg;
    case CH_LLP:
        return s->channels[ch].llp;
    case CH_STATUS:
        return s->channels[ch].status;
    default:
        return 0;
    }
}

static void pz7110_dma_write(void *opaque, hwaddr addr, uint64_t val64,
                             unsigned int size)
{
    PZ7110DmaState *s = PZ7110_DMA(opaque);
    uint32_t val = val64;
    int ch;
    uint32_t offset;

    if (addr < PZ7110_DMA_NUM_CHANNELS * CHAN_REG_LEN) {
        switch (addr) {
        case DMAC_CFG:
            s->dmac_cfg = val;
            break;
        case DMAC_CHEN:
            s->chen = val;
            break;
        case DMAC_CHSUSPREG:
            s->chsuspend = val;
            break;
        case DMAC_INTSTATUS:
            s->intstatus &= ~val;
            pz7110_dma_update_irq(s);
            break;
        case DMAC_INTSTATUS_ENA:
            s->intstatus_ena = val;
            break;
        case DMAC_INTSIGNAL_ENA:
            s->intsignal_ena = val;
            pz7110_dma_update_irq(s);
            break;
        case DMAC_RESET:
            if (val & 1) {
                memset(s->channels, 0, sizeof(s->channels));
                s->chen = 0;
                s->chsuspend = 0;
                s->intstatus = 0;
                pz7110_dma_update_irq(s);
            }
            break;
        default:
            break;
        }
        return;
    }

    ch = (addr - PZ7110_DMA_NUM_CHANNELS * CHAN_REG_LEN) / CHAN_REG_LEN;
    offset = addr % CHAN_REG_LEN;

    if (ch >= PZ7110_DMA_NUM_CHANNELS) {
        return;
    }

    switch (offset) {
    case CH_SAR:
        s->channels[ch].sar = val;
        break;
    case CH_DAR:
        s->channels[ch].dar = val;
        break;
    case CH_BLOCK_TS:
        s->channels[ch].block_ts = val;
        break;
    case CH_CTL:
        s->channels[ch].ctl = val;
        break;
    case CH_CFG:
        s->channels[ch].cfg = val;
        break;
    case CH_LLP:
        s->channels[ch].llp = val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pz7110_dma_ops = {
    .read = pz7110_dma_read,
    .write = pz7110_dma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void pz7110_dma_reset(DeviceState *dev)
{
    PZ7110DmaState *s = PZ7110_DMA(dev);

    s->dmac_id = 0x040000a0;
    s->dmac_compver = 0x3232322a;
    s->dmac_cfg = 0;
    s->chen = 0;
    s->chsuspend = 0;
    s->intstatus = 0;
    s->intstatus_ena = 0;
    s->intsignal_ena = 0;
    s->common_intstatus = 0;
    memset(s->channels, 0, sizeof(s->channels));
    pz7110_dma_update_irq(s);
}

static void pz7110_dma_init(Object *obj)
{
    PZ7110DmaState *s = PZ7110_DMA(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    memory_region_init_io(&s->iomem, obj, &pz7110_dma_ops,
                          s, "pz7110-dma", 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription pz7110_dma_channel_vmstate = {
    .name = "pz7110_dma_channel",
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(sar, PZ7110DmaChannel),
        VMSTATE_UINT32(dar, PZ7110DmaChannel),
        VMSTATE_UINT32(block_ts, PZ7110DmaChannel),
        VMSTATE_UINT32(ctl, PZ7110DmaChannel),
        VMSTATE_UINT32(cfg, PZ7110DmaChannel),
        VMSTATE_UINT32(llp, PZ7110DmaChannel),
        VMSTATE_UINT32(status, PZ7110DmaChannel),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription pz7110_dma_vmstate = {
    .name = TYPE_PZ7110_DMA,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(dmac_id, PZ7110DmaState),
        VMSTATE_UINT32(dmac_compver, PZ7110DmaState),
        VMSTATE_UINT32(dmac_cfg, PZ7110DmaState),
        VMSTATE_UINT32(chen, PZ7110DmaState),
        VMSTATE_UINT32(chsuspend, PZ7110DmaState),
        VMSTATE_UINT32(intstatus, PZ7110DmaState),
        VMSTATE_UINT32(intstatus_ena, PZ7110DmaState),
        VMSTATE_UINT32(intsignal_ena, PZ7110DmaState),
        VMSTATE_UINT32(common_intstatus, PZ7110DmaState),
        VMSTATE_STRUCT_ARRAY(channels, PZ7110DmaState,
                             PZ7110_DMA_NUM_CHANNELS, 1,
                             pz7110_dma_channel_vmstate,
                             PZ7110DmaChannel),
        VMSTATE_END_OF_LIST()
    }
};

static void pz7110_dma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, pz7110_dma_reset);
    dc->vmsd = &pz7110_dma_vmstate;
}

static const TypeInfo pz7110_dma_info = {
    .name          = TYPE_PZ7110_DMA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110DmaState),
    .instance_init = pz7110_dma_init,
    .class_init    = pz7110_dma_class_init,
};

static void pz7110_dma_register_types(void)
{
    type_register_static(&pz7110_dma_info);
}

type_init(pz7110_dma_register_types)
