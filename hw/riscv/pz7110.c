/*
 * QEMU RISC-V PZ7110 SoC machine shell
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "system/block-backend-global-state.h"
#include "system/block-backend-io.h"
#include "system/blockdev.h"
#include "hw/boards.h"
#include "hw/char/serial-mm.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/sifive_plic.h"
#include "hw/loader.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/pz7110.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/sd/sd.h"
#include "hw/sysbus.h"
#include "net/net.h"
#include "system/system.h"
#include "target/riscv/cpu.h"
#include <libfdt.h>

static RISCVException pz7110_csr_any(CPURISCVState *env, int csrno)
{
    return RISCV_EXCP_NONE;
}

static RISCVException pz7110_read_zero(CPURISCVState *env, int csrno,
                                       target_ulong *val)
{
    *val = 0;
    return RISCV_EXCP_NONE;
}

static RISCVException pz7110_write_ignore(CPURISCVState *env, int csrno,
                                          target_ulong val)
{
    return RISCV_EXCP_NONE;
}

static const MemMapEntry pz7110_memmap[] = {
    [PZ7110_MROM] = { 0x2a000000, 0x10000 },
    [PZ7110_SRAM] = { 0x08000000, 0x200000 },
    [PZ7110_CLINT] = { 0x02000000, 0x10000 },
    [PZ7110_PLIC] = { 0x0c000000, PZ7110_PLIC_SIZE },
    [PZ7110_UART0] = { 0x10000000, 0x10000 },
    [PZ7110_QSPI0] = { 0x13010000, 0x10000 },
    [PZ7110_QSPI_XIP] = { 0x21000000, 0x8000000 },
    [PZ7110_SYS_CRG_IDX] = { 0x13020000, 0x10000 },
    [PZ7110_STG_CRG_IDX] = { 0x10230000, 0x10000 },
    [PZ7110_AON_CRG_IDX] = { 0x17000000, 0x10000 },
    [PZ7110_SYS_SYSCON_IDX] = { 0x13030000, 0x10000 },
    [PZ7110_STG_SYSCON_IDX] = { 0x10240000, 0x10000 },
    [PZ7110_AON_SYSCON_IDX] = { 0x17010000, 0x10000 },
    [PZ7110_SYS_IOMUX_IDX] = { 0x13040000, 0x10000 },
    [PZ7110_AON_IOMUX_IDX] = { 0x17020000, 0x10000 },
    [PZ7110_I2C0] = { 0x10030000, 0x10000 },
    [PZ7110_I2C1] = { 0x10040000, 0x10000 },
    [PZ7110_I2C2] = { 0x10050000, 0x10000 },
    [PZ7110_I2C3] = { 0x12030000, 0x10000 },
    [PZ7110_I2C4] = { 0x12040000, 0x10000 },
    [PZ7110_I2C5] = { 0x12050000, 0x10000 },
    [PZ7110_I2C6] = { 0x12060000, 0x10000 },
    [PZ7110_SDIO0_IDX] = { 0x16010000, 0x10000 },
    [PZ7110_SDIO1_IDX] = { 0x16020000, 0x10000 },
    [PZ7110_SFCTEMP_IDX] = { 0x120e0000, 0x10000 },
    [PZ7110_TIMER_IDX] = { 0x13050000, 0x10000 },
    [PZ7110_RTC_IDX] = { 0x17040000, 0x10000 },
    [PZ7110_TRNG_IDX] = { 0x1600c000, 0x4000 },
    [PZ7110_GMAC0_IDX] = { 0x16030000, 0x10000 },
    [PZ7110_GMAC1_IDX] = { 0x16040000, 0x10000 },
    [PZ7110_VOUT_CRG_IDX] = { 0x295c0000, 0x10000 },
    [PZ7110_DRAM] = { 0x40000000, 0x0 },
};

static uint64_t pz7110_quiet_stub_read(void *opaque, hwaddr addr,
                                       unsigned int size)
{
    return 0;
}

static void pz7110_quiet_stub_write(void *opaque, hwaddr addr, uint64_t value,
                                    unsigned int size)
{
}

static const MemoryRegionOps pz7110_quiet_stub_ops = {
    .read = pz7110_quiet_stub_read,
    .write = pz7110_quiet_stub_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
};

static uint64_t pz7110_pmu_read(void *opaque, hwaddr addr, unsigned int size)
{
    RISCVPZ7110State *s = opaque;

    switch (addr) {
    case 0x80: /* CURR_POWER_MODE */
        return s->pmu_power_mode;
    case 0x88: /* PMU_EVENT_STATUS */
    case 0x8c: /* PMU_INT_STATUS */
        return 0;
    default:
        return 0;
    }
}

