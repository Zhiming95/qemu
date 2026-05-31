/*
 * QEMU RISC-V PZ7110 SoC device
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "hw/boards.h"
#include "hw/char/serial-mm.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/sifive_plic.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/pz7110.h"
#include "hw/riscv/pz7110_soc.h"
#include "hw/riscv/sifive_cpu.h"
#include "hw/riscv/pz7110_ddr_stub.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/sd/sd.h"
#include "hw/ssi/pl022.h"
#include "hw/sysbus.h"
#include "net/net.h"
#include "system/system.h"
#include "target/riscv/cpu.h"

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

static uint64_t pz7110_dw_uart_ext_read(void *opaque, hwaddr addr,
                                        unsigned int size)
{
    return 0;
}

static void pz7110_dw_uart_ext_write(void *opaque, hwaddr addr,
                                     uint64_t value, unsigned int size)
{
}

static const MemoryRegionOps pz7110_dw_uart_ext_ops = {
    .read = pz7110_dw_uart_ext_read,
    .write = pz7110_dw_uart_ext_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void pz7110_create_quiet_stub(const char *name, hwaddr base,
                                     hwaddr size)
{
    MemoryRegion *mr = g_new0(MemoryRegion, 1);

    memory_region_init_io(mr, NULL, &pz7110_quiet_stub_ops, NULL, name, size);
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

static void pz7110_soc_instance_init(Object *obj)
{
    PZ7110SoCState *s = PZ7110_SOC(obj);

    /*
     * CPU topology: S7 monitor core (hart 0) + U74 application cores (harts 1-4).
     * Following the sifive_u.c pattern of separate clusters for heterogeneous harts.
     */
    object_initialize_child(obj, "s7-cluster", &s->s7_cluster,
                            TYPE_CPU_CLUSTER);
    object_property_set_uint(OBJECT(&s->s7_cluster), "cluster-id", 0,
                             &error_abort);

    object_initialize_child(obj, "u74-cluster", &s->u74_cluster,
                            TYPE_CPU_CLUSTER);
    object_property_set_uint(OBJECT(&s->u74_cluster), "cluster-id", 1,
                             &error_abort);

    object_initialize_child(OBJECT(&s->s7_cluster), "s7-cpus", &s->s7_cpus,
                            TYPE_RISCV_HART_ARRAY);
    qdev_prop_set_uint32(DEVICE(&s->s7_cpus), "num-harts", 1);
    qdev_prop_set_uint32(DEVICE(&s->s7_cpus), "hartid-base", 0);
    qdev_prop_set_string(DEVICE(&s->s7_cpus), "cpu-type", SIFIVE_E_CPU);
    object_initialize_child(OBJECT(&s->u74_cluster), "u74-cpus", &s->u74_cpus,
                            TYPE_RISCV_HART_ARRAY);
    qdev_prop_set_uint32(DEVICE(&s->u74_cpus), "num-harts",
                         PZ7110_HART_COUNT - 1);
    qdev_prop_set_uint32(DEVICE(&s->u74_cpus), "hartid-base", 1);

    object_initialize_child(obj, "qspi", &s->qspi, TYPE_CADENCE_QSPI);
    object_initialize_child(obj, "ccache", &s->ccache, TYPE_PZ7110_CCACHE);
    object_initialize_child(obj, "sys-crg", &s->sys_crg, TYPE_PZ7110_SYS_CRG);
    object_initialize_child(obj, "stg-crg", &s->stg_crg, TYPE_PZ7110_STG_CRG);
    object_initialize_child(obj, "aon-crg", &s->aon_crg, TYPE_PZ7110_AON_CRG);
    object_initialize_child(obj, "sys-syscon", &s->sys_syscon,
                            TYPE_PZ7110_SYS_SYSCON);
    object_initialize_child(obj, "stg-syscon", &s->stg_syscon,
                            TYPE_PZ7110_STG_SYSCON);
    object_initialize_child(obj, "aon-syscon", &s->aon_syscon,
                            TYPE_PZ7110_AON_SYSCON);
    object_initialize_child(obj, "sys-iomux", &s->sys_iomux,
                            TYPE_PZ7110_SYS_IOMUX);
    object_initialize_child(obj, "aon-iomux", &s->aon_iomux,
                            TYPE_PZ7110_AON_IOMUX);
    object_initialize_child(obj, "sdio0", &s->sdio0, TYPE_PZ7110_SDIO);
    object_initialize_child(obj, "sdio1", &s->sdio1, TYPE_PZ7110_SDIO);
    object_initialize_child(obj, "temp", &s->temp, TYPE_PZ7110_SFCTEMP);
    object_initialize_child(obj, "timer", &s->timer, TYPE_PZ7110_TIMER);
    object_initialize_child(obj, "rtc", &s->rtc, TYPE_PZ7110_RTC);
    object_initialize_child(obj, "trng", &s->trng, TYPE_PZ7110_TRNG);
    object_initialize_child(obj, "crypto", &s->crypto, TYPE_PZ7110_CRYPTO);
    object_initialize_child(obj, "gmac0", &s->gmac0, TYPE_PZ7110_GMAC);
    object_initialize_child(obj, "gmac1", &s->gmac1, TYPE_PZ7110_GMAC);
    object_initialize_child(obj, "dma", &s->dma, TYPE_PZ7110_DMA);
    object_initialize_child(obj, "pwm", &s->pwm, TYPE_PZ7110_PWM);
    object_initialize_child(obj, "pz7110-usb", &s->usb, TYPE_PZ7110_USB);
    object_initialize_child(obj, "pcie0", &s->pcie0, TYPE_PZ7110_PCIE);
    object_initialize_child(obj, "pcie1", &s->pcie1, TYPE_PZ7110_PCIE);
    object_initialize_child(obj, "pmu", &s->pmu, TYPE_PZ7110_PMU);
    object_initialize_child(obj, "vout-crg", &s->vout_crg,
                            TYPE_PZ7110_VOUT_CRG);
    object_initialize_child(obj, "wdt", &s->wdt, TYPE_PZ7110_WDT);
}

