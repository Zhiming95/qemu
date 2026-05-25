/*
 * PZ7110 DW_apb_i2c stub
 *
 * Minimal register-compatible stub for DesignWare I2C controller.
 * Provides enough register behavior for Linux to probe the device.
 * No actual I2C bus transactions are modeled.
 *
 * Register layout matches DW_apb_i2c v2.01a (Synopsys).
 * Only the essential registers are implemented; others return 0.
 *
 * Copyright (c) 2024 StarSix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#ifndef HW_RISCV_PZ7110_I2C_H
#define HW_RISCV_PZ7110_I2C_H

#include "hw/sysbus.h"
#include "hw/irq.h"
#include "qemu/typedefs.h"

#define TYPE_PZ7110_I2C "pz7110.i2c"
OBJECT_DECLARE_SIMPLE_TYPE(PZ7110I2CState, PZ7110_I2C)

#define PZ7110_I2C_SIZE 0x10000

/* DW_apb_i2c register offsets */
#define DW_I2C_CON          0x00   /* Control register */
#define DW_I2C_TAR          0x04   /* Target address */
#define DW_I2C_SAR          0x08   /* Slave address */
#define DW_I2C_HS_MADDR     0x0C   /* High speed master mode code */
#define DW_I2C_DATA_CMD     0x10   /* Data buffer and command */
#define DW_I2C_SS_SCL_HCNT  0x14   /* Standard speed SCL high count */
#define DW_I2C_SS_SCL_LCNT  0x18   /* Standard speed SCL low count */
#define DW_I2C_FS_SCL_HCNT  0x1C   /* Fast speed SCL high count */
#define DW_I2C_FS_SCL_LCNT  0x20   /* Fast speed SCL low count */
#define DW_I2C_HS_SCL_HCNT  0x24   /* High speed SCL high count */
#define DW_I2C_HS_SCL_LCNT  0x28   /* High speed SCL low count */
#define DW_I2C_INTR_STAT    0x2C   /* Interrupt status (read-only) */
#define DW_I2C_INTR_MASK    0x30   /* Interrupt mask */
#define DW_I2C_RAW_INTR     0x34   /* Raw interrupt status (read-only) */
#define DW_I2C_RX_TL        0x38   /* Receive FIFO threshold */
#define DW_I2C_TX_TL        0x3C   /* Transmit FIFO threshold */
#define DW_I2C_CLR_INTR     0x40   /* Clear combined and individual IRQ (read-only) */
#define DW_I2C_CLR_RX_UNDER 0x44   /* Clear RX_UNDER interrupt (read-only) */
#define DW_I2C_CLR_RX_OVER  0x48   /* Clear RX_OVER interrupt (read-only) */
#define DW_I2C_CLR_TX_OVER  0x4C   /* Clear TX_OVER interrupt (read-only) */
#define DW_I2C_CLR_RD_REQ   0x50   /* Clear RD_REQ interrupt (read-only) */
#define DW_I2C_CLR_TX_ABRT  0x54   /* Clear TX_ABRT interrupt (read-only) */
#define DW_I2C_CLR_RX_DONE  0x58   /* Clear RX_DONE interrupt (read-only) */
#define DW_I2C_CLR_ACTIVITY 0x5C   /* Clear ACTIVITY interrupt (read-only) */
#define DW_I2C_CLR_STOP_DET 0x60   /* Clear STOP_DET interrupt (read-only) */
#define DW_I2C_CLR_START_DET 0x64  /* Clear START_DET interrupt (read-only) */
#define DW_I2C_CLR_GEN_CALL 0x68   /* Clear GEN_CALL interrupt (read-only) */
#define DW_I2C_ENABLE       0x6C   /* Enable register */
#define DW_I2C_STATUS       0x70   /* Status register (read-only) */
#define DW_I2C_TXFLR        0x74   /* Transmit FIFO level (read-only) */
#define DW_I2C_RXFLR        0x78   /* Receive FIFO level (read-only) */
#define DW_I2C_SDA_HOLD     0x7C   /* SDA hold time */
#define DW_I2C_TX_ABRT_SRC  0x80   /* Transmit abort source (read-only) */
#define DW_I2C_SLV_DATA_NACK 0x84  /* Slave data NACK only */
#define DW_I2C_DMA_CR       0x88   /* DMA control */
#define DW_I2C_DMA_TDLR     0x8C   /* DMA transmit data level */
#define DW_I2C_DMA_RDLR     0x90   /* DMA receive data level */
#define DW_I2C_SDA_SETUP    0x94   /* SDA setup time */
#define DW_I2C_ACK_GENERAL  0x98   /* ACK general call */
#define DW_I2C_ENABLE_STATUS 0x9C  /* Enable status (read-only) */
#define DW_I2C_COMP_PARAM   0xF4   /* Component parameter (read-only) */
#define DW_I2C_COMP_VERSION 0xF8   /* Component version (read-only) */
#define DW_I2C_COMP_TYPE    0xFC   /* Component type (read-only) */

/* DW_I2C_CON bits */
#define DW_I2C_CON_MASTER_MODE      (1 << 0)
#define DW_I2C_CON_SPEED_MASK       (3 << 1)
#define DW_I2C_CON_SPEED_SS         (1 << 1)  /* 100kbps */
#define DW_I2C_CON_SPEED_FS         (2 << 1)  /* 400kbps */
#define DW_I2C_CON_SPEED_HS         (3 << 1)  /* 3.4Mbps */
#define DW_I2C_CON_10BITADDR_SLAVE  (1 << 3)
#define DW_I2C_CON_10BITADDR_MASTER (1 << 4)
#define DW_I2C_CON_RESTART_EN       (1 << 5)
#define DW_I2C_CON_SLAVE_DISABLE    (1 << 6)

/* DW_I2C_STATUS bits */
#define DW_I2C_STATUS_ACTIVITY       (1 << 0)
#define DW_I2C_STATUS_TFNF           (1 << 1)  /* TX FIFO not full */
#define DW_I2C_STATUS_TFE            (1 << 2)  /* TX FIFO empty */
#define DW_I2C_STATUS_RFNE           (1 << 3)  /* RX FIFO not empty */
#define DW_I2C_STATUS_RFF            (1 << 4)  /* RX FIFO full */
#define DW_I2C_STATUS_MST_ACTIVITY   (1 << 5)
#define DW_I2C_STATUS_SLV_ACTIVITY   (1 << 6)

#define PZ7110_I2C_EEPROM_SIZE 256
#define PZ7110_I2C_RX_FIFO_SIZE 16

struct PZ7110I2CState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    /* Registers */
    uint32_t con;
    uint32_t tar;
    uint32_t sar;
    uint32_t intr_mask;
    uint32_t raw_intr;
    uint32_t rx_tl;
    uint32_t tx_tl;
    uint32_t enable;
    uint32_t sda_hold;
    uint32_t sda_setup;
    uint32_t ack_general;
    uint32_t dma_cr;
    uint32_t dma_tdlr;
    uint32_t dma_rdlr;
    uint32_t slv_data_nack;

    bool eeprom_present;
    uint8_t eeprom[PZ7110_I2C_EEPROM_SIZE];
    uint16_t eeprom_ptr;
    uint8_t rx_fifo[PZ7110_I2C_RX_FIFO_SIZE];
    uint8_t rx_count;
    uint8_t rx_pos;

    qemu_irq irq;
};

#endif
