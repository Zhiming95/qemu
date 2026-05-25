/*
 * QEMU PZ7110 Synopsys DesignWare Mobile Storage Host Controller
 *
 * Copyright (c) 2024 StarSix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#include "qemu/osdep.h"
#include "hw/riscv/pz7110_sdio.h"
#include "hw/irq.h"
#include "hw/qdev-core.h"
#include "hw/sd/sd.h"
#include "system/dma.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "migration/vmstate.h"

#define SDMMC_IDSTS_TI BIT(0)
#define SDMMC_IDSTS_RI BIT(1)
#define SDMMC_IDMAC_OWN BIT(31)
#define SDMMC_IDMAC_LD BIT(2)
#define SDMMC_FIFO_EMPTY BIT(2)
#define SDMMC_FIFO_FULL BIT(3)
#define SDMMC_FIFO_SHIFT 17

typedef struct PZ7110SdioDesc {
    uint32_t flags;
    uint32_t cnt;
    uint32_t addr;
    uint32_t next_addr;
} PZ7110SdioDesc;

static void pz7110_sdio_update_irq(PZ7110SdioState *s)
{
    s->mintsts = s->rintsts & s->intmask;
    qemu_set_irq(s->irq, s->mintsts != 0);
}

static void pz7110_sdio_set_irq_bits(PZ7110SdioState *s, uint32_t bits)
{
    s->rintsts |= bits;
    pz7110_sdio_update_irq(s);
}

static void pz7110_sdio_clear_irq_bits(PZ7110SdioState *s, uint32_t bits)
{
    s->rintsts &= ~bits;
    pz7110_sdio_update_irq(s);
}

static uint32_t pz7110_sdio_status(PZ7110SdioState *s)
{
    uint32_t status = s->status;

    status &= ~((0x1fffu << SDMMC_FIFO_SHIFT) |
                SDMMC_FIFO_EMPTY | SDMMC_FIFO_FULL | SDMMC_STATUS_BUSY);
    status |= (s->fifo_count & 0x1fff) << SDMMC_FIFO_SHIFT;
    if (s->fifo_count == 0) {
        status |= SDMMC_FIFO_EMPTY;
    }
    if (s->fifo_count >= SDMMC_FIFO_DEPTH) {
        status |= SDMMC_FIFO_FULL;
    }

    return status;
}

static void pz7110_sdio_set_response(PZ7110SdioState *s,
                                      const uint8_t *response, int len)
{
    s->resp[0] = s->resp[1] = s->resp[2] = s->resp[3] = 0;

    if (len == 4) {
        s->resp[0] = ldl_be_p(response);
    } else if (len == 16) {
        /* R2 response: 128 bits stored in RESP0..3 (DW MMC layout).
         * RESP0 = bits 31:0 (bytes 12-15), RESP1 = bits 63:32 (bytes 8-11),
         * RESP2 = bits 95:64 (bytes 4-7), RESP3 = bits 127:96 (bytes 0-3).
         */
        s->resp[0] = ldl_be_p(&response[12]);
        s->resp[1] = ldl_be_p(&response[8]);
        s->resp[2] = ldl_be_p(&response[4]);
        s->resp[3] = ldl_be_p(&response[0]);
    }
}

static void pz7110_sdio_fifo_load(PZ7110SdioState *s,
                                  const uint8_t *buf, uint32_t len)
{
    uint32_t words = MIN(len / 4, (uint32_t)SDMMC_FIFO_DEPTH);

    s->fifo_pos = 0;
    s->fifo_count = words;
    s->fifo_half = 0;
    for (uint32_t i = 0; i < words; i++) {
        s->fifo[i] = ldl_le_p(buf + i * 4);
    }
}

