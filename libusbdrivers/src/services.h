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
#pragma once

#include <stdlib.h>
#include <platsupport/io.h>
#include <platsupport/delay.h>
#include <utils/ansi.h>
#include <utils/util.h>
#include <utils/zf_log.h>
#include <usb/usb_host.h>

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
		if (!ret) {
			ZF_LOGF("Malloc error\n");
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
		if (!ret) {
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
	asm volatile ("" ::: "memory");
#endif
}

static inline void *ps_dma_alloc_pinned(ps_dma_man_t * dma_man, size_t size,
					int align, int cache,
					ps_mem_flags_t flags, uintptr_t * paddr)
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

static inline void
ps_dma_free_pinned(ps_dma_man_t * dma_man, void *addr, size_t size)
{
	if (!dma_man) {
		ZF_LOGF("Invalid arguments\n");
	}
	ps_dma_unpin(dma_man, addr, size);
	ps_dma_free(dma_man, addr, size);
}

