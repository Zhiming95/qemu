/*
 * QEMU model of the Cadence QSPI Controller (cdns,qspi-nor)
 *
 * Minimal implementation for VisionFive2 / JH7110 SPL boot.
 * Supports STIG commands and indirect read mode for SPI NOR flash.
 *
 * Copyright (c) 2024 StarSix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/bswap.h"
#include "hw/ssi/cadence_qspi.h"

/* Register offsets (in uint32_t units) */
#define R_CONFIG            (0x00 / 4)
#define R_RD_INSTR          (0x04 / 4)
#define R_WR_INSTR          (0x08 / 4)
#define R_DELAY             (0x0C / 4)
#define R_RD_DATA_CAPTURE   (0x10 / 4)
#define R_SIZE              (0x14 / 4)
#define R_SRAMPARTITION     (0x18 / 4)
#define R_INDIRECTTRIGGER   (0x1C / 4)
#define R_REMAP             (0x24 / 4)
#define R_MODE_BIT          (0x28 / 4)
#define R_SRAM_LEVEL        (0x2C / 4)
#define R_WR_COMPLETION     (0x38 / 4)
#define R_IRQSTATUS         (0x40 / 4)
#define R_IRQMASK           (0x44 / 4)
#define R_INDIRECTRD        (0x60 / 4)
#define R_INDIRECTRDWATERMARK (0x64 / 4)
#define R_INDIRECTRDSTARTADDR (0x68 / 4)
#define R_INDIRECTRDBYTES   (0x6C / 4)
#define R_INDIRECTWR        (0x70 / 4)
#define R_INDIRECTWRWATERMARK (0x74 / 4)
#define R_INDIRECTWRSTARTADDR (0x78 / 4)
#define R_INDIRECTWRBYTES   (0x7C / 4)
#define R_CMDCTRL           (0x90 / 4)
#define R_CMDADDR           (0x94 / 4)
#define R_CMDREADLOWER      (0xA0 / 4)
#define R_CMDREADUPPER      (0xA4 / 4)
#define R_CMDWRITELOWER     (0xA8 / 4)
#define R_CMDWRITEUPPER     (0xAC / 4)
#define R_OP_EXT_LOWER      (0xE0 / 4)

/* CONFIG register bits */
#define CONFIG_ENABLE       BIT(0)
#define CONFIG_CLK_POL      BIT(1)
#define CONFIG_CLK_PHA      BIT(2)
#define CONFIG_DIRECT       BIT(7)
#define CONFIG_IDLE         BIT(31)

/* CMDCTRL register bits */
#define CMDCTRL_EXECUTE     BIT(0)
#define CMDCTRL_INPROGRESS  BIT(1)
#define CMDCTRL_DUMMY_LSB   7
#define CMDCTRL_WR_BYTES_LSB 12
#define CMDCTRL_WR_EN       BIT(15)
#define CMDCTRL_ADD_BYTES_LSB 16
#define CMDCTRL_ADDR_EN     BIT(19)
#define CMDCTRL_RD_BYTES_LSB 20
#define CMDCTRL_RD_EN       BIT(23)
#define CMDCTRL_OPCODE_LSB  24

/* INDIRECTRD register bits */
#define INDIRECTRD_START       BIT(0)
#define INDIRECTRD_CANCEL      BIT(1)
#define INDIRECTRD_INPROGRESS  BIT(2)
#define INDIRECTRD_DONE        BIT(5)

/* INDIRECTWR register bits */
#define INDIRECTWR_START       BIT(0)
#define INDIRECTWR_CANCEL      BIT(1)
#define INDIRECTWR_INPROGRESS  BIT(2)
#define INDIRECTWR_DONE        BIT(5)

#define SRAM_LEVEL_RD_MASK     0x0000ffff
#define SRAM_LEVEL_WR_MASK     0xffff0000
#define SRAM_LEVEL_CHUNK_BYTES 256

/* IRQSTATUS register bits */
#define IRQ_UNDERFLOW          BIT(1)
#define IRQ_INDIRECT_COMPLETE  BIT(2)
#define IRQ_WATERMARK          BIT(6)

