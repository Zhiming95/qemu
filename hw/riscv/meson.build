riscv_ss = ss.source_set()
riscv_ss.add(files('boot.c'))
riscv_ss.add(when: 'CONFIG_RISCV_NUMA', if_true: files('numa.c'))
riscv_ss.add(files('riscv_hart.c'))
riscv_ss.add(when: 'CONFIG_OPENTITAN', if_true: files('opentitan.c'))
riscv_ss.add(when: 'CONFIG_PZ7110', if_true: files(
    'pz7110.c',
    'pz7110_soc.c',
    'pz7110_ccache.c',
    'pz7110_crypto.c',
    'pz7110_crg.c',
    'pz7110_ddr_stub.c',
    'pz7110_dma.c',
    'pz7110_gmac.c',
    'pz7110_i2c.c',
    'pz7110_pcie.c',
    'pz7110_pmu.c',
    'pz7110_syscon.c',
    'pz7110_iomux.c',
    'pz7110_pwm.c',
    'pz7110_rtc.c',
    'pz7110_sdio.c',
    'pz7110_sfctemp.c',
    'pz7110_timer.c',
    'pz7110_trng.c',
    'pz7110_usb.c',
    'pz7110_vout_crg.c',
    'pz7110_wdt.c',
))
riscv_ss.add(when: 'CONFIG_RISCV_VIRT', if_true: files('virt.c'))
riscv_ss.add(when: 'CONFIG_SHAKTI_C', if_true: files('shakti_c.c'))
riscv_ss.add(when: 'CONFIG_SIFIVE_E', if_true: files('sifive_e.c'))
riscv_ss.add(when: 'CONFIG_SIFIVE_U', if_true: files('sifive_u.c'))
riscv_ss.add(when: 'CONFIG_SPIKE', if_true: files('spike.c'))
riscv_ss.add(when: 'CONFIG_MICROCHIP_PFSOC', if_true: files('microchip_pfsoc.c'))
riscv_ss.add(when: 'CONFIG_ACPI', if_true: files('virt-acpi-build.c'))
riscv_ss.add(when: 'CONFIG_RISCV_IOMMU', if_true: files(
	'riscv-iommu.c', 'riscv-iommu-pci.c', 'riscv-iommu-sys.c', 'riscv-iommu-hpm.c'))
riscv_ss.add(when: 'CONFIG_MICROBLAZE_V', if_true: files('microblaze-v-generic.c'))

hw_arch += {'riscv': riscv_ss}
