/*
 * PZ7110 SYS CRG (Clock & Reset Generator)
 *
 * Implements the SYS CRG register block at 0x17000000.
 * Contains ~198 registers for clock gating, dividers, muxes,
 * software reset, and reset status.
 *
 * Copyright (c) 2024 StarSix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/register.h"
#include "migration/vmstate.h"
#include "hw/riscv/pz7110_crg.h"

static void pz7110_sys_sw_reset0_post_write(RegisterInfo *reg, uint64_t val)
{
    PZ7110SYSCRGState *s = PZ7110_SYS_CRG(reg->opaque);

    s->regs[A_SYS_RST_STATUS0 / 4] = ~val;
}

static void pz7110_sys_sw_reset1_post_write(RegisterInfo *reg, uint64_t val)
{
    PZ7110SYSCRGState *s = PZ7110_SYS_CRG(reg->opaque);

    s->regs[A_SYS_RST_STATUS1 / 4] = ~val;
}

static void pz7110_sys_sw_reset2_post_write(RegisterInfo *reg, uint64_t val)
{
    PZ7110SYSCRGState *s = PZ7110_SYS_CRG(reg->opaque);

    s->regs[A_SYS_RST_STATUS2 / 4] = ~val;
}

static void pz7110_sys_sw_reset3_post_write(RegisterInfo *reg, uint64_t val)
{
    PZ7110SYSCRGState *s = PZ7110_SYS_CRG(reg->opaque);

    s->regs[A_SYS_RST_STATUS3 / 4] = ~val;
}

static void pz7110_stg_sw_reset_post_write(RegisterInfo *reg, uint64_t val)
{
    PZ7110STGCRGState *s = PZ7110_STG_CRG(reg->opaque);

    s->regs[A_STG_RST_STATUS / 4] = ~val;
}

static void pz7110_aon_sw_reset_post_write(RegisterInfo *reg, uint64_t val)
{
    PZ7110AONCRGState *s = PZ7110_AON_CRG(reg->opaque);

    s->regs[A_AON_RST_STATUS / 4] = ~val;
}

/* Reset values extracted from TRM Section 2.8.1 */
static const RegisterAccessInfo pz7110_sys_crg_regs_info[] = {
    /* CPU / Bus Root */
    { .name = "SYS_CLK_CPU_ROOT", .addr = A_SYS_CLK_CPU_ROOT,
      .rsvd = 0xc0000000,
    },
    { .name = "SYS_CLK_CPU_CORE", .addr = A_SYS_CLK_CPU_CORE,
      .reset = 0x00000001,
      .rsvd = 0xff000000,
    },
    { .name = "SYS_CLK_CPU_BUS", .addr = A_SYS_CLK_CPU_BUS,
      .reset = 0x00000002,
      .rsvd = 0xff000000,
    },
    { .name = "SYS_CLK_GPU_ROOT", .addr = A_SYS_CLK_GPU_ROOT,
      .rsvd = 0xc0000000,
    },
    /* Peripheral / Bus Root */
    { .name = "SYS_CLK_PERIPH_ROOT", .addr = A_SYS_CLK_PERIPH_ROOT,
      .reset = 0x00000002,
      .rsvd = 0xc0000000,
    },
    { .name = "SYS_CLK_BUS_ROOT", .addr = A_SYS_CLK_BUS_ROOT,
      .rsvd = 0xc0000000,
    },
    { .name = "SYS_CLK_NOCSTG_BUS", .addr = A_SYS_CLK_NOCSTG_BUS,
      .reset = 0x00000003,
      .rsvd = 0xff000000,
    },
    { .name = "SYS_CLK_AXI_CFG0", .addr = A_SYS_CLK_AXI_CFG0,
      .reset = 0x00000003,
      .rsvd = 0xff000000,
    },
    { .name = "SYS_CLK_STG_AXIAHB", .addr = A_SYS_CLK_STG_AXIAHB,
      .reset = 0x00000002,
      .rsvd = 0xff000000,
    },
    { .name = "SYS_CLK_AHB0", .addr = A_SYS_CLK_AHB0,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_AHB1", .addr = A_SYS_CLK_AHB1,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_APB_BUS_FUNC", .addr = A_SYS_CLK_APB_BUS_FUNC,
      .reset = 0x00000004,
      .rsvd = 0xff000000,
    },
    { .name = "SYS_CLK_APB0", .addr = A_SYS_CLK_APB0,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_PLL0_DIV2", .addr = A_SYS_CLK_PLL0_DIV2,
      .reset = 0x00000002,
      .rsvd = 0xff000000,
    },
    { .name = "SYS_CLK_PLL1_DIV2", .addr = A_SYS_CLK_PLL1_DIV2,
      .reset = 0x00000002,
      .rsvd = 0xff000000,
    },
    { .name = "SYS_CLK_PLL2_DIV2", .addr = A_SYS_CLK_PLL2_DIV2,
      .reset = 0x00000002,
      .rsvd = 0xff000000,
    },
    /* Audio Root */
    { .name = "SYS_CLK_AUDIO_ROOT", .addr = A_SYS_CLK_AUDIO_ROOT,
      .reset = 0x00000002,
      .rsvd = 0xff000000,
    },
    { .name = "SYS_CLK_MCLK_INNER", .addr = A_SYS_CLK_MCLK_INNER,
      .reset = 0x0000000C,
      .rsvd = 0xff000000,
    },
    { .name = "SYS_CLK_MCLK", .addr = A_SYS_CLK_MCLK,
      .rsvd = 0xc0000000,
    },
    { .name = "SYS_CLK_MCLK_OUT", .addr = A_SYS_CLK_MCLK_OUT,
    },
    /* ISP */
    { .name = "SYS_CLK_ISP_2X", .addr = A_SYS_CLK_ISP_2X,
      .reset = 0x00000002,
      .rsvd = 0xc0000000,
    },
    { .name = "SYS_CLK_ISP_AXI", .addr = A_SYS_CLK_ISP_AXI,
      .reset = 0x00000002,
      .rsvd = 0xff000000,
    },
    /* GPU GCLK */
    { .name = "SYS_CLK_GCLK0", .addr = A_SYS_CLK_GCLK0,
      .reset = 0x80000014,
      .rsvd = 0x00000000,
    },
    { .name = "SYS_CLK_GCLK1", .addr = A_SYS_CLK_GCLK1,
      .reset = 0x80000010,
    },
    { .name = "SYS_CLK_GCLK2", .addr = A_SYS_CLK_GCLK2,
      .reset = 0x8000000C,
    },
    /* U7MC Core / Debug / Trace */
    { .name = "SYS_CLK_U7MC_CORE", .addr = A_SYS_CLK_U7MC_CORE,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_U7MC_CORE1", .addr = A_SYS_CLK_U7MC_CORE1,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_U7MC_CORE2", .addr = A_SYS_CLK_U7MC_CORE2,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_U7MC_CORE3", .addr = A_SYS_CLK_U7MC_CORE3,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_U7MC_CORE4", .addr = A_SYS_CLK_U7MC_CORE4,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_U7MC_DEBUG", .addr = A_SYS_CLK_U7MC_DEBUG,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_U7MC_RTC_TOGGLE", .addr = A_SYS_CLK_U7MC_RTC_TOGGLE,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_U7MC_TRACE0", .addr = A_SYS_CLK_U7MC_TRACE0,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_U7MC_TRACE1", .addr = A_SYS_CLK_U7MC_TRACE1,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_U7MC_TRACE2", .addr = A_SYS_CLK_U7MC_TRACE2,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_U7MC_TRACE3", .addr = A_SYS_CLK_U7MC_TRACE3,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_U7MC_TRACE4", .addr = A_SYS_CLK_U7MC_TRACE4,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_U7MC_TRACE_COM", .addr = A_SYS_CLK_U7MC_TRACE_COM,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_NOC_CPU_AXI", .addr = A_SYS_CLK_NOC_CPU_AXI,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_NOC_AXICFG0_AXI", .addr = A_SYS_CLK_NOC_AXICFG0_AXI,
      .reset = 0x80000000,
    },
    /* Divider / Derived clocks */
    { .name = "SYS_CLK_OSC_DIV2", .addr = A_SYS_CLK_OSC_DIV2,
      .reset = 0x00000002,
      .rsvd = 0xff000000,
    },
    { .name = "SYS_CLK_PLL1_DIV4", .addr = A_SYS_CLK_PLL1_DIV4,
      .reset = 0x00000002,
      .rsvd = 0xff000000,
    },
    { .name = "SYS_CLK_PLL1_DIV8", .addr = A_SYS_CLK_PLL1_DIV8,
      .reset = 0x00000002,
      .rsvd = 0xff000000,
    },
    /* DDR */
    { .name = "SYS_CLK_DDR_BUS", .addr = A_SYS_CLK_DDR_BUS,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_DDR_AXI", .addr = A_SYS_CLK_DDR_AXI,
      .reset = 0x80000000,
    },
    /* GPU */
    { .name = "SYS_CLK_GPU_CORE", .addr = A_SYS_CLK_GPU_CORE,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_GPU_CORE_ICG", .addr = A_SYS_CLK_GPU_CORE_ICG,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_GPU_SYS", .addr = A_SYS_CLK_GPU_SYS,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_GPU_APB", .addr = A_SYS_CLK_GPU_APB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_GPU_RTC_TOGGLE", .addr = A_SYS_CLK_GPU_RTC_TOGGLE,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_NOC_GPU_AXI", .addr = A_SYS_CLK_NOC_GPU_AXI,
      .reset = 0x80000000,
    },
    /* ISP instances */
    { .name = "SYS_CLK_ISP_INST_2X", .addr = A_SYS_CLK_ISP_INST_2X,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_ISP_INST_AXI", .addr = A_SYS_CLK_ISP_INST_AXI,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_NOC_ISP_AXI", .addr = A_SYS_CLK_NOC_ISP_AXI,
      .reset = 0x80000000,
    },
    /* HIFI4 */
    { .name = "SYS_CLK_HIFI4_CORE", .addr = A_SYS_CLK_HIFI4_CORE,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_HIFI4_AXI", .addr = A_SYS_CLK_HIFI4_AXI,
      .reset = 0x80000000,
    },
    /* AXI config */
    { .name = "SYS_CLK_AXI_CFG1_DEC_MAIN", .addr = A_SYS_CLK_AXI_CFG1_DEC_MAIN,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_AXI_CFG1_DEC_AHB", .addr = A_SYS_CLK_AXI_CFG1_DEC_AHB,
      .reset = 0x80000000,
    },
    /* Display */
    { .name = "SYS_CLK_VOUT_SRC", .addr = A_SYS_CLK_VOUT_SRC,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_VOUT_AXI_DIV", .addr = A_SYS_CLK_VOUT_AXI_DIV,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_NOC_DISP_AXI", .addr = A_SYS_CLK_NOC_DISP_AXI,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_VOUT_AHB", .addr = A_SYS_CLK_VOUT_AHB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_VOUT_AXI", .addr = A_SYS_CLK_VOUT_AXI,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_VOUT_HDMI_TX0_MCLK", .addr = A_SYS_CLK_VOUT_HDMI_TX0_MCLK,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_VOUT_MIPI_PHY_REF", .addr = A_SYS_CLK_VOUT_MIPI_PHY_REF,
      .reset = 0x80000000,
    },
    /* JPEG / Video decode / encode */
    { .name = "SYS_CLK_JPEG_AXI", .addr = A_SYS_CLK_JPEG_AXI,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_CODAJ12_AXI", .addr = A_SYS_CLK_CODAJ12_AXI,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_CODAJ12_CORE", .addr = A_SYS_CLK_CODAJ12_CORE,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_CODAJ12_APB", .addr = A_SYS_CLK_CODAJ12_APB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_VDEC_AXI_DIV", .addr = A_SYS_CLK_VDEC_AXI_DIV,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_WAVE511_AXI", .addr = A_SYS_CLK_WAVE511_AXI,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_WAVE511_BPU", .addr = A_SYS_CLK_WAVE511_BPU,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_WAVE511_VCE", .addr = A_SYS_CLK_WAVE511_VCE,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_WAVE511_APB", .addr = A_SYS_CLK_WAVE511_APB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_VDEC_JPG_ARB", .addr = A_SYS_CLK_VDEC_JPG_ARB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_VDEC_JPG_MAIN", .addr = A_SYS_CLK_VDEC_JPG_MAIN,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_NOC_VDEC_AXI", .addr = A_SYS_CLK_NOC_VDEC_AXI,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_VENC_AXI_DIV", .addr = A_SYS_CLK_VENC_AXI_DIV,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_WAVE420L_AXI", .addr = A_SYS_CLK_WAVE420L_AXI,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_WAVE420L_BPU", .addr = A_SYS_CLK_WAVE420L_BPU,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_WAVE420L_VCE", .addr = A_SYS_CLK_WAVE420L_VCE,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_WAVE420L_APB", .addr = A_SYS_CLK_WAVE420L_APB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_NOC_VENC_AXI", .addr = A_SYS_CLK_NOC_VENC_AXI,
      .reset = 0x80000000,
    },
    /* AXI config0 decoder */
    { .name = "SYS_CLK_AXI_CFG0_DEC_MAIN_DIV", .addr = A_SYS_CLK_AXI_CFG0_DEC_MAIN_DIV,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_AXI_CFG0_DEC_MAIN", .addr = A_SYS_CLK_AXI_CFG0_DEC_MAIN,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_AXI_CFG0_DEC_HIFI4", .addr = A_SYS_CLK_AXI_CFG0_DEC_HIFI4,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_AXIMEM_128B_AXI", .addr = A_SYS_CLK_AXIMEM_128B_AXI,
      .reset = 0x80000000,
    },
    /* QSPI */
    { .name = "SYS_CLK_QSPI_AHB", .addr = A_SYS_CLK_QSPI_AHB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_QSPI_APB", .addr = A_SYS_CLK_QSPI_APB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_QSPI_REF_SRC", .addr = A_SYS_CLK_QSPI_REF_SRC,
      .reset = 0x0000000a,
    },
    { .name = "SYS_CLK_QSPI_REF", .addr = A_SYS_CLK_QSPI_REF,
      .reset = 0x80000000,
    },
    /* SD */
    { .name = "SYS_CLK_SD0_AHB", .addr = A_SYS_CLK_SD0_AHB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_SD1_AHB", .addr = A_SYS_CLK_SD1_AHB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_SD0_CARD", .addr = A_SYS_CLK_SD0_CARD,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_SD1_CARD", .addr = A_SYS_CLK_SD1_CARD,
      .reset = 0x80000000,
    },
    /* USB / Storage NoC */
    { .name = "SYS_CLK_USB_125M", .addr = A_SYS_CLK_USB_125M,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_NOC_STG_AXI", .addr = A_SYS_CLK_NOC_STG_AXI,
      .reset = 0x80000000,
    },
    /* GMAC */
    { .name = "SYS_CLK_GMAC_AHB", .addr = A_SYS_CLK_GMAC_AHB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_GMAC_AXI", .addr = A_SYS_CLK_GMAC_AXI,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_GMAC_SRC", .addr = A_SYS_CLK_GMAC_SRC,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_GMAC1_GTX", .addr = A_SYS_CLK_GMAC1_GTX,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_GMAC1_RMII_RTX", .addr = A_SYS_CLK_GMAC1_RMII_RTX,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_GMAC_PTP", .addr = A_SYS_CLK_GMAC_PTP,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_GMAC_RX_DLY", .addr = A_SYS_CLK_GMAC_RX_DLY,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_GMAC_RX_INV", .addr = A_SYS_CLK_GMAC_RX_INV,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_GMAC_TX", .addr = A_SYS_CLK_GMAC_TX,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_GMAC_TX_INV", .addr = A_SYS_CLK_GMAC_TX_INV,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_GMAC1_GTXC_DLY", .addr = A_SYS_CLK_GMAC1_GTXC_DLY,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_GMAC0_GTX", .addr = A_SYS_CLK_GMAC0_GTX,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_GMAC0_PTP", .addr = A_SYS_CLK_GMAC0_PTP,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_GMAC_PHY", .addr = A_SYS_CLK_GMAC_PHY,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_GMAC0_GTXC_DLY", .addr = A_SYS_CLK_GMAC0_GTXC_DLY,
      .reset = 0x80000000,
    },
    /* System peripherals */
    { .name = "SYS_CLK_SYS_IOMUX_PCLK", .addr = A_SYS_CLK_SYS_IOMUX_PCLK,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_MAILBOX_APB", .addr = A_SYS_CLK_MAILBOX_APB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_INT_CTRL_APB", .addr = A_SYS_CLK_INT_CTRL_APB,
      .reset = 0x80000000,
    },
    /* CAN */
    { .name = "SYS_CLK_CAN0_APB", .addr = A_SYS_CLK_CAN0_APB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_CAN0_TIMER", .addr = A_SYS_CLK_CAN0_TIMER,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_CAN0_CAN", .addr = A_SYS_CLK_CAN0_CAN,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_CAN1_APB", .addr = A_SYS_CLK_CAN1_APB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_CAN1_TIMER", .addr = A_SYS_CLK_CAN1_TIMER,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_CAN1_CAN", .addr = A_SYS_CLK_CAN1_CAN,
      .reset = 0x80000000,
    },
    /* PWM / WDT / Timer */
    { .name = "SYS_CLK_PWM_APB", .addr = A_SYS_CLK_PWM_APB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_WDT_APB", .addr = A_SYS_CLK_WDT_APB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_WDT", .addr = A_SYS_CLK_WDT,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_TIMER_APB", .addr = A_SYS_CLK_TIMER_APB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_TIMER0", .addr = A_SYS_CLK_TIMER0,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_TIMER1", .addr = A_SYS_CLK_TIMER1,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_TIMER2", .addr = A_SYS_CLK_TIMER2,
    },
    { .name = "SYS_CLK_TIMER3", .addr = A_SYS_CLK_TIMER3,
    },
    /* Temperature sensor */
    { .name = "SYS_CLK_TEMP_SENSOR_APB", .addr = A_SYS_CLK_TEMP_SENSOR_APB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_TEMP_SENSOR", .addr = A_SYS_CLK_TEMP_SENSOR,
      .reset = 0x80000000,
    },
    /* SPI 0-6 */
    { .name = "SYS_CLK_SPI0_APB", .addr = A_SYS_CLK_SPI0_APB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_SPI1_APB", .addr = A_SYS_CLK_SPI1_APB,
    },
    { .name = "SYS_CLK_SPI2_APB", .addr = A_SYS_CLK_SPI2_APB,
    },
    { .name = "SYS_CLK_SPI3_APB", .addr = A_SYS_CLK_SPI3_APB,
    },
    { .name = "SYS_CLK_SPI4_APB", .addr = A_SYS_CLK_SPI4_APB,
    },
    { .name = "SYS_CLK_SPI5_APB", .addr = A_SYS_CLK_SPI5_APB,
    },
    { .name = "SYS_CLK_SPI6_APB", .addr = A_SYS_CLK_SPI6_APB,
    },
    /* I2C 0-6 */
    { .name = "SYS_CLK_I2C0_APB", .addr = A_SYS_CLK_I2C0_APB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_I2C1_APB", .addr = A_SYS_CLK_I2C1_APB,
    },
    { .name = "SYS_CLK_I2C2_APB", .addr = A_SYS_CLK_I2C2_APB,
    },
    { .name = "SYS_CLK_I2C3_APB", .addr = A_SYS_CLK_I2C3_APB,
    },
    { .name = "SYS_CLK_I2C4_APB", .addr = A_SYS_CLK_I2C4_APB,
    },
    { .name = "SYS_CLK_I2C5_APB", .addr = A_SYS_CLK_I2C5_APB,
    },
    { .name = "SYS_CLK_I2C6_APB", .addr = A_SYS_CLK_I2C6_APB,
    },
    /* UART 0-5 */
    { .name = "SYS_CLK_UART0_APB", .addr = A_SYS_CLK_UART0_APB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_UART0_CORE", .addr = A_SYS_CLK_UART0_CORE,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_UART1_APB", .addr = A_SYS_CLK_UART1_APB,
    },
    { .name = "SYS_CLK_UART1_CORE", .addr = A_SYS_CLK_UART1_CORE,
    },
    { .name = "SYS_CLK_UART2_APB", .addr = A_SYS_CLK_UART2_APB,
    },
    { .name = "SYS_CLK_UART2_CORE", .addr = A_SYS_CLK_UART2_CORE,
    },
    { .name = "SYS_CLK_UART3_APB", .addr = A_SYS_CLK_UART3_APB,
    },
    { .name = "SYS_CLK_UART3_CORE", .addr = A_SYS_CLK_UART3_CORE,
    },
    { .name = "SYS_CLK_UART4_APB", .addr = A_SYS_CLK_UART4_APB,
    },
    { .name = "SYS_CLK_UART4_CORE", .addr = A_SYS_CLK_UART4_CORE,
    },
    { .name = "SYS_CLK_UART5_APB", .addr = A_SYS_CLK_UART5_APB,
    },
    { .name = "SYS_CLK_UART5_CORE", .addr = A_SYS_CLK_UART5_CORE,
    },
    /* PWMDAC */
    { .name = "SYS_CLK_PWMDAC_APB", .addr = A_SYS_CLK_PWMDAC_APB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_PWMDAC_CORE", .addr = A_SYS_CLK_PWMDAC_CORE,
      .reset = 0x80000000,
    },
    /* SPDIF */
    { .name = "SYS_CLK_SPDIF_APB", .addr = A_SYS_CLK_SPDIF_APB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_SPDIF_CORE", .addr = A_SYS_CLK_SPDIF_CORE,
      .reset = 0x80000000,
    },
    /* I2S TX0 */
    { .name = "SYS_CLK_I2STX0_APB", .addr = A_SYS_CLK_I2STX0_APB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_I2STX0_BCLK_MST", .addr = A_SYS_CLK_I2STX0_BCLK_MST,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_I2STX0_BCLK_MST_INV", .addr = A_SYS_CLK_I2STX0_BCLK_MST_INV,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_I2STX0_LRCK_MST", .addr = A_SYS_CLK_I2STX0_LRCK_MST,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_I2STX0_BCLK", .addr = A_SYS_CLK_I2STX0_BCLK,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_I2STX0_BCLK_NEG", .addr = A_SYS_CLK_I2STX0_BCLK_NEG,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_I2STX0_LRCK", .addr = A_SYS_CLK_I2STX0_LRCK,
      .reset = 0x80000000,
    },
    /* I2S TX1 */
    { .name = "SYS_CLK_I2STX1_APB", .addr = A_SYS_CLK_I2STX1_APB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_I2STX1_BCLK_MST", .addr = A_SYS_CLK_I2STX1_BCLK_MST,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_I2STX1_BCLK_MST_INV", .addr = A_SYS_CLK_I2STX1_BCLK_MST_INV,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_I2STX1_LRCK_MST", .addr = A_SYS_CLK_I2STX1_LRCK_MST,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_I2STX1_BCLK", .addr = A_SYS_CLK_I2STX1_BCLK,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_I2STX1_BCLK_NEG", .addr = A_SYS_CLK_I2STX1_BCLK_NEG,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_I2STX1_LRCK", .addr = A_SYS_CLK_I2STX1_LRCK,
      .reset = 0x80000000,
    },
    /* I2S RX */
    { .name = "SYS_CLK_I2SRX_APB", .addr = A_SYS_CLK_I2SRX_APB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_I2SRX_BCLK_MST", .addr = A_SYS_CLK_I2SRX_BCLK_MST,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_I2SRX_BCLK_MST_INV", .addr = A_SYS_CLK_I2SRX_BCLK_MST_INV,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_I2SRX_LRCK_MST", .addr = A_SYS_CLK_I2SRX_LRCK_MST,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_I2SRX_BCLK", .addr = A_SYS_CLK_I2SRX_BCLK,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_I2SRX_BCLK_NEG", .addr = A_SYS_CLK_I2SRX_BCLK_NEG,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_I2SRX_LRCK", .addr = A_SYS_CLK_I2SRX_LRCK,
      .reset = 0x80000000,
    },
    /* PDM */
    { .name = "SYS_CLK_PDM_DMIC", .addr = A_SYS_CLK_PDM_DMIC,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_PDM_APB", .addr = A_SYS_CLK_PDM_APB,
      .reset = 0x80000000,
    },
    /* TDM */
    { .name = "SYS_CLK_TDM_AHB", .addr = A_SYS_CLK_TDM_AHB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_TDM_APB", .addr = A_SYS_CLK_TDM_APB,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_TDM_INTERNAL", .addr = A_SYS_CLK_TDM_INTERNAL,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_TDM", .addr = A_SYS_CLK_TDM,
      .reset = 0x80000000,
    },
    { .name = "SYS_CLK_TDM_NEG", .addr = A_SYS_CLK_TDM_NEG,
      .reset = 0x80000000,
    },
    /* JTAG / TRNG */
    { .name = "SYS_CLK_JTAG_TRNG", .addr = A_SYS_CLK_JTAG_TRNG,
      .reset = 0x80000000,
    },
    /* Software Reset */
    { .name = "SYS_SW_RESET0", .addr = A_SYS_SW_RESET0,
      .post_write = pz7110_sys_sw_reset0_post_write,
    },
    { .name = "SYS_SW_RESET1", .addr = A_SYS_SW_RESET1,
      .post_write = pz7110_sys_sw_reset1_post_write,
    },
    { .name = "SYS_SW_RESET2", .addr = A_SYS_SW_RESET2,
      .post_write = pz7110_sys_sw_reset2_post_write,
    },
    { .name = "SYS_SW_RESET3", .addr = A_SYS_SW_RESET3,
      .post_write = pz7110_sys_sw_reset3_post_write,
    },
    /* Reset Status (read-only) */
    { .name = "SYS_RST_STATUS0", .addr = A_SYS_RST_STATUS0,
    },
    { .name = "SYS_RST_STATUS1", .addr = A_SYS_RST_STATUS1,
    },
    { .name = "SYS_RST_STATUS2", .addr = A_SYS_RST_STATUS2,
    },
    { .name = "SYS_RST_STATUS3", .addr = A_SYS_RST_STATUS3,
    },
};

