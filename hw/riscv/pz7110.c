/*
 * QEMU RISC-V PZ7110 SoC machine shell
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/char/serial-mm.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/sifive_plic.h"
#include "hw/loader.h"
#include "hw/riscv/pz7110.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/sysbus.h"
#include "system/system.h"
#include "target/riscv/cpu.h"

static const MemMapEntry pz7110_memmap[] = {
    [PZ7110_MROM] = { 0x2a000000, 0x10000 },
    [PZ7110_SRAM] = { 0x08000000, 0x200000 },
    [PZ7110_CLINT] = { 0x02000000, 0x10000 },
    [PZ7110_PLIC] = { 0x0c000000, PZ7110_PLIC_SIZE },
    [PZ7110_UART0] = { 0x10000000, 0x10000 },
    [PZ7110_DRAM] = { 0x40000000, 0x0 },
};

static DeviceState *pz7110_create_plic(const MemMapEntry *memmap,
                                       int base_hartid, int hart_count)
{
    g_autofree char *plic_hart_config = g_strdup("M,MS,MS,MS,MS");

    return sifive_plic_create(memmap[PZ7110_PLIC].base,
                              plic_hart_config,
                              hart_count,
                              base_hartid,
                              PZ7110_PLIC_NUM_SOURCES,
                              (1U << PZ7110_PLIC_NUM_PRIO_BITS) - 1,
                              PZ7110_PLIC_PRIORITY_BASE,
                              PZ7110_PLIC_PENDING_BASE,
                              PZ7110_PLIC_ENABLE_BASE,
                              PZ7110_PLIC_ENABLE_STRIDE,
                              PZ7110_PLIC_CONTEXT_BASE,
                              PZ7110_PLIC_CONTEXT_STRIDE,
                              memmap[PZ7110_PLIC].size);
}

static void pz7110_machine_init(MachineState *machine)
{
    RISCVPZ7110State *s = RISCV_PZ7110_MACHINE(machine);
    const MemMapEntry *memmap = pz7110_memmap;
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    DeviceState *irqchip;
    uint32_t park_loop[] = {
        0x10500073, /* wfi */
        0xffdff06f, /* j . */
    };

    object_initialize_child(OBJECT(machine), "e-cpus", &s->e_cpus,
                            TYPE_RISCV_HART_ARRAY);
    object_property_set_str(OBJECT(&s->e_cpus), "cpu-type",
                            machine->cpu_type, &error_abort);
    object_property_set_int(OBJECT(&s->e_cpus), "hartid-base", 0,
                            &error_abort);
    object_property_set_int(OBJECT(&s->e_cpus), "num-harts", 1,
                            &error_abort);
    object_property_set_int(OBJECT(&s->e_cpus), "resetvec",
                            memmap[PZ7110_MROM].base + 0x100,
                            &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->e_cpus), &error_fatal);

    object_initialize_child(OBJECT(machine), "u-cpus", &s->u_cpus,
                            TYPE_RISCV_HART_ARRAY);
    object_property_set_str(OBJECT(&s->u_cpus), "cpu-type",
                            machine->cpu_type, &error_abort);
    object_property_set_int(OBJECT(&s->u_cpus), "hartid-base", 1,
                            &error_abort);
    object_property_set_int(OBJECT(&s->u_cpus), "num-harts",
                            PZ7110_HART_COUNT - 1, &error_abort);
    object_property_set_int(OBJECT(&s->u_cpus), "resetvec",
                            memmap[PZ7110_MROM].base, &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->u_cpus), &error_fatal);

    riscv_aclint_swi_create(memmap[PZ7110_CLINT].base,
                            0, PZ7110_HART_COUNT, false);
    riscv_aclint_mtimer_create(memmap[PZ7110_CLINT].base +
                               RISCV_ACLINT_SWI_SIZE,
                               RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
                               0, PZ7110_HART_COUNT,
                               RISCV_ACLINT_DEFAULT_MTIMECMP,
                               RISCV_ACLINT_DEFAULT_MTIME,
                               RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ,
                               true);

    irqchip = pz7110_create_plic(memmap, 0, PZ7110_HART_COUNT);

    memory_region_add_subregion(system_memory, memmap[PZ7110_DRAM].base,
                                machine->ram);

    memory_region_init_rom(mask_rom, NULL, "pz7110.mrom",
                           memmap[PZ7110_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[PZ7110_MROM].base,
                                mask_rom);

    memory_region_init_ram(sram, NULL, "pz7110.sram",
                           memmap[PZ7110_SRAM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[PZ7110_SRAM].base,
                                sram);

    for (int i = 0; i < ARRAY_SIZE(park_loop); i++) {
        park_loop[i] = cpu_to_le32(park_loop[i]);
    }

    rom_add_blob_fixed_as("mrom.u74-park", park_loop, sizeof(park_loop),
                          memmap[PZ7110_MROM].base, &address_space_memory);
    rom_add_blob_fixed_as("mrom.s7-park", park_loop, sizeof(park_loop),
                          memmap[PZ7110_MROM].base + 0x100,
                          &address_space_memory);

    serial_mm_init(system_memory, memmap[PZ7110_UART0].base,
                   2, qdev_get_gpio_in(irqchip, UART0_IRQ), 24000000,
                   serial_hd(0), DEVICE_LITTLE_ENDIAN);
}

static void pz7110_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "VisionFive V2 / PZ7110";
    mc->init = pz7110_machine_init;
    mc->max_cpus = PZ7110_HART_COUNT;
    mc->min_cpus = PZ7110_HART_COUNT;
    mc->default_cpus = PZ7110_HART_COUNT;
    mc->default_cpu_type = TYPE_RISCV_CPU_BASE;
    mc->default_ram_id = "pz7110.ram";
    mc->default_ram_size = 4 * GiB;
    mc->auto_create_sdcard = false;
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
