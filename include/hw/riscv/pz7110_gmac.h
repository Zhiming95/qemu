/*
 * QEMU PZ7110 Synopsys DWMAC Ethernet Controller
 *
 * Copyright (c) 2024 StarSix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#ifndef HW_RISCV_PZ7110_GMAC_H
#define HW_RISCV_PZ7110_GMAC_H

#include "hw/sysbus.h"
#include "net/net.h"

#define TYPE_PZ7110_GMAC "pz7110-gmac"
OBJECT_DECLARE_SIMPLE_TYPE(PZ7110GmacState, PZ7110_GMAC)

/* DWMAC GMAC4/5 register offsets */
#define GMAC_CTRL       0x0000  /* MAC configuration */
#define GMAC_FRAME_FMT  0x0004  /* MAC frame filter */
#define GMAC_HASH_HIGH  0x0008  /* Hash table high */
#define GMAC_HASH_LOW   0x000C  /* Hash table low */
#define GMAC_MII_ADDR   0x0200  /* MDIO address (GMAC4/5) */
#define GMAC_MII_DATA   0x0204  /* MDIO data (GMAC4/5) */
#define GMAC_FLOW_CTRL  0x0018  /* Flow control */
#define GMAC_VLAN_TAG   0x001C  /* VLAN tag */
#define GMAC4_VERSION   0x0110  /* GMAC4+ core version */
#define GMAC_HW_FEATURE0 0x011C /* GMAC4+ HW feature 0 */
#define GMAC_HW_FEATURE1 0x0120 /* GMAC4+ HW feature 1 */
#define GMAC_HW_FEATURE2 0x0124 /* GMAC4+ HW feature 2 */
#define GMAC_HW_FEATURE3 0x0128 /* GMAC4+ HW feature 3 */

/* Global DMA registers (GMAC4/5) */
#define DMA_BUS_MODE    0x1000  /* DMA bus mode */
#define DMA_TX_POLL     0x1004  /* TX poll demand (write-only) */
#define DMA_RX_POLL     0x1008  /* RX poll demand (write-only) */
#define DMA_STATUS      0x1014  /* DMA status (GMAC4/5 global) */
#define DMA_CTRL        0x1018  /* DMA control */
#define DMA_INTR_ENA    0x101C  /* DMA interrupt enable */
#define DMA_MISS_FRAME  0x1020  /* Missed frame counter */

/* GMAC4/5 channel-based DMA registers (channel 0 at 0x1100) */
#define DMA_CHAN_BASE        0x1100
#define DMA_CHAN_OFFSET      0x80
#define DMA_CHAN_CTRL(n)     (DMA_CHAN_BASE + (n) * DMA_CHAN_OFFSET + 0x00)
#define DMA_CHAN_TX_CTL(n)   (DMA_CHAN_BASE + (n) * DMA_CHAN_OFFSET + 0x04)
#define DMA_CHAN_RX_CTL(n)   (DMA_CHAN_BASE + (n) * DMA_CHAN_OFFSET + 0x08)
#define DMA_CHAN_TX_ADDR_HI(n) (DMA_CHAN_BASE + (n) * DMA_CHAN_OFFSET + 0x10)
#define DMA_CHAN_TX_ADDR(n)  (DMA_CHAN_BASE + (n) * DMA_CHAN_OFFSET + 0x14)
#define DMA_CHAN_RX_ADDR_HI(n) (DMA_CHAN_BASE + (n) * DMA_CHAN_OFFSET + 0x18)
#define DMA_CHAN_RX_ADDR(n)  (DMA_CHAN_BASE + (n) * DMA_CHAN_OFFSET + 0x1c)
#define DMA_CHAN_TX_END(n)   (DMA_CHAN_BASE + (n) * DMA_CHAN_OFFSET + 0x20)
#define DMA_CHAN_RX_END(n)   (DMA_CHAN_BASE + (n) * DMA_CHAN_OFFSET + 0x28)
#define DMA_CHAN_TX_RING_LEN(n) (DMA_CHAN_BASE + (n) * DMA_CHAN_OFFSET + 0x2c)
#define DMA_CHAN_RX_RING_LEN(n) (DMA_CHAN_BASE + (n) * DMA_CHAN_OFFSET + 0x30)
#define DMA_CHAN_INT_EN(n)   (DMA_CHAN_BASE + (n) * DMA_CHAN_OFFSET + 0x34)
#define DMA_CHAN_CUR_TX_DESC(n) (DMA_CHAN_BASE + (n) * DMA_CHAN_OFFSET + 0x44)
#define DMA_CHAN_CUR_RX_DESC(n) (DMA_CHAN_BASE + (n) * DMA_CHAN_OFFSET + 0x4c)
#define DMA_CHAN_CUR_TX_BUF(n)  (DMA_CHAN_BASE + (n) * DMA_CHAN_OFFSET + 0x54)
#define DMA_CHAN_CUR_RX_BUF(n)  (DMA_CHAN_BASE + (n) * DMA_CHAN_OFFSET + 0x5c)
#define DMA_CHAN_STATUS(n)   (DMA_CHAN_BASE + (n) * DMA_CHAN_OFFSET + 0x60)

