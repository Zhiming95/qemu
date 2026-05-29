/*
 * QEMU PZ7110 Synopsys DWMAC Ethernet Controller
 *
 * Copyright (c) 2024 StarSix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#include "qemu/osdep.h"
#include "hw/riscv/pz7110_gmac.h"
#include "hw/irq.h"
#include "hw/qdev-core.h"
#include "net/net.h"
#include "net/checksum.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "system/dma.h"

/*
 * DWMAC4 capabilities exposed to Linux.  Keep this conservative: only
 * advertise the media/MDIO features this model actually handles, and do not
 * expose checksum offload, timestamping, MMC counters, multi-queue, TSO, or
 * other features that are not emulated here.
 */
#define PZ7110_GMAC4_VERSION  0x00001020
#define PZ7110_GMAC_HW_FEAT0  (BIT(0) | BIT(1) | BIT(2) | BIT(5))
#define PZ7110_GMAC_HW_FEAT1  0
#define PZ7110_GMAC_HW_FEAT2  0
#define PZ7110_GMAC_HW_FEAT3  0

static void pz7110_gmac_update_irq(PZ7110GmacState *s)
{
    bool normal_enabled;
    bool tx_enabled;
    bool rx_enabled;
    bool level;

    /*
     * DWMAC4 channel status and interrupt-enable bits are not laid out as
     * identical masks: status.NIS is bit 15, while DWMAC4 enable.NIE is
     * bit 16.  Some later revisions use bit 15 for NIE, so accept both.
     */
    normal_enabled = (s->chan_status & DMA_ST_NIS) &&
                     (s->chan_int_en & (DMA_IE_NIE | DMA_IE_NIE_4_10));
    tx_enabled = (s->chan_status & DMA_ST_TI) &&
                 (s->chan_int_en & DMA_IE_TIE);
    rx_enabled = (s->chan_status & DMA_ST_RI) &&
                 (s->chan_int_en & DMA_IE_RIE);
    level = normal_enabled && (tx_enabled || rx_enabled);

    qemu_set_irq(s->irq, level);
}

static bool gmac4_read_desc(hwaddr addr, GMAC4Desc *desc)
{
    if (dma_memory_read(&address_space_memory, addr, desc,
                        sizeof(*desc), MEMTXATTRS_UNSPECIFIED)) {
        return false;
    }

    desc->des0 = le32_to_cpu(desc->des0);
    desc->des1 = le32_to_cpu(desc->des1);
    desc->des2 = le32_to_cpu(desc->des2);
    desc->des3 = le32_to_cpu(desc->des3);
    return true;
}

static bool gmac4_write_desc(hwaddr addr, const GMAC4Desc *desc)
{
    GMAC4Desc le_desc = {
        .des0 = cpu_to_le32(desc->des0),
        .des1 = cpu_to_le32(desc->des1),
        .des2 = cpu_to_le32(desc->des2),
        .des3 = cpu_to_le32(desc->des3),
    };

    return !dma_memory_write(&address_space_memory, addr, &le_desc,
                             sizeof(le_desc), MEMTXATTRS_UNSPECIFIED);
}

static uint32_t gmac4_ring_count(uint32_t reg)
{
    return (reg & DMA_RING_LEN_MASK) + 1;
}

static hwaddr gmac4_ring_next(hwaddr base, hwaddr current, uint32_t ring_count)
{
    uint32_t idx = 0;

    if (current >= base) {
        idx = (current - base) / GMAC4_DESC_SIZE;
        idx %= ring_count;
    }

    idx = (idx + 1) % ring_count;
    return base + idx * GMAC4_DESC_SIZE;
}

static hwaddr gmac4_buf_addr(const GMAC4Desc *desc)
{
    return ((hwaddr)desc->des1 << 32) | desc->des0;
}