static void pz7110_sys_crg_reset_enter(Object *obj, ResetType type)
{
    PZ7110SYSCRGState *s = PZ7110_SYS_CRG(obj);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
}

static const MemoryRegionOps pz7110_sys_crg_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void pz7110_sys_crg_init(Object *obj)
{
    PZ7110SYSCRGState *s = PZ7110_SYS_CRG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->reg_array = register_init_block32(
        DEVICE(obj),
        pz7110_sys_crg_regs_info,
        ARRAY_SIZE(pz7110_sys_crg_regs_info),
        s->regs_info,
        s->regs,
        &pz7110_sys_crg_ops,
        false,
        PZ7110_SYS_CRG_R_MAX * 4
    );
    sysbus_init_mmio(sbd, &s->reg_array->mem);
}

static void pz7110_sys_crg_finalize(Object *obj)
{
    register_finalize_block(PZ7110_SYS_CRG(obj)->reg_array);
}

static const VMStateDescription vmstate_pz7110_sys_crg = {
    .name = TYPE_PZ7110_SYS_CRG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, PZ7110SYSCRGState,
                             PZ7110_SYS_CRG_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void pz7110_sys_crg_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PZ7110 SYS CRG (Clock & Reset Generator)";
    dc->vmsd = &vmstate_pz7110_sys_crg;
    rc->phases.enter = pz7110_sys_crg_reset_enter;
}

