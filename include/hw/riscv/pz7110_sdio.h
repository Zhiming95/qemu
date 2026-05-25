/*
 * QEMU PZ7110 Synopsys DesignWare Mobile Storage Host Controller
 *
 * Copyright (c) 2024 StarSix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#ifndef HW_RISCV_PZ7110_SDIO_H
#define HW_RISCV_PZ7110_SDIO_H

#include "hw/sysbus.h"
#include "hw/sd/sd.h"

#define TYPE_PZ7110_SDIO "pz7110-sdio"
OBJECT_DECLARE_SIMPLE_TYPE(PZ7110SdioState, PZ7110_SDIO)

/* DW MMC register offsets (from dw_mmc.h) */
#define SDMMC_CTRL        0x000
#define SDMMC_PWREN       0x004
#define SDMMC_CLKDIV      0x008
#define SDMMC_CLKSRC      0x00c
#define SDMMC_CLKENA      0x010
#define SDMMC_TMOUT       0x014
#define SDMMC_CTYPE       0x018
#define SDMMC_BLKSIZ      0x01c
#define SDMMC_BYTCNT      0x020
#define SDMMC_INTMASK     0x024
#define SDMMC_CMDARG      0x028
#define SDMMC_CMD         0x02c
#define SDMMC_RESP0       0x030
#define SDMMC_RESP1       0x034
#define SDMMC_RESP2       0x038
#define SDMMC_RESP3       0x03c
#define SDMMC_MINTSTS     0x040
#define SDMMC_RINTSTS     0x044
#define SDMMC_STATUS      0x048
#define SDMMC_FIFOTH      0x04c
#define SDMMC_CDETECT     0x050
#define SDMMC_WRTPRT      0x054
#define SDMMC_GPIO        0x058
#define SDMMC_TCBCNT      0x05c
#define SDMMC_TBBCNT      0x060
#define SDMMC_DEBNCE      0x064
#define SDMMC_USRID       0x068
#define SDMMC_VERID       0x06c
#define SDMMC_HCON        0x070
#define SDMMC_UHS_REG     0x074
#define SDMMC_RST_N       0x078
#define SDMMC_BMOD        0x080
#define SDMMC_PLDMND      0x084
#define SDMMC_DBADDR      0x088
#define SDMMC_IDSTS       0x08c
#define SDMMC_IDINTEN     0x090
#define SDMMC_DSCADDR     0x094
#define SDMMC_BUFADDR     0x098
#define SDMMC_CDTHRCTL    0x100
#define SDMMC_UHS_REG_EXT 0x108
#define SDMMC_DDR_REG     0x10c
#define SDMMC_ENABLE_SHIFT 0x110
#define SDMMC_FIFO        0x200

/* Control register bits */
#define SDMMC_CTRL_DMA_ENABLE      BIT(5)
#define SDMMC_CTRL_INT_ENABLE      BIT(4)
#define SDMMC_CTRL_DMA_RESET       BIT(2)
#define SDMMC_CTRL_FIFO_RESET      BIT(1)
#define SDMMC_CTRL_RESET           BIT(0)
#define SDMMC_CTRL_USE_IDMAC       BIT(25)

/* Clock enable bits */
#define SDMMC_CLKEN_ENABLE         BIT(0)
#define SDMMC_CLKEN_LOW_PWR        BIT(16)

/* Interrupt bits */
#define SDMMC_INT_CD               BIT(0)
#define SDMMC_INT_RESP_ERR         BIT(1)
#define SDMMC_INT_CMD_DONE         BIT(2)
#define SDMMC_INT_DATA_OVER        BIT(3)
#define SDMMC_INT_TXDR             BIT(4)
#define SDMMC_INT_RXDR             BIT(5)
#define SDMMC_INT_RCRC             BIT(6)
#define SDMMC_INT_DCRC             BIT(7)
#define SDMMC_INT_RTO              BIT(8)
#define SDMMC_INT_DRTO             BIT(9)
#define SDMMC_INT_HTO              BIT(10)
#define SDMMC_INT_FRUN             BIT(11)
#define SDMMC_INT_HLE              BIT(12)
#define SDMMC_INT_SBE              BIT(13)
#define SDMMC_INT_ACD              BIT(14)
#define SDMMC_INT_EBE              BIT(15)