/* Flash commands */
#define CMD_RDID            0x9F
#define CMD_WRSR            0x01
#define CMD_RDSR            0x05
#define CMD_RDCR            0x35
#define CMD_WREN            0x06
#define CMD_WRDIS           0x04
#define CMD_CHIP_ERASE      0xC7
#define CMD_SECTOR_ERASE    0x20
#define CMD_BLOCK_ERASE_32  0x52
#define CMD_BLOCK_ERASE_64  0xD8
#define CMD_PAGE_PROG       0x02
#define CMD_READ            0x03
#define CMD_FAST_READ       0x0B
#define CMD_RESET_ENABLE    0x66
#define CMD_RESET           0x99

/* GD25Q64 JEDEC ID */
#define FLASH_JEDEC_MFG     0xC8
#define FLASH_JEDEC_TYPE    0x40
#define FLASH_JEDEC_CAP     0x17

#define SR_WEL              BIT(1)

static void cadence_qspi_update_irq(CadenceQSPIState *s)
{
    qemu_set_irq(s->irq, (s->regs[R_IRQSTATUS] & s->regs[R_IRQMASK]) != 0);
}

static void cadence_qspi_stig_execute(CadenceQSPIState *s)
{
    uint32_t cmdctrl = s->regs[R_CMDCTRL];
    uint8_t opcode = (cmdctrl >> CMDCTRL_OPCODE_LSB) & 0xFF;
    bool addr_en = (cmdctrl >> 19) & 1;
    unsigned int rd_bytes = (cmdctrl >> 20) & 0x7;
    unsigned int wr_bytes = (cmdctrl >> 12) & 0x7;
    bool wr_en = (cmdctrl >> 15) & 1;

    memset(s->stig_read_data, 0, sizeof(s->stig_read_data));

    switch (opcode) {
    case CMD_RDID:
        /* Return JEDEC ID: C8 40 17 (GD25Q64) */
        s->stig_read_data[0] = FLASH_JEDEC_MFG;
        s->stig_read_data[1] = FLASH_JEDEC_TYPE;
        s->stig_read_data[2] = FLASH_JEDEC_CAP;
        if (rd_bytes > 3) {
            /* Some drivers read more ID bytes */
            memset(&s->stig_read_data[3], 0, rd_bytes - 3);
        }
        break;

    case CMD_RDSR:
        s->stig_read_data[0] = s->status_reg;
        break;

    case CMD_RDCR:
        s->stig_read_data[0] = s->config_reg;
        break;

    case CMD_WREN:
        s->status_reg |= SR_WEL;
        break;

    case CMD_WRDIS:
        s->status_reg &= ~SR_WEL;
        break;

    case CMD_WRSR:
        if (wr_en && (s->status_reg & SR_WEL)) {
            uint32_t data = s->regs[R_CMDWRITELOWER];

            s->status_reg = data & ~SR_WEL;
            if (wr_bytes >= 1) {
                s->config_reg = extract32(data, 8, 8);
            }
        }
        s->status_reg &= ~SR_WEL;
        break;

    case CMD_CHIP_ERASE:
    case CMD_SECTOR_ERASE:
    case CMD_BLOCK_ERASE_32:
    case CMD_BLOCK_ERASE_64:
    case CMD_PAGE_PROG:
    case CMD_RESET_ENABLE:
    case CMD_RESET:
        /* No data returned for write/erase commands */
        break;

    case CMD_READ:
    case CMD_FAST_READ:
        /* Read data from flash at the command address */
        if (addr_en) {
            uint32_t addr = s->regs[R_CMDADDR];
            unsigned int i;
            for (i = 0; i < rd_bytes && (addr + i) < s->flash_size; i++) {
                s->stig_read_data[i] = s->flash_data[addr + i];
            }
        }
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "cadence_qspi: unhandled STIG opcode 0x%02x\n",
                      opcode);
        break;
    }
}

