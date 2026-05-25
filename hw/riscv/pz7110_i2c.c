/*
 * PZ7110 DW_apb_i2c stub
 *
 * Minimal register-compatible stub for DesignWare I2C controller.
 * Provides enough register behavior for Linux i2c-designware driver
 * to probe and initialize without errors. No real I2C transactions.
 *
 * Copyright (c) 2024 StarSix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/riscv/pz7110_i2c.h"

#define DW_I2C_DATA_CMD_READ BIT(8)
#define DW_I2C_INTR_RX_UNDER BIT(0)
#define DW_I2C_INTR_RX_FULL  BIT(2)
#define DW_I2C_INTR_TX_EMPTY BIT(4)
#define DW_I2C_INTR_TX_ABRT  BIT(6)
#define DW_I2C_INTR_STOP_DET BIT(9)

#define PZ7110_EEPROM_I2C_ADDR 0x50
#define PZ7110_EEPROM_ATOM_VENDOR 0x0001
#define PZ7110_EEPROM_ATOM_CUSTOM 0x0004
#define PZ7110_EEPROM_CRC16 0x8005

static void pz7110_i2c_update_irq(PZ7110I2CState *s);

static uint16_t pz7110_eeprom_crc16(const uint8_t *data, size_t size)
{
    uint16_t out = 0;
    uint16_t crc = 0;
    int bits_read = 0;

    while (size > 0) {
        bool bit_flag = out >> 15;

        out <<= 1;
        out |= (*data >> bits_read) & 1;

        bits_read++;
        if (bits_read > 7) {
            bits_read = 0;
            data++;
            size--;
        }

        if (bit_flag) {
            out ^= PZ7110_EEPROM_CRC16;
        }
    }

    for (int i = 0; i < 16; i++) {
        bool bit_flag = out >> 15;

        out <<= 1;
        if (bit_flag) {
            out ^= PZ7110_EEPROM_CRC16;
        }
    }

    for (uint16_t i = 0x8000, j = 0x0001; i != 0; i >>= 1, j <<= 1) {
        if (i & out) {
            crc |= j;
        }
    }

    return crc;
}

static void pz7110_store_le16(uint8_t *p, uint16_t val)
{
    p[0] = val & 0xff;
    p[1] = val >> 8;
}

static void pz7110_store_le32(uint8_t *p, uint32_t val)
{
    p[0] = val & 0xff;
    p[1] = (val >> 8) & 0xff;
    p[2] = (val >> 16) & 0xff;
    p[3] = val >> 24;
}

static void pz7110_i2c_init_eeprom(PZ7110I2CState *s)
{
    uint8_t *e = s->eeprom;
    uint8_t *atom;
    size_t off = 0;
    uint16_t crc;
    static const uint8_t mac0[6] = { 0x6c, 0xcf, 0x39, 0x6c, 0xde, 0xad };
    static const uint8_t mac1[6] = { 0x6c, 0xcf, 0x39, 0x7c, 0xae, 0x5d };

    memset(e, 0, PZ7110_I2C_EEPROM_SIZE);

    memcpy(&e[0], "SFVF", 4);
    e[4] = 0x03;
    e[5] = 0x00;
    pz7110_store_le16(&e[6], 2);
    pz7110_store_le32(&e[8], 136);
    off = 12;

    atom = &e[off];
    pz7110_store_le16(&atom[0], PZ7110_EEPROM_ATOM_VENDOR);
    pz7110_store_le16(&atom[2], 1);
    pz7110_store_le32(&atom[4], 88);
    memset(&atom[8], 0, 16);
    pz7110_store_le16(&atom[24], 0);
    pz7110_store_le16(&atom[26], 0);
    atom[28] = 32;
    atom[29] = 32;
    memcpy(&atom[30], "StarFive Technology Co., Ltd.", 29);
    memcpy(&atom[62], "VF7110A1-2228-D004E000-00000001", 32);
    crc = pz7110_eeprom_crc16(atom, 94);
    pz7110_store_le16(&atom[94], crc);
    off += 96;

    atom = &e[off];
    pz7110_store_le16(&atom[0], PZ7110_EEPROM_ATOM_CUSTOM);
    pz7110_store_le16(&atom[2], 2);
    pz7110_store_le32(&atom[4], 20);
    pz7110_store_le16(&atom[8], 0x03);
    atom[10] = 0xb1;
    atom[11] = 'A';
    memcpy(&atom[12], mac0, sizeof(mac0));
    memcpy(&atom[18], mac1, sizeof(mac1));
    atom[24] = 0;
    atom[25] = 0;
    crc = pz7110_eeprom_crc16(atom, 26);
    pz7110_store_le16(&atom[26], crc);
}

static void pz7110_i2c_rx_push(PZ7110I2CState *s, uint8_t val)
{
    if (s->rx_count < PZ7110_I2C_RX_FIFO_SIZE) {
        s->rx_fifo[(s->rx_pos + s->rx_count) % PZ7110_I2C_RX_FIFO_SIZE] = val;
        s->rx_count++;
        s->raw_intr |= DW_I2C_INTR_RX_FULL;
    }
}

static uint8_t pz7110_i2c_rx_pop(PZ7110I2CState *s)
{
    uint8_t val;

    if (!s->rx_count) {
        s->raw_intr |= DW_I2C_INTR_RX_UNDER;
        pz7110_i2c_update_irq(s);
        return 0xff;
    }

    val = s->rx_fifo[s->rx_pos];
    s->rx_pos = (s->rx_pos + 1) % PZ7110_I2C_RX_FIFO_SIZE;
    s->rx_count--;
    if (!s->rx_count) {
        s->raw_intr &= ~DW_I2C_INTR_RX_FULL;
        s->rx_pos = 0;
    }
    pz7110_i2c_update_irq(s);
    return val;
}

static void pz7110_i2c_update_irq(PZ7110I2CState *s)
{
    uint32_t level = (s->raw_intr & s->intr_mask) ? 1 : 0;
    qemu_set_irq(s->irq, level);
}

static uint64_t pz7110_i2c_read(void *opaque, hwaddr addr,
                                 unsigned int size)
{
    PZ7110I2CState *s = opaque;

    switch (addr) {
    case DW_I2C_CON:
        return s->con;
    case DW_I2C_TAR:
        return s->tar;
    case DW_I2C_SAR:
        return s->sar;
    case DW_I2C_INTR_STAT:
        return s->raw_intr & s->intr_mask;
    case DW_I2C_INTR_MASK:
        return s->intr_mask;
    case DW_I2C_RAW_INTR:
        return s->raw_intr;
    case DW_I2C_DATA_CMD:
        return pz7110_i2c_rx_pop(s);
    case DW_I2C_RX_TL:
        return s->rx_tl;
    case DW_I2C_TX_TL:
        return s->tx_tl;
    case DW_I2C_CLR_INTR:
        /* Read clears all interrupts */
        s->raw_intr = 0;
        pz7110_i2c_update_irq(s);
        return 0;
    case DW_I2C_CLR_RX_UNDER:
        s->raw_intr &= ~(1 << 0);
        pz7110_i2c_update_irq(s);
        return 0;
    case DW_I2C_CLR_TX_ABRT:
        s->raw_intr &= ~(1 << 6);
        pz7110_i2c_update_irq(s);
        return 0;
    case DW_I2C_CLR_STOP_DET:
        s->raw_intr &= ~(1 << 9);
        pz7110_i2c_update_irq(s);
        return 0;
    case DW_I2C_ENABLE:
        return s->enable;
    case DW_I2C_STATUS:
        if (s->enable) {
            /* TX FIFO empty and not full, RX FIFO empty */
            uint32_t status = DW_I2C_STATUS_TFE | DW_I2C_STATUS_TFNF;

            if (s->rx_count) {
                status |= DW_I2C_STATUS_RFNE;
            }
            return status;
        }
        return 0;
    case DW_I2C_TXFLR:
        return 0; /* TX FIFO always empty in stub */
    case DW_I2C_RXFLR:
        return s->rx_count;
    case DW_I2C_SDA_HOLD:
        return s->sda_hold;
    case DW_I2C_TX_ABRT_SRC:
        return 0;
    case DW_I2C_SLV_DATA_NACK:
        return s->slv_data_nack;
    case DW_I2C_DMA_CR:
        return s->dma_cr;
    case DW_I2C_DMA_TDLR:
        return s->dma_tdlr;
    case DW_I2C_DMA_RDLR:
        return s->dma_rdlr;
    case DW_I2C_SDA_SETUP:
        return s->sda_setup;
    case DW_I2C_ACK_GENERAL:
        return s->ack_general;
    case DW_I2C_ENABLE_STATUS:
        return s->enable;
    case DW_I2C_COMP_PARAM:
        /* TX buffer depth=16, RX buffer depth=16, APB data width=32 */
        return (15 << 0) | (15 << 8) | (2 << 16);
    case DW_I2C_COMP_VERSION:
        return 0x3230312A; /* "2.01a" in ASCII-ish */
    case DW_I2C_COMP_TYPE:
        return 0x44570140; /* "DW" + design config */
    default:
        return 0;
    }
}

