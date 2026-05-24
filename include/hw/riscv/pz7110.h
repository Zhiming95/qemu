/*
 * QEMU RISC-V PZ7110 SoC machine interface
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_PZ7110_H
#define HW_RISCV_PZ7110_H

#include "hw/boards.h"

#define TYPE_RISCV_PZ7110_MACHINE MACHINE_TYPE_NAME("pz7110")
typedef struct RISCVPZ7110State RISCVPZ7110State;
DECLARE_INSTANCE_CHECKER(RISCVPZ7110State, RISCV_PZ7110_MACHINE,
                         TYPE_RISCV_PZ7110_MACHINE)

struct RISCVPZ7110State {
    MachineState parent;
};

#define PZ7110_HART_COUNT 5

#endif