static const TypeInfo pz7110_sys_crg_info = {
    .name          = TYPE_PZ7110_SYS_CRG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110SYSCRGState),
    .class_init    = pz7110_sys_crg_class_init,
    .instance_init = pz7110_sys_crg_init,
    .instance_finalize = pz7110_sys_crg_finalize,
};

static void pz7110_sys_crg_register_types(void)
{
    type_register_static(&pz7110_sys_crg_info);
}

type_init(pz7110_sys_crg_register_types)

/* ================================================================
 * STG CRG (Storage Domain Clock & Reset Generator)
 * Base: 0x10230000, 31 registers, offset 0x00 - 0x78
 * ================================================================ */

static const RegisterAccessInfo pz7110_stg_crg_regs_info[] = {
    { .name = "STG_CLK_HIFI4_CORE", .addr = A_STG_CLK_HIFI4_CORE },
    { .name = "STG_CLK_USB_APB", .addr = A_STG_CLK_USB_APB },
    { .name = "STG_CLK_USB_UTMI_APB", .addr = A_STG_CLK_USB_UTMI_APB },
    { .name = "STG_CLK_USB_AXI", .addr = A_STG_CLK_USB_AXI },
    { .name = "STG_CLK_USB_IPM", .addr = A_STG_CLK_USB_IPM,
      .reset = 0x00000002,
    },
    { .name = "STG_CLK_USB_STB", .addr = A_STG_CLK_USB_STB,
      .reset = 0x00000004,
    },
    { .name = "STG_CLK_USB_APP_125", .addr = A_STG_CLK_USB_APP_125 },
    { .name = "STG_CLK_USB_REF", .addr = A_STG_CLK_USB_REF,
      .reset = 0x00000002,
    },
    { .name = "STG_CLK_PCIE0_AXI_MST0", .addr = A_STG_CLK_PCIE0_AXI_MST0 },
    { .name = "STG_CLK_PCIE0_APB", .addr = A_STG_CLK_PCIE0_APB },
    { .name = "STG_CLK_PCIE0_TL", .addr = A_STG_CLK_PCIE0_TL },
    { .name = "STG_CLK_PCIE1_AXI_MST0", .addr = A_STG_CLK_PCIE1_AXI_MST0 },
    { .name = "STG_CLK_PCIE1_APB", .addr = A_STG_CLK_PCIE1_APB },
    { .name = "STG_CLK_PCIE1_TL", .addr = A_STG_CLK_PCIE1_TL },
    { .name = "STG_CLK_PCIE_SLV_DEC_MAIN", .addr = A_STG_CLK_PCIE_SLV_DEC_MAIN,
      .reset = 0x80000000,
    },
    { .name = "STG_CLK_SECURITY_HCLK", .addr = A_STG_CLK_SECURITY_HCLK },
    { .name = "STG_CLK_SECURITY_MISC_AHB", .addr = A_STG_CLK_SECURITY_MISC_AHB },
    { .name = "STG_CLK_MTRX_GRP0_MAIN", .addr = A_STG_CLK_MTRX_GRP0_MAIN,
      .reset = 0x80000000,
    },
    { .name = "STG_CLK_MTRX_GRP0_BUS", .addr = A_STG_CLK_MTRX_GRP0_BUS,
      .reset = 0x80000000,
    },
    { .name = "STG_CLK_MTRX_GRP0_STG", .addr = A_STG_CLK_MTRX_GRP0_STG,
      .reset = 0x80000000,
    },
    { .name = "STG_CLK_MTRX_GRP1_MAIN", .addr = A_STG_CLK_MTRX_GRP1_MAIN,
      .reset = 0x80000000,
    },
    { .name = "STG_CLK_MTRX_GRP1_BUS", .addr = A_STG_CLK_MTRX_GRP1_BUS,
      .reset = 0x80000000,
    },
    { .name = "STG_CLK_MTRX_GRP1_STG", .addr = A_STG_CLK_MTRX_GRP1_STG,
      .reset = 0x80000000,
    },
    { .name = "STG_CLK_MTRX_GRP1_HIFI", .addr = A_STG_CLK_MTRX_GRP1_HIFI,
      .reset = 0x80000000,
    },
    { .name = "STG_CLK_E2_RTC", .addr = A_STG_CLK_E2_RTC,
      .reset = 0x00000018,
    },
    { .name = "STG_CLK_E2_CORE", .addr = A_STG_CLK_E2_CORE,
      .reset = 0x80000000,
    },
    { .name = "STG_CLK_E2_DBG", .addr = A_STG_CLK_E2_DBG },
    { .name = "STG_CLK_DMA_AXI", .addr = A_STG_CLK_DMA_AXI },
    { .name = "STG_CLK_DMA_AHB", .addr = A_STG_CLK_DMA_AHB },
    { .name = "STG_SW_RESET", .addr = A_STG_SW_RESET,
      .reset = 0x007FFFFE,
      .post_write = pz7110_stg_sw_reset_post_write,
    },
    { .name = "STG_RST_STATUS", .addr = A_STG_RST_STATUS,
      .reset = 0x007FFFFE,
    },
};

