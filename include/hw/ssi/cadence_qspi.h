/*
 * QEMU model of the Cadence QSPI Controller
 *
 * Copyright (c) 2024 StarSix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#ifndef HW_SSI_CADENCE_QSPI_H
#define HW_SSI_CADENCE_QSPI_H

#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qom/object.h"

#define TYPE_CADENCE_QSPI "cadence-qspi"
OBJECT_DECLARE_SIMPLE_TYPE(CadenceQSPIState, CADENCE_QSPI)

#define CQSPI_NUM_REGS  (0x100 / 4)

struct CadenceQSPIState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t regs[CQSPI_NUM_REGS];

    /* Flash image backing */
    uint8_t *flash_data;
    uint32_t flash_size;

    /* STIG command state */
    uint32_t stig_cmd;
    uint32_t stig_addr;
    uint8_t  stig_read_data[8];
    uint8_t  status_reg;
    uint8_t  config_reg;

    /* Indirect read state */
    uint32_t indirect_addr;
    uint32_t indirect_bytes;
    uint32_t indirect_offset;
    uint32_t indirect_ahb_offset;
};

#endif /* HW_SSI_CADENCE_QSPI_H */
