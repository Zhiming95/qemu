/*
 * QEMU RISC-V PZ7110 SoC machine interface
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_PZ7110_H
#define HW_RISCV_PZ7110_H

#include "hw/boards.h"
#include "hw/riscv/riscv_hart.h"

#define TYPE_RISCV_PZ7110_MACHINE MACHINE_TYPE_NAME("pz7110")
typedef struct RISCVPZ7110State RISCVPZ7110State;
DECLARE_INSTANCE_CHECKER(RISCVPZ7110State, RISCV_PZ7110_MACHINE,
                         TYPE_RISCV_PZ7110_MACHINE)

struct RISCVPZ7110State {
    MachineState parent;

    RISCVHartArrayState e_cpus;
    RISCVHartArrayState u_cpus;
};

enum {
    PZ7110_MROM,
    PZ7110_SRAM,
    PZ7110_CLINT,
    PZ7110_PLIC,
    PZ7110_UART0,
    PZ7110_DRAM,
};

enum {
    UART0_IRQ = 32,
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