static void pz7110_gmac_try_tx(PZ7110GmacState *s)
{
    uint8_t frame[2048];
    uint32_t frame_len = 0;
    uint32_t ring_count;
    hwaddr desc_addr;

    if (!s->nic || !(s->chan_tx_ctl & DMA_CTL_ST) || !s->chan_tx_addr) {
        return;
    }

    ring_count = gmac4_ring_count(s->chan_tx_ring_len);
    desc_addr = s->chan_cur_tx_desc ? s->chan_cur_tx_desc : s->chan_tx_addr;

    for (uint32_t i = 0; i < ring_count; i++) {
        GMAC4Desc desc;
        bool last;
        bool ic;
        uint32_t buf_len;

        if (!gmac4_read_desc(desc_addr, &desc)) {
            return;
        }

        if (!(desc.des3 & TDES3_OWN)) {
            return;
        }

        buf_len = desc.des2 & TDES2_B1SZ_MASK;
        if (buf_len && (desc.des0 || desc.des1)) {
            hwaddr buf_addr = gmac4_buf_addr(&desc);
            uint32_t copy_len = MIN(buf_len, sizeof(frame) - frame_len);

            if (copy_len &&
                !dma_memory_read(&address_space_memory, buf_addr,
                                 frame + frame_len, copy_len,
                                 MEMTXATTRS_UNSPECIFIED)) {
                frame_len += copy_len;
            }
        }

        last = desc.des3 & TDES3_LS;
        ic = desc.des2 & TDES2_IC;
        desc.des3 &= ~TDES3_OWN;
        if (!gmac4_write_desc(desc_addr, &desc)) {
            return;
        }

        desc_addr = gmac4_ring_next(s->chan_tx_addr, desc_addr, ring_count);
        s->chan_cur_tx_desc = desc_addr;

        if (ic) {
            s->chan_status |= DMA_ST_TI | DMA_ST_NIS;
            pz7110_gmac_update_irq(s);
        }

        if (last) {
            if (frame_len) {
                net_checksum_calculate(frame, frame_len, CSUM_ALL);
                qemu_send_packet(qemu_get_queue(s->nic), frame, frame_len);
            }
            return;
        }
    }
}

static uint32_t pz7110_gmac_rx_buf_size(PZ7110GmacState *s)
{
    return (s->chan_rx_ctl >> 1) & 0x3fff;
}

static bool pz7110_gmac_rx_desc_ready(PZ7110GmacState *s, hwaddr *desc_addr,
                                      GMAC4Desc *desc)
{
    hwaddr addr;

    if (!(s->chan_rx_ctl & DMA_CTL_SR) || !s->chan_rx_addr) {
        return false;
    }

    addr = s->chan_cur_rx_desc ? s->chan_cur_rx_desc : s->chan_rx_addr;
    if (!gmac4_read_desc(addr, desc)) {
        return false;
    }

    if (!(desc->des3 & RDES3_OWN) || !(desc->des3 & RDES3_BUF1V) ||
        !(desc->des0 || desc->des1)) {
        return false;
    }

    *desc_addr = addr;
    return true;
}

/* --- Register read/write --- */

