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
#include <utils/ansi.h>
#include <utils/util.h>
#include <utils/zf_log.h>
#include <usb/usb_host.h>

void otg_irq(void);

#ifdef ARCH_ARM
#define udelay(ms)  ps_udelay(ms)
#else
static inline void udelay(uint32_t us)
{
	volatile uint32_t i;
	for (; us > 0; us--) {
		for (i = 0; i < 1000; i++) ;
	}
}
#endif
#define msdelay(ms) udelay((ms) * 1000)

#define usb_malloc(...) calloc(1, __VA_ARGS__)
#define usb_free(...) free(__VA_ARGS__)

#define MAP_DEVICE(o, p, s) ps_io_map(&o->io_mapper, p, s, 0, PS_MEM_NORMAL)

#define GET_RESOURCE(ops, id) MAP_DEVICE(ops, id##_PADDR, id##_SIZE)

#ifdef ARCH_ARM
#define dsb() asm volatile("dsb")
#define isb() asm volatile("isb")
#define dmb() asm volatile("dmb")
#else
#define dsb() asm volatile ("" ::: "memory")
#define isb() asm volatile ("" ::: "memory")
#define dmb() asm volatile ("mfence" ::: "memory")
#endif

static inline void *ps_dma_alloc_pinned(ps_dma_man_t * dma_man, size_t size,
					int align, int cache,
					ps_mem_flags_t flags, uintptr_t * paddr)
{
	void *addr;
	if (!dma_man) {
		ZF_LOGE("Invalid arguments\n");
		abort();
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
		ZF_LOGE("Invalid arguments\n");
		abort();
	}
	ps_dma_unpin(dma_man, addr, size);
	ps_dma_free(dma_man, addr, size);
}

/* Circular Buffer */
struct circ_buf {
	char *buf;
	int head;
	int tail;
	int size;
};

static inline int circ_buf_is_full(struct circ_buf *cb)
{
	return (cb->tail + 1) % cb->size == cb->head;
}

static inline int circ_buf_is_empty(struct circ_buf *cb)
{
	return cb->tail == cb->head;
}

static inline void circ_buf_put(struct circ_buf *cb, unsigned char c)
{
	cb->buf[cb->tail] = c;
	cb->tail = (cb->tail + 1) % cb->size;
}

static inline unsigned char circ_buf_get(struct circ_buf *cb)
{
	unsigned char c;

	c = cb->buf[cb->head];
	cb->head = (cb->head + 1) % cb->size;
	return c;
}
