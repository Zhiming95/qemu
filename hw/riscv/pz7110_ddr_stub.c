/*
 * QEMU PZ7110 DDR training register stub
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "exec/address-spaces.h"
#include "exec/memory.h"
#include "hw/riscv/pz7110_ddr_stub.h"

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

void pz7110_create_ddr_stub(const char *name, hwaddr base)
{
    MemoryRegion *mr = g_new0(MemoryRegion, 1);
    PZ7110DdrStubState *s = g_new0(PZ7110DdrStubState, 1);

    memory_region_init_io(mr, NULL, &pz7110_ddr_stub_ops, s, name, 0x10000);
    memory_region_add_subregion(get_system_memory(), base, mr);
}