static uint64_t pz7110_gmac_read(void *opaque, hwaddr addr, unsigned int size)
{
    PZ7110GmacState *s = PZ7110_GMAC(opaque);

    switch (addr) {
    /* MAC registers */
    case GMAC_CTRL:
        return s->mac_ctrl;
    case GMAC_FRAME_FMT:
        return s->frame_fmt;
    case GMAC_HASH_HIGH:
        return s->hash_high;
    case GMAC_HASH_LOW:
        return s->hash_low;
    case GMAC_MII_ADDR:
        return s->mii_addr;
    case GMAC_MII_DATA:
        return s->mii_data;
    case GMAC_FLOW_CTRL:
        return s->flow_ctrl;
    case GMAC_VLAN_TAG:
        return s->vlan_tag;
    case GMAC4_VERSION:
        return PZ7110_GMAC4_VERSION;
    case GMAC_HW_FEATURE0:
        return PZ7110_GMAC_HW_FEAT0;
    case GMAC_HW_FEATURE1:
        return PZ7110_GMAC_HW_FEAT1;
    case GMAC_HW_FEATURE2:
        return PZ7110_GMAC_HW_FEAT2;
    case GMAC_HW_FEATURE3:
        return PZ7110_GMAC_HW_FEAT3;

    /* Legacy global DMA registers */
    case DMA_BUS_MODE:
        return s->dma_bus_mode;
    case DMA_STATUS:
        return s->dma_status;
    case DMA_CTRL:
        return s->dma_ctrl;

    /* GMAC4/5 channel 0 DMA registers */
    case DMA_CHAN_CTRL(0):
        return s->chan_ctrl;
    case DMA_CHAN_TX_CTL(0):
        return s->chan_tx_ctl;
    case DMA_CHAN_RX_CTL(0):
        return s->chan_rx_ctl;
    case DMA_CHAN_TX_ADDR_HI(0):
        return s->chan_tx_addr_hi;
    case DMA_CHAN_TX_ADDR(0):
        return (uint32_t)s->chan_tx_addr;
    case DMA_CHAN_RX_ADDR_HI(0):
        return s->chan_rx_addr_hi;
    case DMA_CHAN_RX_ADDR(0):
        return (uint32_t)s->chan_rx_addr;
    case DMA_CHAN_TX_END(0):
        return s->chan_tx_end;
    case DMA_CHAN_RX_END(0):
        return s->chan_rx_end;
    case DMA_CHAN_INT_EN(0):
        return s->chan_int_en;
    case DMA_CHAN_CUR_TX_DESC(0):
        return (uint32_t)s->chan_cur_tx_desc;
    case DMA_CHAN_CUR_RX_DESC(0):
        return (uint32_t)s->chan_cur_rx_desc;
    case DMA_CHAN_STATUS(0):
        return s->chan_status;
    case DMA_CHAN_TX_RING_LEN(0):
        return s->chan_tx_ring_len;
    case DMA_CHAN_RX_RING_LEN(0):
        return s->chan_rx_ring_len;

    default:
        return 0;
    }
}