static void pz7110_stg_crg_reset_enter(Object *obj, ResetType type)
{
    PZ7110STGCRGState *s = PZ7110_STG_CRG(obj);
    unsigned int i;
    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
}

static void pz7110_stg_crg_init(Object *obj)
{
    PZ7110STGCRGState *s = PZ7110_STG_CRG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->reg_array = register_init_block32(
        DEVICE(obj),
        pz7110_stg_crg_regs_info,
        ARRAY_SIZE(pz7110_stg_crg_regs_info),
        s->regs_info,
        s->regs,
        &pz7110_sys_crg_ops,
        false,
        PZ7110_STG_CRG_R_MAX * 4
    );
    sysbus_init_mmio(sbd, &s->reg_array->mem);
}

static void pz7110_stg_crg_finalize(Object *obj)
{
    register_finalize_block(PZ7110_STG_CRG(obj)->reg_array);
}

static const VMStateDescription vmstate_pz7110_stg_crg = {
    .name = TYPE_PZ7110_STG_CRG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, PZ7110STGCRGState,
                             PZ7110_STG_CRG_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void pz7110_stg_crg_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PZ7110 STG CRG (Storage Domain Clock & Reset)";
    dc->vmsd = &vmstate_pz7110_stg_crg;
    rc->phases.enter = pz7110_stg_crg_reset_enter;
}

