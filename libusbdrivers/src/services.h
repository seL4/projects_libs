/*
 * Copyright 2017, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(DATA61_BSD)
 */

#include <stdlib.h>
#include <platsupport/io.h>
#include <platsupport/delay.h>
#include "debug.h"
#include <assert.h>
#include <utils/util.h>

void otg_irq(void);


#define udelay(ms)  ps_udelay(ms)
#define msdelay(ms) ps_mdelay(ms)


#define usb_assert(test)         \
        do{                      \
            assert(test);        \
        } while(0)


#define usb_malloc(...) malloc(__VA_ARGS__)
#define usb_free(...) free(__VA_ARGS__)

#define MAP_DEVICE(o, p, s) ps_io_map(&o->io_mapper, p, s, 0, PS_MEM_NORMAL)

#define GET_RESOURCE(ops, id) MAP_DEVICE(ops, id##_PADDR, id##_SIZE)



#define dsb() asm volatile("dsb")
#define isb() asm volatile("isb")
#define dmb() asm volatile("dmb")

static inline void*
ps_dma_alloc_pinned(ps_dma_man_t *dma_man, size_t size, int align, int cache,
	       ps_mem_flags_t flags, uintptr_t* paddr)
{
    void* addr;
    assert(dma_man);
    addr = ps_dma_alloc(dma_man, size, align, cache, flags);
    if (addr != NULL) {
        *paddr = ps_dma_pin(dma_man, addr, size);
    }
    return addr;
}

static inline void
ps_dma_free_pinned(ps_dma_man_t *dma_man, void* addr, size_t size)
{
    assert(dma_man);
    ps_dma_unpin(dma_man, addr, size);
    ps_dma_free(dma_man, addr, size);
}


