/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

#include <platsupport/delay.h>
#include <platsupport/io.h>
#include <stdlib.h>
#include <usb/usb_host.h>
#include <utils/ansi.h>
#include <utils/util.h>
#include <utils/zf_log.h>

void otg_irq(void);

#define MAP_DEVICE(o, p, s) ps_io_map(&o->io_mapper, p, s, 0, PS_MEM_NORMAL)
#define GET_RESOURCE(ops, id) MAP_DEVICE(ops, id##_PADDR, id##_SIZE)

extern ps_malloc_ops_t *ps_malloc_ops;
static inline void *usb_malloc(size_t size)
{
    int ret;

    if (ps_malloc_ops && ps_malloc_ops->calloc) {
        void *ptr;
        ret = ps_calloc(ps_malloc_ops, 1, size, &ptr);
        if (0 != ret) {
            ZF_LOGF("Malloc error %d\n", ret);
        }
        return ptr;
    } else {
        return calloc(1, size);
    }
}

static inline void usb_free(void *ptr)
{
    int ret;

    if (ps_malloc_ops && ps_malloc_ops->free) {
        ret = ps_free(ps_malloc_ops, 1, ptr);
        if (ret != 0) {
            ZF_LOGF("Free error\n");
        }
    } else {
        free(ptr);
    }
}

static inline void dsb()
{
#ifdef ARCH_ARM
    asm volatile("dsb");
#else
    asm volatile("" ::
                 : "memory");
#endif
}

static inline void *ps_dma_alloc_pinned(ps_dma_man_t *dma_man, size_t size,
                                        int align, int cache,
                                        ps_mem_flags_t flags, uintptr_t *paddr)
{
    void *addr;
    if (!dma_man) {
        ZF_LOGF("Invalid arguments\n");
    }
    addr = ps_dma_alloc(dma_man, size, align, cache, flags);
    if (addr != NULL) {
        *paddr = ps_dma_pin(dma_man, addr, size);
    }
    return addr;
}

static inline void ps_dma_free_pinned(ps_dma_man_t *dma_man, void *addr, size_t size)
{
    if (!dma_man) {
        ZF_LOGF("Invalid arguments\n");
    }
    ps_dma_unpin(dma_man, addr, size);
    ps_dma_free(dma_man, addr, size);
}
