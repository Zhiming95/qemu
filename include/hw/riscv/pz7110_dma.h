/*
 * QEMU PZ7110 DesignWare AXI DMA Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_PZ7110_DMA_H
#define HW_RISCV_PZ7110_DMA_H

#include "hw/sysbus.h"

#define TYPE_PZ7110_DMA "pz7110-dma"
OBJECT_DECLARE_SIMPLE_TYPE(PZ7110DmaState, PZ7110_DMA)

#define PZ7110_DMA_NUM_CHANNELS 4

#define DMAC_ID               0x000
#define DMAC_COMPVER          0x008
#define DMAC_CFG              0x010
#define DMAC_CHEN             0x018
#define DMAC_CHSUSPREG        0x020
#define DMAC_INTSTATUS        0x030
#define DMAC_INTSTATUS_ENA    0x040
#define DMAC_INTSIGNAL_ENA    0x048
#define DMAC_COMMON_INTSTATUS 0x050
#define DMAC_RESET            0x058

#define CH_SAR                0x000
#define CH_DAR                0x008
#define CH_BLOCK_TS           0x010
#define CH_CTL                0x018
#define CH_CFG                0x020
#define CH_LLP                0x028
#define CH_STATUS             0x030

#define CHAN_REG_LEN          0x100

typedef struct PZ7110DmaChannel {
    uint32_t sar;
    uint32_t dar;
    uint32_t block_ts;
    uint32_t ctl;
    uint32_t cfg;
    uint32_t llp;
    uint32_t status;
} PZ7110DmaChannel;

typedef struct PZ7110DmaState {
    SysBusDevice parent;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t dmac_id;
    uint32_t dmac_compver;
    uint32_t dmac_cfg;
    uint32_t chen;
    uint32_t chsuspend;
    uint32_t intstatus;
    uint32_t intstatus_ena;
    uint32_t intsignal_ena;
    uint32_t common_intstatus;

    PZ7110DmaChannel channels[PZ7110_DMA_NUM_CHANNELS];
} PZ7110DmaState;

#endif