static void pz7110_gmac_write(void *opaque, hwaddr addr, uint64_t val64,
                               unsigned int size)
{
    PZ7110GmacState *s = PZ7110_GMAC(opaque);
    uint32_t val = (uint32_t)val64;

    switch (addr) {
    /* MAC registers */
    case GMAC_CTRL:
        s->mac_ctrl = val;
        break;
    case GMAC_FRAME_FMT:
        s->frame_fmt = val;
        break;
    case GMAC_HASH_HIGH:
        s->hash_high = val;
        break;
    case GMAC_HASH_LOW:
        s->hash_low = val;
        break;
    case GMAC_MII_ADDR:
        s->mii_addr = val;
        if (val & BIT(0)) { /* MII_BUSY — start transaction */
            bool c45 = val & BIT(1);
            bool is_read = (val & (3 << 2)) == (3 << 2);

            if (!c45) {
                /* Clause 22 */
                uint32_t pa = (val >> 21) & 0x1f;
                uint32_t reg = (val >> 16) & 0x1f;

                if (pa == s->phy_addr) {
                    if (is_read) {
                        switch (reg) {
                        case 0: /* BMCR */
                            s->mii_data = s->phy_bmcr;
                            break;
                        case 1: /* BMSR */
                            s->mii_data = 0x786D;
                            break;
                        case 2: /* PHYID1: valid, unmatched Clause 22 PHY */
                            s->mii_data = 0x1234;
                            break;
                        case 3: /* PHYID2 */
                            s->mii_data = 0x5678;
                            break;
                        case 4: /* ANAR */
                            s->mii_data = s->phy_anar;
                            break;
                        case 5: /* ANLPAR */
                            s->mii_data = 0x0DE1;
                            break;
                        case 10: /* 1000BASE-T status */
                            s->mii_data = 0x0800;
                            break;
                        case 17: /* Marvell 88E1111 PHY specific status */
                            s->mii_data = 0xAC00; /* 1G, full, resolved, link */
                            break;
                        default:
                            s->mii_data = 0;
                            break;
                        }
                    } else {
                        switch (reg) {
                        case 0: /* BMCR */
                            s->phy_bmcr = s->mii_data & ~BIT(15);
                            break;
                        case 4: /* ANAR */
                            s->phy_anar = s->mii_data;
                            break;
                        default: break;
                        }
                    }
                } else {
                    s->mii_data = 0xFFFF;
                }
            } else {
                /* Clause 45: not emulated */
                s->mii_data = 0xFFFF;
            }
            s->mii_addr &= ~BIT(0); /* clear busy */
            pz7110_gmac_update_irq(s);
        }
        break;
    case GMAC_MII_DATA:
        s->mii_data = val;
        break;
    case GMAC_FLOW_CTRL:
        s->flow_ctrl = val;
        break;
    case GMAC_VLAN_TAG:
        s->vlan_tag = val;
        break;

    /* Legacy global DMA registers */
    case DMA_BUS_MODE:
        s->dma_bus_mode = val;
        if (val & BIT(0)) {
            /* Software reset */
            s->dma_bus_mode = 0;
            s->dma_status = 0;
            s->dma_ctrl = 0;
        }
        break;
    case DMA_STATUS:
        /* Write 1 to clear */
        s->dma_status &= ~val;
        pz7110_gmac_update_irq(s);
        break;
    case DMA_CTRL:
        s->dma_ctrl = val;
        break;

    /* GMAC4/5 channel 0 DMA registers */
    case DMA_CHAN_CTRL(0):
        s->chan_ctrl = val;
        break;
    case DMA_CHAN_TX_CTL(0):
        s->chan_tx_ctl = val;
        break;
    case DMA_CHAN_RX_CTL(0):
        s->chan_rx_ctl = val;
        if (s->nic && (val & DMA_CTL_SR)) {
            qemu_flush_queued_packets(qemu_get_queue(s->nic));
        }
        break;
    case DMA_CHAN_TX_ADDR_HI(0):
        s->chan_tx_addr_hi = val;
        s->chan_tx_addr = deposit64(s->chan_tx_addr, 32, 32, val);
        s->chan_cur_tx_desc = s->chan_tx_addr;
        break;
    case DMA_CHAN_TX_ADDR(0):
        s->chan_tx_addr = deposit64(s->chan_tx_addr, 0, 32, val);
        s->chan_cur_tx_desc = s->chan_tx_addr;
        break;
    case DMA_CHAN_RX_ADDR_HI(0):
        s->chan_rx_addr_hi = val;
        s->chan_rx_addr = deposit64(s->chan_rx_addr, 32, 32, val);
        s->chan_cur_rx_desc = s->chan_rx_addr;
        break;
    case DMA_CHAN_RX_ADDR(0):
        s->chan_rx_addr = deposit64(s->chan_rx_addr, 0, 32, val);
        s->chan_cur_rx_desc = s->chan_rx_addr;
        break;
    case DMA_CHAN_TX_END(0):
        s->chan_tx_end = val;
        pz7110_gmac_try_tx(s);
        break;
    case DMA_CHAN_RX_END(0):
        s->chan_rx_end = val;
        if (s->nic) {
            qemu_flush_queued_packets(qemu_get_queue(s->nic));
        }
        break;
    case DMA_CHAN_INT_EN(0):
        s->chan_int_en = val;
        pz7110_gmac_update_irq(s);
        break;
    case DMA_CHAN_STATUS(0):
        /* Write-1-to-clear */
        s->chan_status &= ~val;
        pz7110_gmac_update_irq(s);
        break;
    case DMA_CHAN_TX_RING_LEN(0):
        s->chan_tx_ring_len = val;
        break;
    case DMA_CHAN_RX_RING_LEN(0):
        s->chan_rx_ring_len = val;
        break;
    case DMA_TX_POLL:
        pz7110_gmac_try_tx(s);
        break;
    case DMA_RX_POLL:
        if (s->nic) {
            qemu_flush_queued_packets(qemu_get_queue(s->nic));
        }
        break;

    /* Read-only channel registers */
    case DMA_CHAN_CUR_TX_DESC(0):
    case DMA_CHAN_CUR_RX_DESC(0):
        /* Ignored */
        break;

    default:
        break;
    }
}

