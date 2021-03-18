/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdlib.h>
#include <platsupport/io.h>
#include <platsupport/delay.h>

#define _malloc(...) malloc(__VA_ARGS__)
#define _free(...) free(__VA_ARGS__)

#ifdef CAMKES
#define RESOURCE(o, id) id##_VADDR
#else
#define RESOURCE(o, id) sdhc_map_device(&o->io_mapper, id##_PADDR, id##_SIZE)
#endif

static inline void udelay(long us)
{
    ps_udelay(us);
}

/**
 * Maps in device memory
 * @param[in] o     A reference to the services provided
 * @param[in] paddr the physical address of the device
 * @param[in] size  the size of the region in bytes
 * @return          the virtual address of the mapping.
 *                  NULL on failure.
 */
static inline void *sdhc_map_device(struct ps_io_mapper *o, uintptr_t paddr, int size)
{
    return ps_io_map(o, paddr, size, 0, PS_MEM_NORMAL);
}

static inline void *ps_dma_alloc_pinned(ps_dma_man_t *dma_man, size_t size, int align, int cache, ps_mem_flags_t flags,
                                        uintptr_t *paddr)
{
    void *addr;
    assert(dma_man);
    addr = ps_dma_alloc(dma_man, size, align, cache, flags);
    if (addr != NULL) {
        *paddr = ps_dma_pin(dma_man, addr, size);
    }
    return addr;
}

static inline void ps_dma_free_pinned(ps_dma_man_t *dma_man, void *addr, size_t size)
{
    assert(dma_man);
    ps_dma_unpin(dma_man, addr, size);
    ps_dma_free(dma_man, addr, size);
}


