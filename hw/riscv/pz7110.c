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
#include "hw/loader.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/pz7110.h"
#include "hw/riscv/pz7110_soc.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/sysbus.h"
#include "system/system.h"
#include "target/riscv/cpu.h"
#include <libfdt.h>

static const MemMapEntry pz7110_memmap[] = {
    [PZ7110_MROM] = { 0x2a000000, 0x10000 },
    [PZ7110_SRAM] = { 0x08000000, 0x200000 },
    [PZ7110_CLINT] = { 0x02000000, 0x10000 },
    [PZ7110_CCACHE_IDX] = { 0x02010000, 0x40000 },
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
    [PZ7110_SPI0] = { 0x10060000, 0x10000 },
    [PZ7110_SPI1] = { 0x10070000, 0x10000 },
    [PZ7110_SPI2] = { 0x10080000, 0x10000 },
    [PZ7110_SPI3] = { 0x12070000, 0x10000 },
    [PZ7110_SPI4] = { 0x12080000, 0x10000 },
    [PZ7110_SPI5] = { 0x12090000, 0x10000 },
    [PZ7110_SPI6] = { 0x120a0000, 0x10000 },
    [PZ7110_SDIO0_IDX] = { 0x16010000, 0x10000 },
    [PZ7110_SDIO1_IDX] = { 0x16020000, 0x10000 },
    [PZ7110_SFCTEMP_IDX] = { 0x120e0000, 0x10000 },
    [PZ7110_TIMER_IDX] = { 0x13050000, 0x10000 },
    [PZ7110_RTC_IDX] = { 0x17040000, 0x10000 },
    [PZ7110_TRNG_IDX] = { 0x1600c000, 0x4000 },
    [PZ7110_CRYPTO_IDX] = { 0x16000000, 0x4000 },
    [PZ7110_SEC_DMA_IDX] = { 0x16008000, 0x4000 },
    [PZ7110_GMAC0_IDX] = { 0x16030000, 0x10000 },
    [PZ7110_GMAC1_IDX] = { 0x16040000, 0x10000 },
    [PZ7110_DMA_IDX] = { 0x16050000, 0x10000 },
    [PZ7110_PWM_IDX] = { 0x120d0000, 0x10000 },
    [PZ7110_USB_IDX] = { 0x10100000, 0x100000 },
    [PZ7110_PCIE0_APB_IDX] = { 0x2b000000, 0x100000 },
    [PZ7110_PCIE0_CFG_IDX] = { 0x940000000, 0x1000000 },
    [PZ7110_PCIE1_APB_IDX] = { 0x2c000000, 0x100000 },
    [PZ7110_PCIE1_CFG_IDX] = { 0x9c0000000, 0x1000000 },
    [PZ7110_MAILBOX_IDX] = { 0x13060000, 0x1000 },
    [PZ7110_CAN0_IDX] = { 0x130d0000, 0x1000 },
    [PZ7110_CAN1_IDX] = { 0x130e0000, 0x1000 },
    [PZ7110_PMU_IDX] = { 0x17030000, 0x10000 },
    [PZ7110_VOUT_CRG_IDX] = { 0x295c0000, 0x10000 },
    [PZ7110_WDT_IDX] = { 0x13070000, 0x10000 },
    [PZ7110_DRAM] = { 0x40000000, 0x0 },
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
    MemoryRegion *xip = g_new(MemoryRegion, 1);
    DriveInfo *dinfo;
    const char *firmware_name;
    hwaddr firmware_load_addr = memmap[PZ7110_SRAM].base;
    target_ulong firmware_end_addr;
    ssize_t spl_dtb_offset = -1;
    uint64_t spl_fdt_load_addr = 0;

    /* Create and realize SoC device */
    object_initialize_child(OBJECT(machine), "soc", &s->soc, TYPE_PZ7110_SOC);
    s->soc.memmap = memmap;
    qdev_realize(DEVICE(&s->soc), NULL, &error_fatal);

    /* DRAM */
    memory_region_add_subregion(system_memory, memmap[PZ7110_DRAM].base,
                                machine->ram);

    /* QSPI XIP flash window */
    s->soc.qspi.flash_size = memmap[PZ7110_QSPI_XIP].size;
    s->soc.qspi.flash_data = g_malloc(s->soc.qspi.flash_size);
    memset(s->soc.qspi.flash_data, 0xff, s->soc.qspi.flash_size);
    memory_region_init_io(xip, NULL, &pz7110_qspi_xip_ops, &s->soc.qspi,
                          "pz7110.qspi_xip",
                          memmap[PZ7110_QSPI_XIP].size);
    memory_region_add_subregion(system_memory, memmap[PZ7110_QSPI_XIP].base,
                                xip);

    /* Load QSPI flash image if provided */
    dinfo = drive_get(IF_MTD, 0, 0);
    if (dinfo) {
        BlockBackend *blk = blk_by_legacy_dinfo(dinfo);
        int64_t flash_size = blk_getlength(blk);

        if (blk_attach_dev(blk, DEVICE(&s->soc.qspi)) < 0) {
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
        s->soc.qspi.flash_size = flash_size;
        if (blk_pread(blk, 0, flash_size, s->soc.qspi.flash_data, 0) < 0) {
            error_report("Could not read PZ7110 QSPI flash image");
            exit(1);
        }
    }

    /* Load SPL firmware into SRAM */
    firmware_name = riscv_default_firmware_name(&s->soc.u74_cpus);
    firmware_end_addr = riscv_find_and_load_firmware(machine, firmware_name,
                                                     &firmware_load_addr,
                                                     NULL);
    if (firmware_end_addr > firmware_load_addr) {
        spl_dtb_offset = pz7110_patch_spl_dtb(
            memory_region_get_ram_ptr(s->soc.sram_mr),
            memmap[PZ7110_SRAM].size);
    }

    if (spl_dtb_offset >= 0) {
        spl_fdt_load_addr = firmware_load_addr + spl_dtb_offset;
    }

    /*
     * Set up the standard reset vector in MROM.  All harts (0-4) start from
     * the same address, enter SPL with a0=mhartid, a1=fdt_addr.
     * SPL hart_lottery determines which hart boots; QEMU does not interfere.
     */
    riscv_setup_rom_reset_vec(machine, &s->soc.u74_cpus, firmware_load_addr,
                              memmap[PZ7110_MROM].base,
                              memmap[PZ7110_MROM].size, 0,
                              spl_fdt_load_addr);
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
