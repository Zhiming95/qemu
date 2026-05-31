/*
 * QEMU RISC-V PZ7110 SoC device interface
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_PZ7110_SOC_H
#define HW_RISCV_PZ7110_SOC_H

#include "hw/boards.h"
#include "hw/cpu/cluster.h"
#include "hw/riscv/pz7110_ccache.h"
#include "hw/riscv/pz7110_crypto.h"
#include "hw/riscv/pz7110_crg.h"
#include "hw/riscv/pz7110_dma.h"
#include "hw/riscv/pz7110_gmac.h"
#include "hw/riscv/pz7110_i2c.h"
#include "hw/riscv/pz7110_iomux.h"
#include "hw/riscv/pz7110_pcie.h"
#include "hw/riscv/pz7110_pmu.h"
#include "hw/riscv/pz7110_pwm.h"
#include "hw/riscv/pz7110_rtc.h"
#include "hw/riscv/pz7110_sdio.h"
#include "hw/riscv/pz7110_syscon.h"
#include "hw/riscv/pz7110_sfctemp.h"
#include "hw/riscv/pz7110_timer.h"
#include "hw/riscv/pz7110_trng.h"
#include "hw/riscv/pz7110_usb.h"
#include "hw/riscv/pz7110_vout_crg.h"
#include "hw/riscv/pz7110_wdt.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/ssi/cadence_qspi.h"

#define TYPE_PZ7110_SOC "pz7110.soc"
OBJECT_DECLARE_SIMPLE_TYPE(PZ7110SoCState, PZ7110_SOC)

struct PZ7110SoCState {
    DeviceState parent;

    const MemMapEntry *memmap;
    MemoryRegion        *sram_mr;

    CPUClusterState     s7_cluster;
    CPUClusterState     u74_cluster;
    RISCVHartArrayState s7_cpus;
    RISCVHartArrayState u74_cpus;
    CadenceQSPIState    qspi;
    PZ7110CcacheState   ccache;
    PZ7110CryptoState   crypto;
    PZ7110SYSCRGState   sys_crg;
    PZ7110STGCRGState   stg_crg;
    PZ7110AONCRGState   aon_crg;
    PZ7110SYSSYSCONState  sys_syscon;
    PZ7110STGSYSCONState  stg_syscon;
    PZ7110AONSYSCONState  aon_syscon;
    PZ7110SysIOMUXState   sys_iomux;
    PZ7110AONIOMUXState   aon_iomux;
    PZ7110SdioState     sdio0;
    PZ7110SdioState     sdio1;
    PZ7110SFCTempState  temp;
    PZ7110TimerState    timer;
    PZ7110RtcState      rtc;
    PZ7110TrngState     trng;
    PZ7110GmacState     gmac0;
    PZ7110GmacState     gmac1;
    PZ7110DmaState      dma;
    PZ7110PwmState      pwm;
    PZ7110UsbState      usb;
    PZ7110PcieState     pcie0;
    PZ7110PcieState     pcie1;
    PZ7110PmuState      pmu;
    PZ7110VOUTCRGState  vout_crg;
    PZ7110WdtState      wdt;
};

#endif