/* Status register bits */
#define SDMMC_STATUS_BUSY          BIT(9)
#define SDMMC_STATUS_DMA_REQ       BIT(31)
#define SDMMC_GET_FCNT(x)          (((x) >> 17) & 0x1FFF)

/* Command register bits */
#define SDMMC_CMD_START            BIT(31)
#define SDMMC_CMD_USE_HOLD_REG     BIT(29)
#define SDMMC_CMD_INDX(n)          ((n) & 0x3F)
#define SDMMC_CMD_RESP_EXP         BIT(6)
#define SDMMC_CMD_RESP_LONG        BIT(7)
#define SDMMC_CMD_RESP_CRC         BIT(8)
#define SDMMC_CMD_DAT_EXP          BIT(9)
#define SDMMC_CMD_DAT_WR           BIT(10)
#define SDMMC_CMD_SEND_STOP        BIT(12)
#define SDMMC_CMD_PRV_DAT_WAIT     BIT(13)
#define SDMMC_CMD_STOP             BIT(14)
#define SDMMC_CMD_INIT             BIT(15)
#define SDMMC_CMD_UPD_CLK          BIT(21)
#define SDMMC_CMD_VOLT_SWITCH      BIT(28)

/* Card type bits */
#define SDMMC_CTYPE_8BIT           BIT(16)
#define SDMMC_CTYPE_4BIT           BIT(0)

/* Version ID (DesignWare MMC 4.80a) */
#define SDMMC_VERID_VAL            0x240a

/* User ID */
#define SDMMC_USRID_VAL            0x53442020  /* "SD  " */

/* Hardware configuration */
#define SDMMC_HCON_VAL             0x00000000

/* FIFO size: 512 bytes = 128 x 32-bit words */
#define SDMMC_FIFO_SIZE            512
#define SDMMC_FIFO_DEPTH           128

/* Bus mode register bits */
#define SDMMC_BMOD_RESET           BIT(0)
#define SDMMC_BMOD_FB              BIT(1)
#define SDMMC_BMOD_ENABLE          BIT(7)

typedef struct PZ7110SdioState {
    /*< private >*/
    SysBusDevice parent;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq     irq;
    SDBus        sdbus;
    bool         card_present;
    bool         emmc_mode;

    uint32_t ctrl;
    uint32_t pwren;
    uint32_t clkdiv;
    uint32_t clksrc;
    uint32_t clkena;
    uint32_t tmout;
    uint32_t ctype;
    uint32_t blksiz;
    uint32_t bytcnt;
    uint32_t intmask;
    uint32_t cmdarg;
    uint32_t cmd;
    uint32_t resp[4];
    uint32_t mintsts;
    uint32_t rintsts;
    uint32_t status;
    uint32_t fifoth;
    uint32_t cdetect;
    uint32_t wrtprt;
    uint32_t gpio;
    uint32_t tcbcnt;
    uint32_t tbbcnt;
    uint32_t debnce;
    uint32_t usrid;
    uint32_t verid;
    uint32_t hcon;
    uint32_t uhs_reg;
    uint32_t rst_n;
    uint32_t bmod;
    uint32_t dbaddr;
    uint32_t idsts;
    uint32_t idinten;
    uint32_t uhs_reg_ext;
    uint32_t ddr_reg;
    uint32_t enable_shift;
    uint32_t fifo[SDMMC_FIFO_DEPTH];
    int      fifo_pos;
    int      fifo_count;
    int      fifo_half;

    uint32_t freq;      /* CIU clock frequency (Hz) */
    bool     pre_init;  /* suppress SD probe cmds before eMMC init */
} PZ7110SdioState;

#endif