static const MemoryRegionOps pz7110_gmac_ops = {
    .read = pz7110_gmac_read,
    .write = pz7110_gmac_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void pz7110_gmac_reset(DeviceState *dev)
{
    PZ7110GmacState *s = PZ7110_GMAC(dev);

    s->mac_ctrl = 0;
    s->frame_fmt = 0;
    s->hash_high = 0;
    s->hash_low = 0;
    s->mii_addr = 0;
    s->mii_data = 0;
    s->flow_ctrl = 0;
    s->vlan_tag = 0;
    s->dma_bus_mode = 0;
    s->dma_status = 0;
    s->dma_ctrl = 0;

    /* Channel DMA registers */
    s->chan_ctrl = 0;
    s->chan_tx_ctl = 0;
    s->chan_rx_ctl = 0;
    s->chan_tx_addr_hi = 0;
    s->chan_tx_addr = 0;
    s->chan_rx_addr_hi = 0;
    s->chan_rx_addr = 0;
    s->chan_tx_end = 0;
    s->chan_rx_end = 0;
    s->chan_tx_ring_len = 0;
    s->chan_rx_ring_len = 0;
    s->chan_int_en = 0;
    s->chan_cur_tx_desc = 0;
    s->chan_cur_rx_desc = 0;
    s->chan_status = 0;

    /* PHY defaults */
    s->phy_bmcr = 0x1000; /* AN enable */
    s->phy_anar = 0x0DE1; /* 100FD + 100HD + 10FD + 10HD + IEEE 802.3 */
}

static bool pz7110_gmac_can_receive(NetClientState *nc)
{
    PZ7110GmacState *s = PZ7110_GMAC(qemu_get_nic_opaque(nc));
    GMAC4Desc desc;
    hwaddr desc_addr;

    return pz7110_gmac_rx_desc_ready(s, &desc_addr, &desc);
}

static ssize_t pz7110_gmac_receive(NetClientState *nc, const uint8_t *buf,
                                   size_t size)
{
    PZ7110GmacState *s = PZ7110_GMAC(qemu_get_nic_opaque(nc));
    GMAC4Desc desc;
    hwaddr desc_addr;
    uint32_t buf_size = pz7110_gmac_rx_buf_size(s);
    uint32_t ring_count = gmac4_ring_count(s->chan_rx_ring_len);

    if (!pz7110_gmac_rx_desc_ready(s, &desc_addr, &desc)) {
        return -1;
    }

    if (!buf_size) {
        buf_size = 1536;
    }
    if (size > buf_size) {
        s->chan_status |= DMA_ST_RBU | DMA_ST_AIS;
        pz7110_gmac_update_irq(s);
        return -1;
    }

    if (dma_memory_write(&address_space_memory, gmac4_buf_addr(&desc), buf, size,
                         MEMTXATTRS_UNSPECIFIED)) {
        return -1;
    }

    desc.des2 = 0;
    /*
     * DWMAC4 reports RX frame length including Ethernet FCS. The stmmac
     * driver subtracts ETH_FCS_LEN before handing the packet to the stack.
     */
    desc.des3 = RDES3_FD | RDES3_LD | ((size + 4) & RDES3_FL_MASK);
    if (!gmac4_write_desc(desc_addr, &desc)) {
        return -1;
    }

    s->chan_cur_rx_desc = gmac4_ring_next(s->chan_rx_addr, desc_addr,
                                          ring_count);
    s->chan_status |= DMA_ST_RI | DMA_ST_NIS;
    pz7110_gmac_update_irq(s);
    return size;
}

static NetClientInfo net_pz7110_gmac_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = pz7110_gmac_can_receive,
    .receive = pz7110_gmac_receive,
};