static void pz7110_i2c_write(void *opaque, hwaddr addr,
                              uint64_t val64, unsigned int size)
{
    PZ7110I2CState *s = opaque;
    uint32_t val = (uint32_t)val64;

    switch (addr) {
    case DW_I2C_CON:
        s->con = val & 0x7F;
        return;
    case DW_I2C_TAR:
        s->tar = val & 0x3FF;
        return;
    case DW_I2C_SAR:
        s->sar = val & 0x3FF;
        return;
    case DW_I2C_SS_SCL_HCNT:
    case DW_I2C_SS_SCL_LCNT:
    case DW_I2C_FS_SCL_HCNT:
    case DW_I2C_FS_SCL_LCNT:
    case DW_I2C_HS_SCL_HCNT:
    case DW_I2C_HS_SCL_LCNT:
        /* Clock divider registers: accept and ignore */
        return;
    case DW_I2C_DATA_CMD:
        if (val & (1 << 8)) {
            if (s->eeprom_present && s->tar == PZ7110_EEPROM_I2C_ADDR) {
                pz7110_i2c_rx_push(s, s->eeprom[s->eeprom_ptr]);
                s->eeprom_ptr = (s->eeprom_ptr + 1) %
                                PZ7110_I2C_EEPROM_SIZE;
                s->raw_intr |= DW_I2C_INTR_STOP_DET |
                               DW_I2C_INTR_TX_EMPTY;
            } else {
                s->raw_intr |= DW_I2C_INTR_TX_ABRT;
            }
        } else if (s->eeprom_present && s->tar == PZ7110_EEPROM_I2C_ADDR) {
            s->eeprom_ptr = val & 0xff;
            s->raw_intr |= DW_I2C_INTR_TX_EMPTY;
        } else {
            s->raw_intr |= DW_I2C_INTR_TX_ABRT;
        }
        pz7110_i2c_update_irq(s);
        return;
    case DW_I2C_INTR_MASK:
        s->intr_mask = val & 0x7FF;
        pz7110_i2c_update_irq(s);
        return;
    case DW_I2C_RX_TL:
        s->rx_tl = val & 0xFF;
        return;
    case DW_I2C_TX_TL:
        s->tx_tl = val & 0xFF;
        return;
    case DW_I2C_ENABLE:
        s->enable = val & 1;
        return;
    case DW_I2C_SDA_HOLD:
        s->sda_hold = val;
        return;
    case DW_I2C_SDA_SETUP:
        s->sda_setup = val & 0xFF;
        return;
    case DW_I2C_ACK_GENERAL:
        s->ack_general = val & 1;
        return;
    case DW_I2C_SLV_DATA_NACK:
        s->slv_data_nack = val & 1;
        return;
    case DW_I2C_DMA_CR:
        s->dma_cr = val & 3;
        return;
    case DW_I2C_DMA_TDLR:
        s->dma_tdlr = val & 0xFF;
        return;
    case DW_I2C_DMA_RDLR:
        s->dma_rdlr = val & 0xFF;
        return;
    default:
        return;
    }
}

