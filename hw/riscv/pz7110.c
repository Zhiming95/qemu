/*
 * QEMU RISC-V PZ7110 SoC machine shell
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "hw/boards.h"
#include "hw/riscv/pz7110.h"
#include "target/riscv/cpu.h"

static void pz7110_machine_init(MachineState *machine)
{
    error_report("PZ7110 machine skeleton has no bootable devices yet");
    exit(1);
}

static void pz7110_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "VisionFive V2 / (PZ7110 skeleton)";
    mc->init = pz7110_machine_init;
    mc->max_cpus = PZ7110_HART_COUNT;
    mc->default_cpus = PZ7110_HART_COUNT;
    mc->default_cpu_type = TYPE_RISCV_CPU_BASE;
    mc->default_ram_id = "riscv.pz7110.ram";
    mc->default_ram_size = 4 * GiB;
}

static const TypeInfo pz7110_machine_typeinfo = {
    .name = TYPE_RISCV_PZ7110_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(RISCVPZ7110State),
    .class_init = pz7110_machine_class_init,
};

static void pz7110_machine_init_register_types(void)
{
    type_register_static(&pz7110_machine_typeinfo);
}

type_init(pz7110_machine_init_register_types)