static const TypeInfo pz7110_stg_crg_info = {
    .name          = TYPE_PZ7110_STG_CRG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110STGCRGState),
    .class_init    = pz7110_stg_crg_class_init,
    .instance_init = pz7110_stg_crg_init,
    .instance_finalize = pz7110_stg_crg_finalize,
};

static void pz7110_stg_crg_register_types(void)
{
    type_register_static(&pz7110_stg_crg_info);
}

type_init(pz7110_stg_crg_register_types)

/* ================================================================
 * AON CRG (Always-On Domain Clock & Reset Generator)
 * Base: 0x17000000, 16 registers, offset 0x00 - 0x3C
 * ================================================================ */

static const RegisterAccessInfo pz7110_aon_crg_regs_info[] = {
    { .name = "AON_CLK_OSC", .addr = A_AON_CLK_OSC,
      .reset = 0x00000004,
    },
    { .name = "AON_CLK_APB_FUNC", .addr = A_AON_CLK_APB_FUNC },
    { .name = "AON_CLK_GMAC5_AHB", .addr = A_AON_CLK_GMAC5_AHB },
    { .name = "AON_CLK_GMAC5_AXI", .addr = A_AON_CLK_GMAC5_AXI },
    { .name = "AON_CLK_GMAC0_RMII_RTX", .addr = A_AON_CLK_GMAC0_RMII_RTX,
      .reset = 0x00000002,
    },
    { .name = "AON_CLK_GMAC5_AXI64_TX", .addr = A_AON_CLK_GMAC5_AXI64_TX },
    { .name = "AON_CLK_GMAC5_AXI64_TX_INV", .addr = A_AON_CLK_GMAC5_AXI64_TX_INV,
      .reset = 0x40000000,
    },
    { .name = "AON_CLK_GMAC5_AXI64_RX", .addr = A_AON_CLK_GMAC5_AXI64_RX },
    { .name = "AON_CLK_GMAC5_AXI64_RX_INV", .addr = A_AON_CLK_GMAC5_AXI64_RX_INV,
      .reset = 0x40000000,
    },
    { .name = "AON_CLK_OTPC_APB", .addr = A_AON_CLK_OTPC_APB,
      .reset = 0x80000000,
    },
    { .name = "AON_CLK_RTC_HMS_APB", .addr = A_AON_CLK_RTC_HMS_APB,
      .reset = 0x80000000,
    },
    { .name = "AON_CLK_RTC_INTERNAL", .addr = A_AON_CLK_RTC_INTERNAL,
      .reset = 0x000002EE,
    },
    { .name = "AON_CLK_RTC_HMS_OSC32K", .addr = A_AON_CLK_RTC_HMS_OSC32K },
    { .name = "AON_CLK_RTC_HMS_CAL", .addr = A_AON_CLK_RTC_HMS_CAL },
    { .name = "AON_SW_RESET", .addr = A_AON_SW_RESET,
      .reset = 0x000000E3,
      .post_write = pz7110_aon_sw_reset_post_write,
    },
    { .name = "AON_RST_STATUS", .addr = A_AON_RST_STATUS,
      .reset = 0x000000E3,
    },
};