static void pz7110_sdio_dma_transfer(PZ7110SdioState *s,
                                      uint8_t *buf, uint32_t len,
                                      bool is_write)
{
    hwaddr desc_addr = s->dbaddr;
    uint32_t done = 0;

    while (desc_addr && done < len) {
        uint32_t words[4];
        PZ7110SdioDesc desc;
        uint32_t todo;

        if (dma_memory_read(&address_space_memory, desc_addr, words,
                            sizeof(words), MEMTXATTRS_UNSPECIFIED)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "pz7110_sdio: failed to read IDMAC desc @0x%"
                          HWADDR_PRIx "\n", desc_addr);
            break;
        }

        desc.flags = le32_to_cpu(words[0]);
        desc.cnt = le32_to_cpu(words[1]);
        desc.addr = le32_to_cpu(words[2]);
        desc.next_addr = le32_to_cpu(words[3]);

        if (!(desc.flags & SDMMC_IDMAC_OWN)) {
            break;
        }

        todo = desc.cnt ? desc.cnt : (len - done);
        todo = MIN(todo, len - done);

        if (is_write) {
            dma_memory_read(&address_space_memory, desc.addr,
                            buf + done, todo, MEMTXATTRS_UNSPECIFIED);
        } else {
            dma_memory_write(&address_space_memory, desc.addr,
                             buf + done, todo, MEMTXATTRS_UNSPECIFIED);
        }

        done += todo;
        desc.flags &= ~SDMMC_IDMAC_OWN;
        words[0] = cpu_to_le32(desc.flags);
        dma_memory_write(&address_space_memory, desc_addr, words,
                         sizeof(words), MEMTXATTRS_UNSPECIFIED);

        if (desc.flags & SDMMC_IDMAC_LD) {
            break;
        }
        desc_addr = desc.next_addr;
    }

    if (is_write) {
        s->idsts |= SDMMC_IDSTS_TI;
    } else {
        s->idsts |= SDMMC_IDSTS_RI;
    }
}

static void pz7110_sdio_do_command(PZ7110SdioState *s, uint32_t val)
{
    SDRequest request;
    uint8_t response[16];
    uint8_t *buf = NULL;
    int rlen;
    uint32_t len;
    uint32_t cmd = SDMMC_CMD_INDX(val);
    bool response_expected = val & SDMMC_CMD_RESP_EXP;
    bool data_expected = val & SDMMC_CMD_DAT_EXP;
    bool data_write = val & SDMMC_CMD_DAT_WR;
    bool dma_enabled = (s->ctrl & (SDMMC_CTRL_DMA_ENABLE |
                                   SDMMC_CTRL_USE_IDMAC)) ||
                       (s->bmod & SDMMC_BMOD_ENABLE);

    s->cmd = val & ~SDMMC_CMD_START;

    if (val & SDMMC_CMD_UPD_CLK) {
        pz7110_sdio_set_irq_bits(s, SDMMC_INT_CMD_DONE);
        return;
    }

    request.cmd = cmd;
    request.arg = s->cmdarg;
    request.crc = 0;

    /*
     * No card present in eMMC mode: the SD bus has no backend device,
     * so there is no card to answer the command. Skip the SD bus and
     * signal completion immediately — see after_command for the
     * RTO|CMD_DONE quirk explanation.
     */
    if (!s->card_present && s->emmc_mode) {
        goto after_no_card;
    }

    /*
     * Suppress commands that produce LOG_GUEST_ERROR noise during
     * Linux's multi-type card probe on an eMMC slot.  Do not apply this
     * filtering to SD-card slots: CMD8 and the CMD55/ACMD41 sequence are
     * required for SD initialization.
     *
     * CMD5 (SDIO) and CMD20 (SPEED_CLASS_CONTROL) are never valid
     * for eMMC — suppressed unconditionally.
     *
     * CMD8 is ambiguous: SD uses it as SEND_IF_COND without a data
     * phase, while eMMC uses it as SEND_EXT_CSD with a data phase.
     * Only suppress the no-data SD probe form.
     *
     * CMD23 (SET_BLOCK_COUNT) is valid only after card initialization.
     * Suppress the early probe form before the first successful CMD1.
     */
    if (s->emmc_mode) {
        if (cmd == 8 && !data_expected) {
            rlen = 0;
            goto after_command;
        }
        if (s->pre_init && cmd == 23) {
            rlen = 0;
            goto after_command;
        }
        if (cmd == 5 || cmd == 20) {
            rlen = 0;
            goto after_command;
        }
    }

    rlen = sdbus_do_command(&s->sdbus, &request, response);

    if (cmd == 0) {
        s->pre_init = true;
    } else if (s->pre_init && cmd == 1 && (rlen == 4 || rlen == 16)) {
        s->pre_init = false;
    }

after_command:
    if (response_expected) {
        if (rlen == 4 || rlen == 16) {
            pz7110_sdio_set_response(s, response, rlen);
            s->rintsts |= SDMMC_INT_CMD_DONE;
        } else {
            /*
             * PZ7110 QEMU compatibility quirk — same rationale as
             * after_no_card below.  Filtered eMMC probe commands
             * (CMD5, CMD20, CMD8 no-data, CMD23 pre-init) reach here
             * with rlen=0 when a card IS present.
             */
            s->rintsts |= SDMMC_INT_RTO | SDMMC_INT_CMD_DONE;
            pz7110_sdio_update_irq(s);
            return;
        }
    } else {
        s->rintsts |= SDMMC_INT_CMD_DONE;
    }

    if (!data_expected) {
        pz7110_sdio_update_irq(s);
        return;
    }

    len = s->bytcnt;
    if (!len) {
        len = s->blksiz;
    }
    if (!len) {
        len = 512;
    }

    buf = g_malloc0(len);
    if (data_write) {
        if (dma_enabled) {
            pz7110_sdio_dma_transfer(s, buf, len, true);
        }
        sdbus_write_data(&s->sdbus, buf, len);
    } else {
        sdbus_read_data(&s->sdbus, buf, len);
        if (dma_enabled) {
            pz7110_sdio_dma_transfer(s, buf, len, false);
        } else {
            pz7110_sdio_fifo_load(s, buf, len);
            s->rintsts |= SDMMC_INT_RXDR;
        }
    }
    g_free(buf);

    s->tcbcnt = len;
    s->tbbcnt = 0;
    pz7110_sdio_set_irq_bits(s, SDMMC_INT_DATA_OVER);
    return;

after_no_card:
    /*
     * PZ7110 QEMU compatibility quirk for the VF2 dw_mmc driver.
     *
     * Linux dw_mmc records command-error interrupts but this vendor kernel
     * does not schedule the tasklet from that path. In QEMU's synchronous
     * model this can leave no-card eMMC probes waiting forever. Report
     * CMD_DONE together with RTO so the existing dw_mci_cmd_interrupt()
     * path schedules the tasklet; cmd_status remains RTO, so the request
     * completes as -ETIMEDOUT.
     *
     * This is not a claim that real DW MMC hardware reports successful
     * command completion on response timeout.
     */
    s->rintsts |= SDMMC_INT_RTO | SDMMC_INT_CMD_DONE;
    pz7110_sdio_update_irq(s);
}

