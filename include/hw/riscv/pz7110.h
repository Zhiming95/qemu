/*
 * QEMU RISC-V PZ7110 SoC machine interface
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_PZ7110_H
#define HW_RISCV_PZ7110_H

#include "hw/boards.h"
#include "hw/riscv/pz7110_crg.h"
#include "hw/riscv/pz7110_dma.h"
#include "hw/riscv/pz7110_gmac.h"
#include "hw/riscv/pz7110_i2c.h"
#include "hw/riscv/pz7110_iomux.h"
#include "hw/riscv/pz7110_rtc.h"
#include "hw/riscv/pz7110_sdio.h"
#include "hw/riscv/pz7110_syscon.h"
#include "hw/riscv/pz7110_sfctemp.h"
#include "hw/riscv/pz7110_timer.h"
#include "hw/riscv/pz7110_trng.h"
#include "hw/riscv/pz7110_vout_crg.h"
#include "hw/riscv/pz7110_wdt.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/ssi/cadence_qspi.h"

#define TYPE_RISCV_PZ7110_MACHINE MACHINE_TYPE_NAME("pz7110")
typedef struct RISCVPZ7110State RISCVPZ7110State;
DECLARE_INSTANCE_CHECKER(RISCVPZ7110State, RISCV_PZ7110_MACHINE,
                         TYPE_RISCV_PZ7110_MACHINE)

struct RISCVPZ7110State {
    MachineState parent;

    RISCVHartArrayState e_cpus;
    RISCVHartArrayState u_cpus;
    CadenceQSPIState qspi;
    PZ7110SYSCRGState sys_crg;
    PZ7110STGCRGState stg_crg;
    PZ7110AONCRGState aon_crg;
    PZ7110SYSSYSCONState sys_syscon;
    PZ7110STGSYSCONState stg_syscon;
    PZ7110AONSYSCONState aon_syscon;
    PZ7110SysIOMUXState sys_iomux;
    PZ7110AONIOMUXState aon_iomux;
    PZ7110SdioState sdio0;
    PZ7110SdioState sdio1;
    PZ7110SFCTempState temp;
    PZ7110TimerState timer;
    PZ7110RtcState rtc;
    PZ7110TrngState trng;
    PZ7110GmacState gmac0;
    PZ7110GmacState gmac1;
    PZ7110DmaState dma;
    PZ7110VOUTCRGState vout_crg;
    PZ7110WdtState wdt;
    MemoryRegion ccache_mmio;
    MemoryRegion pmu_mmio;
    uint32_t pmu_power_mode;
};

enum {
    PZ7110_MROM,
    PZ7110_SRAM,
    PZ7110_CLINT,
    PZ7110_PLIC,
    PZ7110_UART0,
    PZ7110_QSPI0,
    PZ7110_QSPI_XIP,
    PZ7110_SYS_CRG_IDX,
    PZ7110_STG_CRG_IDX,
    PZ7110_AON_CRG_IDX,
    PZ7110_SYS_SYSCON_IDX,
    PZ7110_STG_SYSCON_IDX,
    PZ7110_AON_SYSCON_IDX,
    PZ7110_SYS_IOMUX_IDX,
    PZ7110_AON_IOMUX_IDX,
    PZ7110_I2C0,
    PZ7110_I2C1,
    PZ7110_I2C2,
    PZ7110_I2C3,
    PZ7110_I2C4,
    PZ7110_I2C5,
    PZ7110_I2C6,
    PZ7110_SPI0,
    PZ7110_SPI1,
    PZ7110_SPI2,
    PZ7110_SPI3,
    PZ7110_SPI4,
    PZ7110_SPI5,
    PZ7110_SPI6,
    PZ7110_SDIO0_IDX,
    PZ7110_SDIO1_IDX,
    PZ7110_SFCTEMP_IDX,
    PZ7110_TIMER_IDX,
    PZ7110_RTC_IDX,
    PZ7110_TRNG_IDX,
    PZ7110_GMAC0_IDX,
    PZ7110_GMAC1_IDX,
    PZ7110_DMA_IDX,
    PZ7110_VOUT_CRG_IDX,
    PZ7110_WDT_IDX,
    PZ7110_DRAM,
};

enum {
    QSPI0_IRQ = 25,
    UART0_IRQ = 32,
    I2C0_IRQ = 35,
    I2C1_IRQ = 36,
    I2C2_IRQ = 37,
    I2C3_IRQ = 48,
    I2C4_IRQ = 49,
    I2C5_IRQ = 50,
    I2C6_IRQ = 51,
    SPI0_IRQ = 38,
    SPI1_IRQ = 39,
    SPI2_IRQ = 40,
    SPI3_IRQ = 52,
    SPI4_IRQ = 53,
    SPI5_IRQ = 54,
    SPI6_IRQ = 55,
    SDIO0_IRQ = 74,
    SDIO1_IRQ = 75,
    SFCTEMP_IRQ = 81,
    TIMER0_IRQ = 69,
    TIMER1_IRQ = 70,
    TIMER2_IRQ = 71,
    TIMER3_IRQ = 72,
    RTC_MS_PULSE_IRQ = 10,
    RTC_SEC_PULSE_IRQ = 11,
    RTC_IRQ = 12,
    TRNG_IRQ = 30,
    GMAC0_IRQ = 7,
    GMAC1_IRQ = 78,
    DMA_IRQ = 73,
    WDT_IRQ = 68,
    AON_GPIO_IRQ = 85,
    SYS_GPIO_IRQ = 86,
};

#define PZ7110_HART_COUNT 5
#define PZ7110_PLIC_NUM_CONTEXTS (1 + (PZ7110_HART_COUNT - 1) * 2)
#define PZ7110_PLIC_NUM_SOURCES 137
#define PZ7110_PLIC_NUM_PRIO_BITS 3

#define PZ7110_PLIC_PRIORITY_BASE  0x00
#define PZ7110_PLIC_PENDING_BASE   0x1000
#define PZ7110_PLIC_ENABLE_BASE    0x2000
#define PZ7110_PLIC_ENABLE_STRIDE  0x80
#define PZ7110_PLIC_CONTEXT_BASE   0x200000
#define PZ7110_PLIC_CONTEXT_STRIDE 0x1000
#define PZ7110_PLIC_SIZE 0x4000000

#endif