static void pz7110_aon_crg_reset_enter(Object *obj, ResetType type)
{
    PZ7110AONCRGState *s = PZ7110_AON_CRG(obj);
    unsigned int i;
    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
}

static void pz7110_aon_crg_init(Object *obj)
{
    PZ7110AONCRGState *s = PZ7110_AON_CRG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->reg_array = register_init_block32(
        DEVICE(obj),
        pz7110_aon_crg_regs_info,
        ARRAY_SIZE(pz7110_aon_crg_regs_info),
        s->regs_info,
        s->regs,
        &pz7110_sys_crg_ops,
        false,
        PZ7110_AON_CRG_R_MAX * 4
    );
    sysbus_init_mmio(sbd, &s->reg_array->mem);
}

static void pz7110_aon_crg_finalize(Object *obj)
{
    register_finalize_block(PZ7110_AON_CRG(obj)->reg_array);
}

static const VMStateDescription vmstate_pz7110_aon_crg = {
    .name = TYPE_PZ7110_AON_CRG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, PZ7110AONCRGState,
                             PZ7110_AON_CRG_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void pz7110_aon_crg_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PZ7110 AON CRG (Always-On Clock & Reset)";
    dc->vmsd = &vmstate_pz7110_aon_crg;
    rc->phases.enter = pz7110_aon_crg_reset_enter;
}

static const TypeInfo pz7110_aon_crg_info = {
    .name          = TYPE_PZ7110_AON_CRG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PZ7110AONCRGState),
    .class_init    = pz7110_aon_crg_class_init,
    .instance_init = pz7110_aon_crg_init,
    .instance_finalize = pz7110_aon_crg_finalize,
};

static void pz7110_aon_crg_register_types(void)
{
    type_register_static(&pz7110_aon_crg_info);
}

type_init(pz7110_aon_crg_register_types)