static void pz7110_sdio_reset(DeviceState *dev)
{
    PZ7110SdioState *s = PZ7110_SDIO(dev);

    s->ctrl = 0;
    s->pwren = 0;
    s->clkdiv = 0;
    s->clksrc = 0;
    s->clkena = 0;
    s->tmout = 0xFFFFFFFF;
    s->ctype = 0;
    s->blksiz = 512;
    s->bytcnt = 0;
    s->intmask = 0;
    s->cmdarg = 0;
    s->cmd = 0;
    s->resp[0] = s->resp[1] = s->resp[2] = s->resp[3] = 0;
    s->mintsts = 0;
    s->rintsts = 0;
    s->status = 0;
    s->fifoth = 0x00700700;
    /*
     * Synopsys DW MMC CDETECT is active-low.  The VisionFive2 U-Boot
     * snps_dw_mmc glue returns card-present when bit 0 reads as 0.
     */
    s->cdetect = s->card_present ? 0 : 1;
    s->wrtprt = 0;
    s->gpio = 0;
    s->tcbcnt = 0;
    s->tbbcnt = 0;
    s->debnce = 0x00FFFFFF;
    s->usrid = SDMMC_USRID_VAL;
    s->verid = SDMMC_VERID_VAL;
    s->hcon = SDMMC_HCON_VAL;
    s->uhs_reg = 0;
    s->rst_n = 0;
    s->bmod = 0;
    s->dbaddr = 0;
    s->idsts = 0;
    s->idinten = 0;
    s->uhs_reg_ext = 0;
    s->ddr_reg = 0;
    s->enable_shift = 0;
    s->fifo_pos = 0;
    s->fifo_count = 0;
    s->fifo_half = 0;
    s->pre_init = true;
    memset(s->fifo, 0, sizeof(s->fifo));
}