/* DMA channel status bits */
#define DMA_ST_TI    BIT(0)   /* Transmit interrupt */
#define DMA_ST_TPS   BIT(1)   /* Transmit process stopped */
#define DMA_ST_TBU   BIT(2)   /* TX buffer unavailable */
#define DMA_ST_RI    BIT(6)   /* Receive interrupt */
#define DMA_ST_RBU   BIT(7)   /* RX buffer unavailable */
#define DMA_ST_RPS   BIT(8)   /* Receive process stopped */
#define DMA_ST_NIS   BIT(15)  /* Normal interrupt summary */
#define DMA_ST_AIS   BIT(14)  /* Abnormal interrupt summary */

/* DMA control bits (TX_CTL / RX_CTL / CHAN_CTRL) */
#define DMA_CTL_ST   BIT(0)   /* Start/stop TX */
#define DMA_CTL_SR   BIT(0)   /* Start/stop RX */

typedef struct GMAC4Desc {
    uint32_t des0;
    uint32_t des1;
    uint32_t des2;
    uint32_t des3;
} GMAC4Desc;

#define GMAC4_DESC_SIZE          16
#define DMA_RING_LEN_MASK        0x3fff

/* GMAC4 TX descriptor, read format */
#define TDES2_B1SZ_MASK          0x3fff
#define TDES2_IC                 BIT(31)
#define TDES3_PKT_SIZE_MASK      0x7fff
#define TDES3_LS                 BIT(28)
#define TDES3_FS                 BIT(29)
#define TDES3_OWN                BIT(31)

/* GMAC4 RX descriptor */
#define RDES3_FL_MASK            0x7fff
#define RDES3_BUF1V              BIT(24)
#define RDES3_LD                 BIT(28)
#define RDES3_FD                 BIT(29)
#define RDES3_IOC                BIT(30)
#define RDES3_OWN                BIT(31)

/* MAC Configuration Register bits */
#define GMAC_CTRL_RE    BIT(0)  /* Receiver enable */
#define GMAC_CTRL_TE    BIT(3)  /* Transmitter enable */
#define GMAC_CTRL_PRE   BIT(6)  /* Packet preamble length */
#define GMAC_CTRL_DO    BIT(7)  /* Duplex mode */

/* Legacy DMA Status bits (GMAC3 offsets, kept for compat) */
#define DMA_STATUS_TI   BIT(0)  /* Transmit interrupt */
#define DMA_STATUS_RI   BIT(6)  /* Receive interrupt */
#define DMA_STATUS_NIS  BIT(15) /* Normal interrupt summary */
#define DMA_STATUS_AIS  BIT(16) /* Abnormal interrupt summary */

typedef struct PZ7110GmacState {
    /*< private >*/
    SysBusDevice parent;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq     irq;
    NICState    *nic;
    NICConf      conf;

    /* MAC registers */
    uint32_t mac_ctrl;
    uint32_t frame_fmt;
    uint32_t hash_high;
    uint32_t hash_low;
    uint32_t mii_addr;
    uint32_t mii_data;
    uint32_t flow_ctrl;
    uint32_t vlan_tag;

    /* Legacy global DMA registers (GMAC3 offsets) */
    uint32_t dma_bus_mode;
    uint32_t dma_status;
    uint32_t dma_ctrl;

    /* GMAC4/5 channel 0 DMA registers */
    uint32_t chan_ctrl;        /* 0x1100 */
    uint32_t chan_tx_ctl;      /* 0x1104 */
    uint32_t chan_rx_ctl;      /* 0x1108 */
    uint32_t chan_tx_addr_hi;  /* 0x1110 */
    uint64_t chan_tx_addr;     /* 0x1114 */
    uint32_t chan_rx_addr_hi;  /* 0x1118 */
    uint64_t chan_rx_addr;     /* 0x111c */
    uint32_t chan_tx_end;      /* 0x1120 */
    uint32_t chan_rx_end;      /* 0x1128 */
    uint32_t chan_tx_ring_len; /* 0x112c */
    uint32_t chan_rx_ring_len; /* 0x1130 */
    uint32_t chan_int_en;      /* 0x1134 */
    uint64_t chan_cur_tx_desc; /* 0x1144 */
    uint64_t chan_cur_rx_desc; /* 0x114c */
    uint32_t chan_status;      /* 0x1160 */

    /* PHY stub state */
    uint32_t phy_addr;   /* MDIO address this instance responds to */
    uint32_t phy_bmcr;   /* PHY Basic Mode Control Register (reg 0) */
    uint32_t phy_anar;   /* PHY Auto-Negotiation Advertisement (reg 4) */
} PZ7110GmacState;

#endif