static void pz7110_soc_realize(DeviceState *dev, Error **errp)
{
    PZ7110SoCState *soc = PZ7110_SOC(dev);
    const MemMapEntry *memmap = soc->memmap;
    MemoryRegion *system_memory = get_system_memory();
    DeviceState *irqchip;
    DriveInfo *dinfo;

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

    /*
     * Both S7 and U74 harts share the same reset vector at MROM base.
     * This follows the sifive_u pattern: E51 and U54 both start from
     * the same resetvec address.  SPL hart_lottery handles multi-hart
     * coordination; QEMU does not select a boot hart.
     */
    qdev_prop_set_uint64(DEVICE(&soc->s7_cpus), "resetvec",
                         memmap[PZ7110_MROM].base);
    sysbus_realize(SYS_BUS_DEVICE(&soc->s7_cpus), &error_fatal);

    qdev_prop_set_string(DEVICE(&soc->u74_cpus), "cpu-type",
                         MACHINE(qdev_get_machine())->cpu_type);
    qdev_prop_set_uint64(DEVICE(&soc->u74_cpus), "resetvec",
                         memmap[PZ7110_MROM].base);
    sysbus_realize(SYS_BUS_DEVICE(&soc->u74_cpus), &error_fatal);

    /*
     * The cluster must be realized after the RISC-V hart array container,
     * as the container's CPU object is only created on realize, and the
     * CPU must exist and have been parented into the cluster before the
     * cluster is realized.
     */
    qdev_realize(DEVICE(&soc->s7_cluster), NULL, &error_abort);
    qdev_realize(DEVICE(&soc->u74_cluster), NULL, &error_abort);

    /* boot rom */
    {
        MemoryRegion *mask_rom = g_new(MemoryRegion, 1);

        memory_region_init_rom(mask_rom, OBJECT(dev), "pz7110.mrom",
                               memmap[PZ7110_MROM].size, &error_fatal);
        memory_region_add_subregion(system_memory,
                                    memmap[PZ7110_MROM].base, mask_rom);
    }

    /* SRAM: SPL loads here */
    {
        MemoryRegion *sram = g_new(MemoryRegion, 1);

        memory_region_init_ram(sram, OBJECT(dev), "pz7110.sram",
                               memmap[PZ7110_SRAM].size, &error_fatal);
        memory_region_add_subregion(system_memory,
                                    memmap[PZ7110_SRAM].base, sram);
        soc->sram_mr = sram;
    }

    /* ACLINT: software interrupts and machine timer for all 5 harts */
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

    /* PLIC: hart0 M-only, harts 1-4 M+S (9 contexts total) */
    irqchip = pz7110_create_plic(memmap, 0, PZ7110_HART_COUNT);

    /* QSPI controller */
    sysbus_realize(SYS_BUS_DEVICE(&soc->qspi), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->qspi), 0,
                    memmap[PZ7110_QSPI0].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->qspi), 0,
                       qdev_get_gpio_in(irqchip, QSPI0_IRQ));

    /* CCache */
    sysbus_realize(SYS_BUS_DEVICE(&soc->ccache), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->ccache), 0,
                    memmap[PZ7110_CCACHE_IDX].base);

    /* UART0 */
    serial_mm_init(get_system_memory(), memmap[PZ7110_UART0].base,
                   2, qdev_get_gpio_in(irqchip, UART0_IRQ), 24000000,
                   serial_hd(0), DEVICE_LITTLE_ENDIAN);

    {
        static MemoryRegion dw_uart0_ext;

        memory_region_init_io(&dw_uart0_ext, OBJECT(dev),
                              &pz7110_dw_uart_ext_ops, NULL,
                              "pz7110.dw-uart0-ext", 0x40);
        memory_region_add_subregion(get_system_memory(),
                                    memmap[PZ7110_UART0].base + 0xc0,
                                    &dw_uart0_ext);
    }

    /* CRG (Clock Reset Generator) */
    sysbus_realize(SYS_BUS_DEVICE(&soc->sys_crg), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->sys_crg), 0,
                    memmap[PZ7110_SYS_CRG_IDX].base);

    sysbus_realize(SYS_BUS_DEVICE(&soc->stg_crg), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->stg_crg), 0,
                    memmap[PZ7110_STG_CRG_IDX].base);

    sysbus_realize(SYS_BUS_DEVICE(&soc->aon_crg), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->aon_crg), 0,
                    memmap[PZ7110_AON_CRG_IDX].base);

    /* SYSCON */
    sysbus_realize(SYS_BUS_DEVICE(&soc->sys_syscon), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->sys_syscon), 0,
                    memmap[PZ7110_SYS_SYSCON_IDX].base);

    sysbus_realize(SYS_BUS_DEVICE(&soc->stg_syscon), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->stg_syscon), 0,
                    memmap[PZ7110_STG_SYSCON_IDX].base);

    sysbus_realize(SYS_BUS_DEVICE(&soc->aon_syscon), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->aon_syscon), 0,
                    memmap[PZ7110_AON_SYSCON_IDX].base);

    /* IOMUX */
    sysbus_realize(SYS_BUS_DEVICE(&soc->sys_iomux), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->sys_iomux), 0,
                    memmap[PZ7110_SYS_IOMUX_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->sys_iomux), 0,
                       qdev_get_gpio_in(irqchip, SYS_GPIO_IRQ));

    sysbus_realize(SYS_BUS_DEVICE(&soc->aon_iomux), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->aon_iomux), 0,
                    memmap[PZ7110_AON_IOMUX_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->aon_iomux), 0,
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

    /* PMU */
    sysbus_realize(SYS_BUS_DEVICE(&soc->pmu), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->pmu), 0,
                    memmap[PZ7110_PMU_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->pmu), 0,
                       qdev_get_gpio_in(irqchip, PMU_IRQ));

    /* VOUT CRG */
    sysbus_realize(SYS_BUS_DEVICE(&soc->vout_crg), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->vout_crg), 0,
                    memmap[PZ7110_VOUT_CRG_IDX].base);

    /* Timer */
    sysbus_realize(SYS_BUS_DEVICE(&soc->timer), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->timer), 0,
                    memmap[PZ7110_TIMER_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->timer), 0,
                       qdev_get_gpio_in(irqchip, TIMER0_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->timer), 1,
                       qdev_get_gpio_in(irqchip, TIMER1_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->timer), 2,
                       qdev_get_gpio_in(irqchip, TIMER2_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->timer), 3,
                       qdev_get_gpio_in(irqchip, TIMER3_IRQ));

    /* WDT */
    sysbus_realize(SYS_BUS_DEVICE(&soc->wdt), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->wdt), 0,
                    memmap[PZ7110_WDT_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->wdt), 0,
                       qdev_get_gpio_in(irqchip, WDT_IRQ));

    /* RTC */
    sysbus_realize(SYS_BUS_DEVICE(&soc->rtc), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->rtc), 0,
                    memmap[PZ7110_RTC_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->rtc), 0,
                       qdev_get_gpio_in(irqchip, RTC_MS_PULSE_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->rtc), 1,
                       qdev_get_gpio_in(irqchip, RTC_SEC_PULSE_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->rtc), 2,
                       qdev_get_gpio_in(irqchip, RTC_IRQ));

    /* Temperature sensor */
    sysbus_realize(SYS_BUS_DEVICE(&soc->temp), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->temp), 0,
                    memmap[PZ7110_SFCTEMP_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->temp), 0,
                       qdev_get_gpio_in(irqchip, SFCTEMP_IRQ));

    /* TRNG */
    sysbus_realize(SYS_BUS_DEVICE(&soc->trng), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->trng), 0,
                    memmap[PZ7110_TRNG_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->trng), 0,
                       qdev_get_gpio_in(irqchip, TRNG_IRQ));

    /* Crypto */
    sysbus_realize(SYS_BUS_DEVICE(&soc->crypto), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->crypto), 0,
                    memmap[PZ7110_CRYPTO_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->crypto), 0,
                       qdev_get_gpio_in(irqchip, CRYPTO_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->crypto), 1,
                       qdev_get_gpio_in(irqchip, CRYPTO_DMA_IRQ));
    pz7110_create_quiet_stub("pz7110.sec-dma",
                             memmap[PZ7110_SEC_DMA_IDX].base,
                             memmap[PZ7110_SEC_DMA_IDX].size);

    /* GMAC0 */
    soc->gmac0.phy_addr = 0;
    qemu_configure_nic_device(DEVICE(&soc->gmac0), true, NULL);
    sysbus_realize(SYS_BUS_DEVICE(&soc->gmac0), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->gmac0), 0,
                    memmap[PZ7110_GMAC0_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->gmac0), 0,
                       qdev_get_gpio_in(irqchip, GMAC0_IRQ));

    /* GMAC1 */
    soc->gmac1.phy_addr = 1;
    qemu_configure_nic_device(DEVICE(&soc->gmac1), true, NULL);
    sysbus_realize(SYS_BUS_DEVICE(&soc->gmac1), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->gmac1), 0,
                    memmap[PZ7110_GMAC1_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->gmac1), 0,
                       qdev_get_gpio_in(irqchip, GMAC1_IRQ));

    /* DMA */
    sysbus_realize(SYS_BUS_DEVICE(&soc->dma), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->dma), 0,
                    memmap[PZ7110_DMA_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->dma), 0,
                       qdev_get_gpio_in(irqchip, DMA_IRQ));

    /* PWM */
    sysbus_realize(SYS_BUS_DEVICE(&soc->pwm), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->pwm), 0,
                    memmap[PZ7110_PWM_IDX].base);

    /* USB */
    sysbus_realize(SYS_BUS_DEVICE(&soc->usb), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->usb), 0,
                    memmap[PZ7110_USB_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->usb), 0,
                       qdev_get_gpio_in(irqchip, USB_IRQ));

    /* PCIe0 */
    sysbus_realize(SYS_BUS_DEVICE(&soc->pcie0), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->pcie0), 0,
                    memmap[PZ7110_PCIE0_APB_IDX].base);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->pcie0), 1,
                    memmap[PZ7110_PCIE0_CFG_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->pcie0), 0,
                       qdev_get_gpio_in(irqchip, PCIE0_IRQ));

    /* PCIe1 */
    sysbus_realize(SYS_BUS_DEVICE(&soc->pcie1), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->pcie1), 0,
                    memmap[PZ7110_PCIE1_APB_IDX].base);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->pcie1), 1,
                    memmap[PZ7110_PCIE1_CFG_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->pcie1), 0,
                       qdev_get_gpio_in(irqchip, PCIE1_IRQ));

    /* I2C controllers */
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

    /* SPI controllers */
    sysbus_create_simple(TYPE_PL022, memmap[PZ7110_SPI0].base,
                         qdev_get_gpio_in(irqchip, SPI0_IRQ));
    sysbus_create_simple(TYPE_PL022, memmap[PZ7110_SPI1].base,
                         qdev_get_gpio_in(irqchip, SPI1_IRQ));
    sysbus_create_simple(TYPE_PL022, memmap[PZ7110_SPI2].base,
                         qdev_get_gpio_in(irqchip, SPI2_IRQ));
    sysbus_create_simple(TYPE_PL022, memmap[PZ7110_SPI3].base,
                         qdev_get_gpio_in(irqchip, SPI3_IRQ));
    sysbus_create_simple(TYPE_PL022, memmap[PZ7110_SPI4].base,
                         qdev_get_gpio_in(irqchip, SPI4_IRQ));
    sysbus_create_simple(TYPE_PL022, memmap[PZ7110_SPI5].base,
                         qdev_get_gpio_in(irqchip, SPI5_IRQ));
    sysbus_create_simple(TYPE_PL022, memmap[PZ7110_SPI6].base,
                         qdev_get_gpio_in(irqchip, SPI6_IRQ));

    /* Audio/misc stubs */
    pz7110_create_quiet_stub("pz7110.tdm", 0x10090000, 0x1000);
    pz7110_create_quiet_stub("pz7110.i2stx", 0x100c0000, 0x1000);
    pz7110_create_quiet_stub("pz7110.pdm", 0x100d0000, 0x1000);
    pz7110_create_quiet_stub("pz7110.i2srx", 0x100e0000, 0x1000);
    pz7110_create_quiet_stub("pz7110.i2stx-4ch0", 0x120b0000, 0x1000);
    pz7110_create_quiet_stub("pz7110.i2stx-4ch1", 0x120c0000, 0x1000);
    pz7110_create_quiet_stub("pz7110.usb3-phy", 0x10200000, 0x1000);
    pz7110_create_quiet_stub("pz7110.phyctrl0", 0x10210000, 0x10000);
    pz7110_create_quiet_stub("pz7110.phyctrl1", 0x10220000, 0x10000);
    pz7110_create_quiet_stub("pz7110.mailbox",
                             memmap[PZ7110_MAILBOX_IDX].base,
                             memmap[PZ7110_MAILBOX_IDX].size);
    pz7110_create_quiet_stub("pz7110.can0", memmap[PZ7110_CAN0_IDX].base,
                             memmap[PZ7110_CAN0_IDX].size);
    pz7110_create_quiet_stub("pz7110.can1", memmap[PZ7110_CAN1_IDX].base,
                             memmap[PZ7110_CAN1_IDX].size);

    /* SDIO0 (eMMC) */
    soc->sdio0.emmc_mode = true;
    dinfo = drive_get(IF_SD, 0, 1);
    soc->sdio0.card_present = dinfo != NULL;
    sysbus_realize(SYS_BUS_DEVICE(&soc->sdio0), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->sdio0), 0,
                    memmap[PZ7110_SDIO0_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->sdio0), 0,
                       qdev_get_gpio_in(irqchip, SDIO0_IRQ));
    if (dinfo) {
        DeviceState *card = qdev_new(TYPE_EMMC);

        qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
        qdev_realize_and_unref(card,
                               qdev_get_child_bus(DEVICE(&soc->sdio0),
                                                  "sd-bus"),
                               &error_fatal);
    }

    /* SDIO1 (SD card) */
    soc->sdio1.emmc_mode = false;
    dinfo = drive_get(IF_SD, 0, 0);
    soc->sdio1.card_present = dinfo != NULL;
    sysbus_realize(SYS_BUS_DEVICE(&soc->sdio1), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&soc->sdio1), 0,
                    memmap[PZ7110_SDIO1_IDX].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->sdio1), 0,
                       qdev_get_gpio_in(irqchip, SDIO1_IRQ));
    if (dinfo) {
        DeviceState *card = qdev_new(TYPE_SD_CARD);

        qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
        qdev_realize_and_unref(card,
                               qdev_get_child_bus(DEVICE(&soc->sdio1),
                                                  "sd-bus"),
                               &error_fatal);
    }
}

static void pz7110_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = pz7110_soc_realize;
    dc->user_creatable = false;
}

static const TypeInfo pz7110_soc_typeinfo = {
    .name = TYPE_PZ7110_SOC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(PZ7110SoCState),
    .instance_init = pz7110_soc_instance_init,
    .class_init = pz7110_soc_class_init,
};

static void pz7110_soc_register_types(void)
{
    type_register_static(&pz7110_soc_typeinfo);
}

type_init(pz7110_soc_register_types)