static void pz7110_pmu_write(void *opaque, hwaddr addr, uint64_t value,
                             unsigned int size)
{
    RISCVPZ7110State *s = opaque;
    uint32_t mask = value;

    switch (addr) {
    case 0x0c: /* SW_TURN_ON_POWER_MODE */
        s->pmu_power_mode |= mask;
        break;
    case 0x10: /* SW_TURN_OFF_POWER_MODE */
        s->pmu_power_mode &= ~mask;
        /*
         * SYSTOP and CPU domains remain available in the QEMU model.
         * U-Boot/OpenSBI may touch reset paths after failed probes.
         */
        s->pmu_power_mode |= 0x3;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pz7110_pmu_ops = {
    .read = pz7110_pmu_read,
    .write = pz7110_pmu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static uint64_t pz7110_qspi_xip_read(void *opaque, hwaddr addr, unsigned size)
{
    CadenceQSPIState *s = opaque;
    uint64_t value = 0;
    uint32_t flash_addr;

    if ((s->regs[0x60 / 4] & BIT(0)) &&
        s->indirect_ahb_offset < s->indirect_bytes) {
        flash_addr = s->indirect_addr + s->indirect_ahb_offset;
        s->indirect_ahb_offset += size;
        if (s->indirect_ahb_offset > s->indirect_bytes) {
            s->indirect_ahb_offset = s->indirect_bytes;
        }
    } else {
        flash_addr = addr;
    }

    for (unsigned i = 0; i < size; i++) {
        uint8_t byte = 0xff;

        if (flash_addr + i < s->flash_size) {
            byte = s->flash_data[flash_addr + i];
        }
        value |= (uint64_t)byte << (i * 8);
    }

    return value;
}

static void pz7110_qspi_xip_write(void *opaque, hwaddr addr, uint64_t value,
                                  unsigned size)
{
    CadenceQSPIState *s = opaque;
    uint32_t flash_addr = addr;

    if ((s->regs[0x70 / 4] & BIT(0)) && flash_addr < s->flash_size) {
        for (unsigned i = 0; i < size && flash_addr + i < s->flash_size; i++) {
            s->flash_data[flash_addr + i] = extract64(value, i * 8, 8);
        }
    }
}

static const MemoryRegionOps pz7110_qspi_xip_ops = {
    .read = pz7110_qspi_xip_read,
    .write = pz7110_qspi_xip_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

typedef struct PZ7110DdrStubState {
    uint32_t regs[0x10000 / 4];
    unsigned status518_reads;
} PZ7110DdrStubState;

static uint64_t pz7110_ddr_stub_read(void *opaque, hwaddr addr, unsigned size)
{
    PZ7110DdrStubState *s = opaque;

    switch (addr) {
    case 0x504:
        return 0x80000000;
    case 0x518:
        return s->status518_reads++ == 0 ? 0x2 : 0x0;
    default:
        if (addr + size <= sizeof(s->regs)) {
            return s->regs[addr >> 2];
        }
        return 0;
    }
}

static void pz7110_ddr_stub_write(void *opaque, hwaddr addr, uint64_t value,
                                  unsigned size)
{
    PZ7110DdrStubState *s = opaque;

    if (addr == 0x514) {
        s->status518_reads = 0;
    }
    if (addr + size <= sizeof(s->regs)) {
        s->regs[addr >> 2] = value;
    }
}

static const MemoryRegionOps pz7110_ddr_stub_ops = {
    .read = pz7110_ddr_stub_read,
    .write = pz7110_ddr_stub_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t pz7110_ccache_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pz7110_ccache_write(void *opaque, hwaddr addr, uint64_t value,
                                unsigned size)
{
}

static const MemoryRegionOps pz7110_ccache_ops = {
    .read = pz7110_ccache_read,
    .write = pz7110_ccache_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static void pz7110_create_quiet_stub(const char *name, hwaddr base,
                                     hwaddr size)
{
    MemoryRegion *mr = g_new0(MemoryRegion, 1);

    memory_region_init_io(mr, NULL, &pz7110_quiet_stub_ops, NULL, name, size);
    memory_region_add_subregion(get_system_memory(), base, mr);
}

static void pz7110_create_ddr_stub(const char *name, hwaddr base)
{
    MemoryRegion *mr = g_new0(MemoryRegion, 1);
    PZ7110DdrStubState *s = g_new0(PZ7110DdrStubState, 1);

    memory_region_init_io(mr, NULL, &pz7110_ddr_stub_ops, s, name, 0x10000);
    memory_region_add_subregion(get_system_memory(), base, mr);
}

static void pz7110_create_i2c(hwaddr base, qemu_irq irq, bool eeprom)
{
    DeviceState *dev = qdev_new(TYPE_PZ7110_I2C);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    qdev_prop_set_bit(dev, "eeprom", eeprom);
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map(sbd, 0, base);
    sysbus_connect_irq(sbd, 0, irq);
}

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

static ssize_t pz7110_patch_spl_dtb(void *image, size_t image_size)
{
    static const char * const spl_nodes[] = {
        "/soc",
        "/soc/spi@13010000",
        "/soc/spi@13010000/nor-flash@0",
    };
    static const char spl_spi0_path[] = "/soc/spi@13010000";
    const size_t extra = 1024;
    char *base = image;

    for (size_t off = 0; off + sizeof(struct fdt_header) < image_size;
         off += 4) {
        void *dtb = base + off;
        int totalsize;
        void *patched;

        if (fdt_magic(dtb) != FDT_MAGIC || fdt_check_header(dtb)) {
            continue;
        }

        totalsize = fdt_totalsize(dtb);
        if (totalsize <= 0 || off + totalsize > image_size) {
            continue;
        }

        /*
         * VisionFive2 SPL's generated DTB has /firmware spi0 pointing at
         * an old qspi@11860000 path. Replace it in place so this works even
         * when the DTB has no spare room for fdt_setprop().
         */
        {
            int firmware = fdt_path_offset(dtb, "/firmware");
            int len = 0;
            char *spi0;

            if (firmware >= 0) {
                spi0 = (char *)fdt_getprop(dtb, firmware, "spi0", &len);
                if (spi0 && len >= sizeof(spl_spi0_path)) {
                    memset(spi0, 0, len);
                    memcpy(spi0, spl_spi0_path, sizeof(spl_spi0_path));
                }
            }
        }

        if (off + totalsize + extra <= image_size) {
            patched = g_malloc0(totalsize + extra);
            if (fdt_open_into(dtb, patched, totalsize + extra)) {
                g_free(patched);
                return off;
            }

            for (int i = 0; i < ARRAY_SIZE(spl_nodes); i++) {
                int node = fdt_path_offset(patched, spl_nodes[i]);

                if (node >= 0) {
                    fdt_setprop(patched, node, "u-boot,dm-spl", NULL, 0);
                }
            }

            memcpy(dtb, patched, fdt_totalsize(patched));
            g_free(patched);
        }

        return off;
    }

    return -1;
}

static void pz7110_machine_init(MachineState *machine)
{
    RISCVPZ7110State *s = RISCV_PZ7110_MACHINE(machine);
    const MemMapEntry *memmap = pz7110_memmap;
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    MemoryRegion *xip = g_new(MemoryRegion, 1);
    DeviceState *irqchip;
    DriveInfo *dinfo;
    const char *firmware_name;
    hwaddr firmware_load_addr = memmap[PZ7110_SRAM].base;
    target_ulong firmware_end_addr;
    ssize_t spl_dtb_offset = -1;
    uint64_t spl_fdt_load_addr = 0;
    uint32_t park_loop[] = {
        0x10500073, /* wfi */
        0xffdff06f, /* j . */
    };

    /*
     * SPL writes the SiFive U74 feature-disable CSR during early M-mode
     * setup.  Keep this machine-local; do not modify global CSR tables for
     * unrelated RISC-V machines.
     */
    {
        static riscv_csr_operations u74_csr = {
            .name = "u74_feature_disable",
            .predicate = pz7110_csr_any,
            .read = pz7110_read_zero,
            .write = pz7110_write_ignore,
        };

        csr_ops[0x7c1] = u74_csr;
    }

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

    object_initialize_child(OBJECT(machine), "qspi", &s->qspi,
                            TYPE_CADENCE_QSPI);
    sysbus_realize(SYS_BUS_DEVICE(&s->qspi), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->qspi), 0,
                    memmap[PZ7110_QSPI0].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->qspi), 0,
                       qdev_get_gpio_in(irqchip, QSPI0_IRQ));

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

    s->qspi.flash_size = memmap[PZ7110_QSPI_XIP].size;
    s->qspi.flash_data = g_malloc(s->qspi.flash_size);
    memset(s->qspi.flash_data, 0xff, s->qspi.flash_size);
    memory_region_init_io(xip, NULL, &pz7110_qspi_xip_ops, &s->qspi,
                          "pz7110.qspi_xip",
                          memmap[PZ7110_QSPI_XIP].size);
    memory_region_add_subregion(system_memory, memmap[PZ7110_QSPI_XIP].base,
                                xip);

    memory_region_init_io(&s->ccache_mmio, OBJECT(machine),
                          &pz7110_ccache_ops, s, "pz7110.ccache", 0x40000);
    memory_region_add_subregion(system_memory, 0x02010000, &s->ccache_mmio);

    dinfo = drive_get(IF_MTD, 0, 0);
    if (dinfo) {
        BlockBackend *blk = blk_by_legacy_dinfo(dinfo);
        int64_t flash_size = blk_getlength(blk);

        if (blk_attach_dev(blk, DEVICE(&s->qspi)) < 0) {
            error_report("Could not attach PZ7110 QSPI flash image");
            exit(1);
        }
        if (flash_size < 0) {
            error_report("Could not determine PZ7110 QSPI flash image size");
            exit(1);
        }
        if (flash_size > memmap[PZ7110_QSPI_XIP].size) {
            flash_size = memmap[PZ7110_QSPI_XIP].size;
        }
        s->qspi.flash_size = flash_size;
        if (blk_pread(blk, 0, flash_size, s->qspi.flash_data, 0) < 0) {
            error_report("Could not read PZ7110 QSPI flash image");
            exit(1);
        }
    }

    for (int i = 0; i < ARRAY_SIZE(park_loop); i++) {
        park_loop[i] = cpu_to_le32(park_loop[i]);
    }

    rom_add_blob_fixed_as("mrom.s7-park", park_loop, sizeof(park_loop),
                          memmap[PZ7110_MROM].base + 0x100,
                          &address_space_memory);

    serial_mm_init(system_memory, memmap[PZ7110_UART0].base,
                   2, qdev_get_gpio_in(irqchip, UART0_IRQ), 24000000,
                   serial_hd(0), DEVICE_LITTLE_ENDIAN);

    object_initialize_child(OBJECT(machine), "sys-crg", &s->sys_crg,
                            TYPE_PZ7110_SYS_CRG);
    sysbus_realize(SYS_BUS_DEVICE(&s->sys_crg), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sys_crg), 0,
                    memmap[PZ7110_SYS_CRG_IDX].base);

    object_initialize_child(OBJECT(machine), "stg-crg", &s->stg_crg,
                            TYPE_PZ7110_STG_CRG);
    sysbus_realize(SYS_BUS_DEVICE(&s->stg_crg), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->stg_crg), 0,
                    memmap[PZ7110_STG_CRG_IDX].base);

    object_initialize_child(OBJECT(machine), "aon-crg", &s->aon_crg,
                            TYPE_PZ7110_AON_CRG);
    sysbus_realize(SYS_BUS_DEVICE(&s->aon_crg), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->aon_crg), 0,
                    memmap[PZ7110_AON_CRG_IDX].base);

    object_initialize_child(OBJECT(machine), "sys-syscon", &s->sys_syscon,
                            TYPE_PZ7110_SYS_SYSCON);
    sysbus_realize(SYS_BUS_DEVICE(&s->sys_syscon), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sys_syscon), 0,
                    memmap[PZ7110_SYS_SYSCON_IDX].base);

    object_initialize_child(OBJECT(machine), "stg-syscon", &s->stg_syscon,
                            TYPE_PZ7110_STG_SYSCON);
    sysbus_realize(SYS_BUS_DEVICE(&s->stg_syscon), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->stg_syscon), 0,
                    memmap[PZ7110_STG_SYSCON_IDX].base);

    object_initialize_child(OBJECT(machine), "aon-syscon", &s->aon_syscon,
                            TYPE_PZ7110_AON_SYSCON);
    sysbus_realize(SYS_BUS_DEVICE(&s->aon_syscon), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->aon_syscon), 0,
                    memmap[PZ7110_AON_SYSCON_IDX].base);

    object_initialize_child(OBJECT(machine), "sys-iomux", &s->sys_iomux,
                            TYPE_PZ7110_SYS_IOMUX);
    sysbus_realize(SYS_BUS_DEVICE(&s->sys_iomux), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sys_iomux), 0,
                    memmap[PZ7110_SYS_IOMUX_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sys_iomux), 0,
                       qdev_get_gpio_in(irqchip, SYS_GPIO_IRQ));

    object_initialize_child(OBJECT(machine), "aon-iomux", &s->aon_iomux,
                            TYPE_PZ7110_AON_IOMUX);
    sysbus_realize(SYS_BUS_DEVICE(&s->aon_iomux), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->aon_iomux), 0,
                    memmap[PZ7110_AON_IOMUX_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->aon_iomux), 0,
                       qdev_get_gpio_in(irqchip, AON_GPIO_IRQ));

    /*
     * Temporary passive windows for early SPL register touches.  These are not
     * complete device models; later subsystem commits replace them with
     * register-aware models.
    */
    pz7110_create_quiet_stub("pz7110.spi-boot", 0x11000000, 0x10000);
    pz7110_create_ddr_stub("pz7110.dmc", 0x15700000);
    pz7110_create_ddr_stub("pz7110.ddr-phy", 0x13000000);
    pz7110_create_quiet_stub("pz7110.otp", 0x17050000, 0x10000);
    pz7110_create_quiet_stub("pz7110.hdmi", 0x29590000, 0x4000);
    pz7110_create_quiet_stub("pz7110.dssctrl", 0x295b0000, 0x1000);
    pz7110_create_quiet_stub("pz7110.dc8200", 0x29400000, 0x10000);
    pz7110_create_quiet_stub("pz7110.mipi-dsi", 0x295d0000, 0x10000);
    pz7110_create_quiet_stub("pz7110.mipi-dphy", 0x295e0000, 0x10000);

    s->pmu_power_mode = 0x3;
    memory_region_init_io(&s->pmu_mmio, OBJECT(machine), &pz7110_pmu_ops, s,
                          "pz7110.pmu", 0x10000);
    memory_region_add_subregion(system_memory, 0x17030000, &s->pmu_mmio);

    object_initialize_child(OBJECT(machine), "vout-crg", &s->vout_crg,
                            TYPE_PZ7110_VOUT_CRG);
    sysbus_realize(SYS_BUS_DEVICE(&s->vout_crg), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->vout_crg), 0,
                    memmap[PZ7110_VOUT_CRG_IDX].base);

    object_initialize_child(OBJECT(machine), "timer", &s->timer,
                            TYPE_PZ7110_TIMER);
    sysbus_realize(SYS_BUS_DEVICE(&s->timer), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->timer), 0,
                    memmap[PZ7110_TIMER_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->timer), 0,
                       qdev_get_gpio_in(irqchip, TIMER0_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->timer), 1,
                       qdev_get_gpio_in(irqchip, TIMER1_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->timer), 2,
                       qdev_get_gpio_in(irqchip, TIMER2_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->timer), 3,
                       qdev_get_gpio_in(irqchip, TIMER3_IRQ));

    object_initialize_child(OBJECT(machine), "rtc", &s->rtc, TYPE_PZ7110_RTC);
    sysbus_realize(SYS_BUS_DEVICE(&s->rtc), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->rtc), 0,
                    memmap[PZ7110_RTC_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->rtc), 0,
                       qdev_get_gpio_in(irqchip, RTC_MS_PULSE_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->rtc), 1,
                       qdev_get_gpio_in(irqchip, RTC_SEC_PULSE_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->rtc), 2,
                       qdev_get_gpio_in(irqchip, RTC_IRQ));

    object_initialize_child(OBJECT(machine), "temp", &s->temp,
                            TYPE_PZ7110_SFCTEMP);
    sysbus_realize(SYS_BUS_DEVICE(&s->temp), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->temp), 0,
                    memmap[PZ7110_SFCTEMP_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->temp), 0,
                       qdev_get_gpio_in(irqchip, SFCTEMP_IRQ));

    object_initialize_child(OBJECT(machine), "trng", &s->trng,
                            TYPE_PZ7110_TRNG);
    sysbus_realize(SYS_BUS_DEVICE(&s->trng), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->trng), 0,
                    memmap[PZ7110_TRNG_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->trng), 0,
                       qdev_get_gpio_in(irqchip, TRNG_IRQ));

    object_initialize_child(OBJECT(machine), "gmac0", &s->gmac0,
                            TYPE_PZ7110_GMAC);
    s->gmac0.phy_addr = 0;
    qemu_configure_nic_device(DEVICE(&s->gmac0), true, NULL);
    sysbus_realize(SYS_BUS_DEVICE(&s->gmac0), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gmac0), 0,
                    memmap[PZ7110_GMAC0_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->gmac0), 0,
                       qdev_get_gpio_in(irqchip, GMAC0_IRQ));

    object_initialize_child(OBJECT(machine), "gmac1", &s->gmac1,
                            TYPE_PZ7110_GMAC);
    s->gmac1.phy_addr = 1;
    qemu_configure_nic_device(DEVICE(&s->gmac1), true, NULL);
    sysbus_realize(SYS_BUS_DEVICE(&s->gmac1), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gmac1), 0,
                    memmap[PZ7110_GMAC1_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->gmac1), 0,
                       qdev_get_gpio_in(irqchip, GMAC1_IRQ));

    pz7110_create_i2c(memmap[PZ7110_I2C0].base,
                      qdev_get_gpio_in(irqchip, I2C0_IRQ), false);
    pz7110_create_i2c(memmap[PZ7110_I2C1].base,
                      qdev_get_gpio_in(irqchip, I2C1_IRQ), false);
    pz7110_create_i2c(memmap[PZ7110_I2C2].base,
                      qdev_get_gpio_in(irqchip, I2C2_IRQ), false);
    pz7110_create_i2c(memmap[PZ7110_I2C3].base,
                      qdev_get_gpio_in(irqchip, I2C3_IRQ), false);
    pz7110_create_i2c(memmap[PZ7110_I2C4].base,
                      qdev_get_gpio_in(irqchip, I2C4_IRQ), false);
    pz7110_create_i2c(memmap[PZ7110_I2C5].base,
                      qdev_get_gpio_in(irqchip, I2C5_IRQ), true);
    pz7110_create_i2c(memmap[PZ7110_I2C6].base,
                      qdev_get_gpio_in(irqchip, I2C6_IRQ), false);

    object_initialize_child(OBJECT(machine), "sdio0", &s->sdio0,
                            TYPE_PZ7110_SDIO);
    dinfo = drive_get(IF_SD, 0, 1);
    s->sdio0.card_present = dinfo != NULL;
    s->sdio0.emmc_mode = true;
    sysbus_realize(SYS_BUS_DEVICE(&s->sdio0), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sdio0), 0,
                    memmap[PZ7110_SDIO0_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sdio0), 0,
                       qdev_get_gpio_in(irqchip, SDIO0_IRQ));
    if (dinfo) {
        DeviceState *card = qdev_new(TYPE_EMMC);

        qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
        qdev_realize_and_unref(card,
                               qdev_get_child_bus(DEVICE(&s->sdio0),
                                                  "sd-bus"),
                               &error_fatal);
    }

    object_initialize_child(OBJECT(machine), "sdio1", &s->sdio1,
                            TYPE_PZ7110_SDIO);
    dinfo = drive_get(IF_SD, 0, 0);
    s->sdio1.card_present = dinfo != NULL;
    s->sdio1.emmc_mode = false;
    sysbus_realize(SYS_BUS_DEVICE(&s->sdio1), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sdio1), 0,
                    memmap[PZ7110_SDIO1_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sdio1), 0,
                       qdev_get_gpio_in(irqchip, SDIO1_IRQ));
    if (dinfo) {
        DeviceState *card = qdev_new(TYPE_SD_CARD);

        qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
        qdev_realize_and_unref(card,
                               qdev_get_child_bus(DEVICE(&s->sdio1),
                                                  "sd-bus"),
                               &error_fatal);
    }

    firmware_name = riscv_default_firmware_name(&s->u_cpus);
    firmware_end_addr = riscv_find_and_load_firmware(machine, firmware_name,
                                                     &firmware_load_addr,
                                                     NULL);
    if (firmware_end_addr > firmware_load_addr) {
        spl_dtb_offset = pz7110_patch_spl_dtb(
            memory_region_get_ram_ptr(sram), memmap[PZ7110_SRAM].size);
    }

    if (spl_dtb_offset >= 0) {
        spl_fdt_load_addr = firmware_load_addr + spl_dtb_offset;
    }

    {
        uint32_t reset_vec[] = {
            0xf1402573,                  /* csrr   a0, mhartid */
            0x00100313,                  /* li     t1, 1 */
            0x00651c63,                  /* bne    a0, t1, park */
            0x00000297,                  /* auipc  t0, 0 */
            0x00000613,                  /* li     a2, 0 */
            0x0242b583,                  /* ld     a1, 36(t0) */
            0x01c2b283,                  /* ld     t0, 28(t0) */
            0x00028067,                  /* jr     t0 */
            0x10500073,                  /* park:  wfi */
            0xffdff06f,                  /* j      park */
            firmware_load_addr,
            firmware_load_addr >> 32,
            spl_fdt_load_addr,
            spl_fdt_load_addr >> 32,
        };

        for (int i = 0; i < ARRAY_SIZE(reset_vec); i++) {
            reset_vec[i] = cpu_to_le32(reset_vec[i]);
        }

        rom_add_blob_fixed_as("mrom.reset", reset_vec, sizeof(reset_vec),
                              memmap[PZ7110_MROM].base,
                              &address_space_memory);
    }
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
    mc->block_default_type = IF_MTD;
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