static void pz7110_gmac_init(Object *obj)
{
    PZ7110GmacState *s = PZ7110_GMAC(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->iomem, obj, &pz7110_gmac_ops,
                          s, "pz7110-gmac", 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void pz7110_gmac_realize(DeviceState *dev, Error **errp)
{
    PZ7110GmacState *s = PZ7110_GMAC(dev);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_pz7110_gmac_info, &s->conf,
                          object_get_typename(OBJECT(dev)),
                          DEVICE(dev)->id,
                          &dev->mem_reentrancy_guard, dev);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
}

static const VMStateDescription pz7110_gmac_vmstate = {
    .name = TYPE_PZ7110_GMAC,
    .version_id = 2,
    .fields = (const VMStateField[]) {
        /* MAC registers */
        VMSTATE_UINT32(mac_ctrl, PZ7110GmacState),
        VMSTATE_UINT32(frame_fmt, PZ7110GmacState),
        VMSTATE_UINT32(hash_high, PZ7110GmacState),
        VMSTATE_UINT32(hash_low, PZ7110GmacState),
        VMSTATE_UINT32(mii_addr, PZ7110GmacState),
        VMSTATE_UINT32(mii_data, PZ7110GmacState),
        VMSTATE_UINT32(flow_ctrl, PZ7110GmacState),
        VMSTATE_UINT32(vlan_tag, PZ7110GmacState),
        /* Legacy DMA */
        VMSTATE_UINT32(dma_bus_mode, PZ7110GmacState),
        VMSTATE_UINT32(dma_status, PZ7110GmacState),
        VMSTATE_UINT32(dma_ctrl, PZ7110GmacState),
        /* Channel 0 DMA */
        VMSTATE_UINT32(chan_ctrl, PZ7110GmacState),
        VMSTATE_UINT32(chan_tx_ctl, PZ7110GmacState),
        VMSTATE_UINT32(chan_rx_ctl, PZ7110GmacState),
        VMSTATE_UINT32(chan_tx_addr_hi, PZ7110GmacState),
        VMSTATE_UINT64(chan_tx_addr, PZ7110GmacState),
        VMSTATE_UINT32(chan_rx_addr_hi, PZ7110GmacState),
        VMSTATE_UINT64(chan_rx_addr, PZ7110GmacState),
        VMSTATE_UINT32(chan_tx_end, PZ7110GmacState),
        VMSTATE_UINT32(chan_rx_end, PZ7110GmacState),
        VMSTATE_UINT32(chan_tx_ring_len, PZ7110GmacState),
        VMSTATE_UINT32(chan_rx_ring_len, PZ7110GmacState),
        VMSTATE_UINT32(chan_int_en, PZ7110GmacState),
        VMSTATE_UINT64(chan_cur_tx_desc, PZ7110GmacState),
        VMSTATE_UINT64(chan_cur_rx_desc, PZ7110GmacState),
        VMSTATE_UINT32(chan_status, PZ7110GmacState),
        /* PHY */
        VMSTATE_UINT32(phy_addr, PZ7110GmacState),
        VMSTATE_UINT32(phy_bmcr, PZ7110GmacState),
        VMSTATE_UINT32(phy_anar, PZ7110GmacState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property pz7110_gmac_properties[] = {
    DEFINE_NIC_PROPERTIES(PZ7110GmacState, conf),
};

static void pz7110_gmac_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pz7110_gmac_realize;
    device_class_set_legacy_reset(dc, pz7110_gmac_reset);
    dc->vmsd = &pz7110_gmac_vmstate;
    device_class_set_props(dc, pz7110_gmac_properties);
}

static const TypeInfo pz7110_gmac_info = {
    .name          = TYPE_PZ7110_GMAC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110GmacState),
    .instance_init = pz7110_gmac_init,
    .class_init    = pz7110_gmac_class_init,
};

static void pz7110_gmac_register_types(void)
{
    type_register_static(&pz7110_gmac_info);
}

type_init(pz7110_gmac_register_types)