static uint64_t cadence_qspi_read(void *opaque, hwaddr addr, unsigned int size)
{
    CadenceQSPIState *s = opaque;
    unsigned int reg = addr / 4;
    uint32_t val;

    if (reg >= CQSPI_NUM_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "cadence_qspi: read beyond regs @ 0x%" HWADDR_PRIx "\n",
                      addr);
        return 0;
    }


    switch (reg) {
    case R_CONFIG:
        /* Always report idle and enabled after first enable */
        val = s->regs[R_CONFIG] | CONFIG_IDLE;
        return val;

    case R_SRAM_LEVEL:
        /*
         * The U-Boot Cadence driver polls the read SRAM level, then drains
         * the AHB window.  The AHB window itself is backed by the flash ROM
         * mapping, so this model only needs to expose forward progress here.
         */
        if ((s->regs[R_INDIRECTRD] & INDIRECTRD_START) &&
            s->indirect_ahb_offset < s->indirect_bytes) {
            uint32_t remaining = s->indirect_bytes - s->indirect_ahb_offset;
            uint32_t chunk = MIN(remaining, (uint32_t)SRAM_LEVEL_CHUNK_BYTES);
            uint32_t words = (chunk + 3) / 4;

            return words & SRAM_LEVEL_RD_MASK;
        }

        /*
         * Report write FIFO empty.  The current model treats indirect writes
         * as completed no-ops, which is enough for probe and SPL boot flows.
         */
        return s->regs[R_SRAM_LEVEL] & ~SRAM_LEVEL_WR_MASK;

    case R_IRQSTATUS:
        return s->regs[R_IRQSTATUS];

    case R_CMDCTRL:
        /* Return current value but clear INPROGRESS after STIG completes */
        val = s->regs[R_CMDCTRL];
        return val;

    case R_CMDREADLOWER:
        return ldl_le_p(&s->stig_read_data[0]);

    case R_CMDREADUPPER:
        return ldl_le_p(&s->stig_read_data[4]);

    case R_INDIRECTRD:
        val = s->regs[R_INDIRECTRD];
        if (val & INDIRECTRD_START) {
            /* Check if indirect read is still in progress */
            if (s->indirect_ahb_offset < s->indirect_bytes) {
                val |= INDIRECTRD_INPROGRESS;
                val &= ~INDIRECTRD_DONE;
            } else {
                val &= ~INDIRECTRD_INPROGRESS;
                val |= INDIRECTRD_DONE;
            }
        }
        return val;

    case R_INDIRECTWR:
        val = s->regs[R_INDIRECTWR];
        if (val & INDIRECTWR_START) {
            val &= ~INDIRECTWR_INPROGRESS;
            val |= INDIRECTWR_DONE;
        }
        return val;

    default:
        return s->regs[reg];
    }
}