static uint64_t pz7110_sdio_read(void *opaque, hwaddr addr, unsigned int size)
{
    PZ7110SdioState *s = PZ7110_SDIO(opaque);
    uint64_t val = 0;

    switch (addr) {
    case SDMMC_CTRL:
        val = s->ctrl;
        break;
    case SDMMC_PWREN:
        val = s->pwren;
        break;
    case SDMMC_CLKDIV:
        val = s->clkdiv;
        break;
    case SDMMC_CLKSRC:
        val = s->clksrc;
        break;
    case SDMMC_CLKENA:
        val = s->clkena;
        break;
    case SDMMC_TMOUT:
        val = s->tmout;
        break;
    case SDMMC_CTYPE:
        val = s->ctype;
        break;
    case SDMMC_BLKSIZ:
        val = s->blksiz;
        break;
    case SDMMC_BYTCNT:
        val = s->bytcnt;
        break;
    case SDMMC_INTMASK:
        val = s->intmask;
        break;
    case SDMMC_CMDARG:
        val = s->cmdarg;
        break;
    case SDMMC_CMD:
        val = s->cmd;
        break;
    case SDMMC_RESP0:
        val = s->resp[0];
        break;
    case SDMMC_RESP1:
        val = s->resp[1];
        break;
    case SDMMC_RESP2:
        val = s->resp[2];
        break;
    case SDMMC_RESP3:
        val = s->resp[3];
        break;
    case SDMMC_MINTSTS:
        val = s->mintsts;
        break;
    case SDMMC_RINTSTS:
        val = s->rintsts;
        break;
    case SDMMC_STATUS:
        val = pz7110_sdio_status(s);
        break;
    case SDMMC_FIFOTH:
        val = s->fifoth;
        break;
    case SDMMC_CDETECT:
        val = s->cdetect;
        break;
    case SDMMC_WRTPRT:
        val = s->wrtprt;
        break;
    case SDMMC_GPIO:
        val = s->gpio;
        break;
    case SDMMC_TCBCNT:
        val = s->tcbcnt;
        break;
    case SDMMC_TBBCNT:
        val = s->tbbcnt;
        break;
    case SDMMC_DEBNCE:
        val = s->debnce;
        break;
    case SDMMC_USRID:
        val = s->usrid;
        break;
    case SDMMC_VERID:
        val = s->verid;
        break;
    case SDMMC_HCON:
        val = s->hcon;
        break;
    case SDMMC_UHS_REG:
        val = s->uhs_reg;
        break;
    case SDMMC_RST_N:
        val = s->rst_n;
        break;
    case SDMMC_BMOD:
        val = s->bmod;
        break;
    case SDMMC_DBADDR:
        val = s->dbaddr;
        break;
    case SDMMC_IDSTS:
        val = s->idsts;
        break;
    case SDMMC_IDINTEN:
        val = s->idinten;
        break;
    case SDMMC_CDTHRCTL:
        val = 0;
        break;
    case SDMMC_UHS_REG_EXT:
        val = s->uhs_reg_ext;
        break;
    case SDMMC_DDR_REG:
        val = s->ddr_reg;
        break;
    case SDMMC_ENABLE_SHIFT:
        val = s->enable_shift;
        break;
    case SDMMC_FIFO:
        /* FIFO read */
        if (s->fifo_count > 0) {
            if (size == 2) {
                uint32_t word = s->fifo[s->fifo_pos];

                if (s->fifo_half) {
                    val = (word >> 16) & 0xffff;
                    s->fifo_half = 0;
                    s->fifo_pos = (s->fifo_pos + 1) % SDMMC_FIFO_DEPTH;
                    s->fifo_count--;
                } else {
                    val = word & 0xffff;
                    s->fifo_half = 1;
                }
            } else {
                val = s->fifo[s->fifo_pos];
                s->fifo_pos = (s->fifo_pos + 1) % SDMMC_FIFO_DEPTH;
                s->fifo_count--;
                s->fifo_half = 0;
            }
        } else {
            val = 0;
        }
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pz7110_sdio_write(void *opaque, hwaddr addr, uint64_t val64,
                               unsigned int size)
{
    PZ7110SdioState *s = PZ7110_SDIO(opaque);
    uint32_t val = (uint32_t)val64;

    switch (addr) {
    case SDMMC_CTRL:
        s->ctrl = val;
        /* Handle reset bits */
        if (val & SDMMC_CTRL_RESET) {
            s->ctrl &= ~SDMMC_CTRL_RESET;
        }
        if (val & SDMMC_CTRL_FIFO_RESET) {
            s->fifo_pos = 0;
            s->fifo_count = 0;
            s->fifo_half = 0;
            s->ctrl &= ~SDMMC_CTRL_FIFO_RESET;
        }
        if (val & SDMMC_CTRL_DMA_RESET) {
            s->ctrl &= ~SDMMC_CTRL_DMA_RESET;
        }
        break;
    case SDMMC_PWREN:
        s->pwren = val;
        if (val & 1) {
            sdbus_set_voltage(&s->sdbus, 3300);
        }
        break;
    case SDMMC_CLKDIV:
        s->clkdiv = val;
        break;
    case SDMMC_CLKSRC:
        s->clksrc = val;
        break;
    case SDMMC_CLKENA:
        s->clkena = val;
        break;
    case SDMMC_TMOUT:
        s->tmout = val;
        break;
    case SDMMC_CTYPE:
        s->ctype = val;
        break;
    case SDMMC_BLKSIZ:
        s->blksiz = val;
        break;
    case SDMMC_BYTCNT:
        s->bytcnt = val;
        break;
    case SDMMC_INTMASK:
        s->intmask = val;
        pz7110_sdio_update_irq(s);
        break;
    case SDMMC_CMDARG:
        s->cmdarg = val;
        break;
    case SDMMC_CMD:
        s->cmd = val;
        if (val & SDMMC_CMD_START) {
            pz7110_sdio_do_command(s, val);
        }
        break;
    case SDMMC_RINTSTS:
        /* Write 1 to clear */
        pz7110_sdio_clear_irq_bits(s, val);
        break;
    case SDMMC_STATUS:
        /* Read-only, ignore writes */
        break;
    case SDMMC_FIFOTH:
        s->fifoth = val;
        break;
    case SDMMC_CDETECT:
        /* Read-only */
        break;
    case SDMMC_WRTPRT:
        s->wrtprt = val;
        break;
    case SDMMC_GPIO:
        s->gpio = val;
        break;
    case SDMMC_DEBNCE:
        s->debnce = val;
        break;
    case SDMMC_UHS_REG:
        s->uhs_reg = val;
        break;
    case SDMMC_RST_N:
        s->rst_n = val;
        break;
    case SDMMC_BMOD:
        s->bmod = val;
        if (val & SDMMC_BMOD_RESET) {
            s->bmod &= ~SDMMC_BMOD_RESET;
        }
        break;
    case SDMMC_DBADDR:
        s->dbaddr = val;
        break;
    case SDMMC_IDSTS:
        s->idsts &= ~val;
        break;
    case SDMMC_IDINTEN:
        s->idinten = val;
        break;
    case SDMMC_CDTHRCTL:
        /* Card detect threshold, ignore */
        break;
    case SDMMC_UHS_REG_EXT:
        s->uhs_reg_ext = val;
        break;
    case SDMMC_DDR_REG:
        s->ddr_reg = val;
        break;
    case SDMMC_ENABLE_SHIFT:
        s->enable_shift = val;
        break;
    case SDMMC_FIFO:
        /* FIFO write */
        if (s->fifo_count < SDMMC_FIFO_DEPTH) {
            s->fifo[(s->fifo_pos + s->fifo_count) % SDMMC_FIFO_DEPTH] = val;
            s->fifo_count++;
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pz7110_sdio_ops = {
    .read = pz7110_sdio_read,
    .write = pz7110_sdio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 2,
    .valid.max_access_size = 4,
};

static void pz7110_sdio_init(Object *obj)
{
    PZ7110SdioState *s = PZ7110_SDIO(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->iomem, obj, &pz7110_sdio_ops,
                          s, "pz7110-sdio", 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

    qbus_init(&s->sdbus, sizeof(s->sdbus), TYPE_SD_BUS, DEVICE(obj), "sd-bus");
}

static void pz7110_sdio_realize(DeviceState *dev, Error **errp)
{
    PZ7110SdioState *s = PZ7110_SDIO(dev);

    /* Default frequency: 50 MHz (typical SD card clock) */
    if (s->freq == 0) {
        s->freq = 50000000;
    }
}

static const VMStateDescription pz7110_sdio_vmstate = {
    .name = TYPE_PZ7110_SDIO,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ctrl, PZ7110SdioState),
        VMSTATE_UINT32(pwren, PZ7110SdioState),
        VMSTATE_UINT32(clkdiv, PZ7110SdioState),
        VMSTATE_UINT32(clksrc, PZ7110SdioState),
        VMSTATE_UINT32(clkena, PZ7110SdioState),
        VMSTATE_UINT32(tmout, PZ7110SdioState),
        VMSTATE_UINT32(ctype, PZ7110SdioState),
        VMSTATE_UINT32(blksiz, PZ7110SdioState),
        VMSTATE_UINT32(bytcnt, PZ7110SdioState),
        VMSTATE_UINT32(intmask, PZ7110SdioState),
        VMSTATE_UINT32(cmdarg, PZ7110SdioState),
        VMSTATE_UINT32(cmd, PZ7110SdioState),
        VMSTATE_UINT32_ARRAY(resp, PZ7110SdioState, 4),
        VMSTATE_UINT32(mintsts, PZ7110SdioState),
        VMSTATE_UINT32(rintsts, PZ7110SdioState),
        VMSTATE_UINT32(status, PZ7110SdioState),
        VMSTATE_UINT32(fifoth, PZ7110SdioState),
        VMSTATE_UINT32(cdetect, PZ7110SdioState),
        VMSTATE_UINT32(wrtprt, PZ7110SdioState),
        VMSTATE_UINT32(gpio, PZ7110SdioState),
        VMSTATE_UINT32(tcbcnt, PZ7110SdioState),
        VMSTATE_UINT32(tbbcnt, PZ7110SdioState),
        VMSTATE_UINT32(debnce, PZ7110SdioState),
        VMSTATE_UINT32(usrid, PZ7110SdioState),
        VMSTATE_UINT32(verid, PZ7110SdioState),
        VMSTATE_UINT32(hcon, PZ7110SdioState),
        VMSTATE_UINT32(uhs_reg, PZ7110SdioState),
        VMSTATE_UINT32(rst_n, PZ7110SdioState),
        VMSTATE_UINT32(bmod, PZ7110SdioState),
        VMSTATE_UINT32(dbaddr, PZ7110SdioState),
        VMSTATE_UINT32(idsts, PZ7110SdioState),
        VMSTATE_UINT32(idinten, PZ7110SdioState),
        VMSTATE_UINT32(uhs_reg_ext, PZ7110SdioState),
        VMSTATE_UINT32(ddr_reg, PZ7110SdioState),
        VMSTATE_UINT32(enable_shift, PZ7110SdioState),
        VMSTATE_UINT32(freq, PZ7110SdioState),
        VMSTATE_BOOL(card_present, PZ7110SdioState),
        VMSTATE_BOOL(emmc_mode, PZ7110SdioState),
        VMSTATE_BOOL(pre_init, PZ7110SdioState),
        VMSTATE_END_OF_LIST()
    }
};

static void pz7110_sdio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pz7110_sdio_realize;
    device_class_set_legacy_reset(dc, pz7110_sdio_reset);
    dc->vmsd = &pz7110_sdio_vmstate;
}

static const TypeInfo pz7110_sdio_info = {
    .name          = TYPE_PZ7110_SDIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110SdioState),
    .instance_init = pz7110_sdio_init,
    .class_init    = pz7110_sdio_class_init,
};

static void pz7110_sdio_register_types(void)
{
    type_register_static(&pz7110_sdio_info);
}

type_init(pz7110_sdio_register_types)