static const MemoryRegionOps pz7110_i2c_ops = {
    .read = pz7110_i2c_read,
    .write = pz7110_i2c_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void pz7110_i2c_reset_enter(Object *obj, ResetType type)
{
    PZ7110I2CState *s = PZ7110_I2C(obj);
    s->con = DW_I2C_CON_MASTER_MODE | DW_I2C_CON_SPEED_SS |
             DW_I2C_CON_RESTART_EN | DW_I2C_CON_SLAVE_DISABLE;
    s->tar = 0;
    s->sar = 0;
    s->intr_mask = 0;
    s->raw_intr = 0;
    s->rx_tl = 0;
    s->tx_tl = 0;
    s->enable = 0;
    s->sda_hold = 0;
    s->sda_setup = 0;
    s->ack_general = 0;
    s->dma_cr = 0;
    s->dma_tdlr = 0;
    s->dma_rdlr = 0;
    s->slv_data_nack = 0;
    s->eeprom_ptr = 0;
    s->rx_count = 0;
    s->rx_pos = 0;
    pz7110_i2c_init_eeprom(s);
}

static void pz7110_i2c_init(Object *obj)
{
    PZ7110I2CState *s = PZ7110_I2C(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &pz7110_i2c_ops,
                          s, "pz7110.i2c", PZ7110_I2C_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
}

static const VMStateDescription vmstate_pz7110_i2c = {
    .name = TYPE_PZ7110_I2C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(con, PZ7110I2CState),
        VMSTATE_UINT32(tar, PZ7110I2CState),
        VMSTATE_UINT32(sar, PZ7110I2CState),
        VMSTATE_UINT32(intr_mask, PZ7110I2CState),
        VMSTATE_UINT32(raw_intr, PZ7110I2CState),
        VMSTATE_UINT32(rx_tl, PZ7110I2CState),
        VMSTATE_UINT32(tx_tl, PZ7110I2CState),
        VMSTATE_UINT32(enable, PZ7110I2CState),
        VMSTATE_UINT32(sda_hold, PZ7110I2CState),
        VMSTATE_UINT32(sda_setup, PZ7110I2CState),
        VMSTATE_UINT32(ack_general, PZ7110I2CState),
        VMSTATE_UINT32(dma_cr, PZ7110I2CState),
        VMSTATE_UINT32(dma_tdlr, PZ7110I2CState),
        VMSTATE_UINT32(dma_rdlr, PZ7110I2CState),
        VMSTATE_UINT32(slv_data_nack, PZ7110I2CState),
        VMSTATE_BOOL(eeprom_present, PZ7110I2CState),
        VMSTATE_UINT8_ARRAY(eeprom, PZ7110I2CState, PZ7110_I2C_EEPROM_SIZE),
        VMSTATE_UINT16(eeprom_ptr, PZ7110I2CState),
        VMSTATE_UINT8_ARRAY(rx_fifo, PZ7110I2CState, PZ7110_I2C_RX_FIFO_SIZE),
        VMSTATE_UINT8(rx_count, PZ7110I2CState),
        VMSTATE_UINT8(rx_pos, PZ7110I2CState),
        VMSTATE_END_OF_LIST(),
    }
};

static const Property pz7110_i2c_properties[] = {
    DEFINE_PROP_BOOL("eeprom", PZ7110I2CState, eeprom_present, false),
};

static void pz7110_i2c_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PZ7110 DW_apb_i2c stub";
    dc->vmsd = &vmstate_pz7110_i2c;
    device_class_set_props(dc, pz7110_i2c_properties);
    rc->phases.enter = pz7110_i2c_reset_enter;
}

static const TypeInfo pz7110_i2c_info = {
    .name          = TYPE_PZ7110_I2C,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110I2CState),
    .class_init    = pz7110_i2c_class_init,
    .instance_init = pz7110_i2c_init,
};

static void pz7110_i2c_register_types(void)
{
    type_register_static(&pz7110_i2c_info);
}

type_init(pz7110_i2c_register_types)