static void cadence_qspi_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned int size)
{
    CadenceQSPIState *s = opaque;
    unsigned int reg = addr / 4;

    if (reg >= CQSPI_NUM_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "cadence_qspi: write beyond regs @ 0x%" HWADDR_PRIx "\n",
                      addr);
        return;
    }

    switch (reg) {
    case R_CONFIG:
        s->regs[R_CONFIG] = val;
        break;

    case R_CMDCTRL:
        s->regs[R_CMDCTRL] = val;
        if (val & CMDCTRL_EXECUTE) {
            /* Execute STIG command */
            cadence_qspi_stig_execute(s);
            /* Clear INPROGRESS, command is done */
            s->regs[R_CMDCTRL] = val & ~CMDCTRL_INPROGRESS;
        }
        break;

    case R_CMDADDR:
        s->regs[R_CMDADDR] = val;
        break;

    case R_CMDWRITELOWER:
        s->regs[R_CMDWRITELOWER] = val;
        break;

    case R_CMDWRITEUPPER:
        s->regs[R_CMDWRITEUPPER] = val;
        break;

    case R_INDIRECTRD:
        if (val & INDIRECTRD_DONE) {
            s->regs[R_INDIRECTRD] = 0;
            s->indirect_offset = s->indirect_bytes;
            s->indirect_ahb_offset = s->indirect_bytes;
            break;
        }

        s->regs[R_INDIRECTRD] = val;
        if (val & INDIRECTRD_START) {
            s->indirect_addr = s->regs[R_INDIRECTRDSTARTADDR];
            s->indirect_bytes = s->regs[R_INDIRECTRDBYTES];
            s->indirect_offset = 0;
            s->indirect_ahb_offset = 0;
            s->regs[R_IRQSTATUS] |= IRQ_WATERMARK | IRQ_INDIRECT_COMPLETE;
            cadence_qspi_update_irq(s);
        }
        if (val & INDIRECTRD_CANCEL) {
            s->indirect_offset = s->indirect_bytes;
            s->indirect_ahb_offset = s->indirect_bytes;
            s->regs[R_INDIRECTRD] &= ~(INDIRECTRD_CANCEL | INDIRECTRD_START);
        }
        break;

    case R_INDIRECTWR:
        if (val & INDIRECTWR_DONE) {
            s->regs[R_INDIRECTWR] = 0;
            break;
        }

        s->regs[R_INDIRECTWR] = val;
        if (val & INDIRECTWR_START) {
            s->regs[R_INDIRECTWR] |= INDIRECTWR_DONE;
            s->regs[R_INDIRECTWR] &= ~INDIRECTWR_INPROGRESS;
            s->regs[R_IRQSTATUS] |= IRQ_INDIRECT_COMPLETE;
            cadence_qspi_update_irq(s);
        }
        if (val & INDIRECTWR_CANCEL) {
            s->regs[R_INDIRECTWR] &= ~(INDIRECTWR_CANCEL | INDIRECTWR_START);
        }
        break;

    case R_INDIRECTWRSTARTADDR:
        s->regs[R_INDIRECTWRSTARTADDR] = val;
        break;

    case R_INDIRECTWRBYTES:
        s->regs[R_INDIRECTWRBYTES] = val;
        break;

    case R_INDIRECTRDSTARTADDR:
        s->regs[R_INDIRECTRDSTARTADDR] = val;
        break;

    case R_INDIRECTRDBYTES:
        s->regs[R_INDIRECTRDBYTES] = val;
        break;

    case R_IRQSTATUS:
        /* Write-1-to-clear */
        s->regs[R_IRQSTATUS] &= ~val;
        cadence_qspi_update_irq(s);
        break;

    case R_IRQMASK:
        s->regs[R_IRQMASK] = val;
        cadence_qspi_update_irq(s);
        break;

    default:
        s->regs[reg] = val;
        break;
    }
}

static const MemoryRegionOps cadence_qspi_ops = {
    .read = cadence_qspi_read,
    .write = cadence_qspi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void cadence_qspi_reset(DeviceState *dev)
{
    CadenceQSPIState *s = CADENCE_QSPI(dev);

    memset(s->regs, 0, sizeof(s->regs));
    memset(s->stig_read_data, 0, sizeof(s->stig_read_data));
    s->status_reg = 0;
    s->config_reg = 0;
    s->indirect_offset = 0;
    s->indirect_bytes = 0;
    s->indirect_addr = 0;
    s->indirect_ahb_offset = 0;

    /* Default register values */
    s->regs[R_CONFIG] = CONFIG_IDLE;
}

static void cadence_qspi_realize(DeviceState *dev, Error **errp)
{
    CadenceQSPIState *s = CADENCE_QSPI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    /* Initialize IRQ output */
    sysbus_init_irq(sbd, &s->irq);

    /* Register MMIO region for controller registers */
    memory_region_init_io(&s->iomem, OBJECT(dev), &cadence_qspi_ops, s,
                          "cadence-qspi.regs", 0x10000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const Property cadence_qspi_properties[] = {
    DEFINE_PROP_UINT32("flash_size", CadenceQSPIState, flash_size, 0),
};

static void cadence_qspi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = cadence_qspi_realize;
    device_class_set_legacy_reset(dc, cadence_qspi_reset);
    device_class_set_props(dc, cadence_qspi_properties);
}

static const TypeInfo cadence_qspi_info = {
    .name          = TYPE_CADENCE_QSPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CadenceQSPIState),
    .class_init    = cadence_qspi_class_init,
};

static void cadence_qspi_register_types(void)
{
    type_register_static(&cadence_qspi_info);
}

type_init(cadence_qspi_register_types)
