/*
 * Copyright 2019, Data61
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

#include <stdint.h>

#define RING_SIZE       128
#define DESC_TABLE_SIZE 128

#define VQ_DEV_POLL(vq) ((((vq)->a_ring_last_seen + 1) & (RING_SIZE - 1)) != (vq)->avail_ring->idx)
#define VQ_DRV_POLL(vq) ((((vq)->u_ring_last_seen + 1) & (RING_SIZE - 1)) != (vq)->used_ring->idx)

/* Flags for the buffers in the descriptor table */
typedef enum vq_flags {
    VQ_READ = 0,
    VQ_WRITE,
    VQ_RW
} vq_flags_t;

/* Ring of available buffers */
typedef struct vq_vring_avail {
    uint16_t flags;             /* Interrupt suppression flag */
    uint16_t idx;               /* Index of the next free entry in the ring */
    uint16_t ring[RING_SIZE];   /* The ring of descriptor table entries */
} vq_vring_avail_t;

/* Element of a used ring buffer */
typedef struct vq_vring_used_elem {
    uint32_t id;        /* Index in descriptor table */
    uint32_t len;       /* Length of data that was written by the device */
} vq_vring_used_elem_t;

/* Ring of used buffers */
typedef struct vq_vring_used {
    uint16_t flags;                             /* Interrupt suppression flag */
    uint16_t idx;                               /* Index of the next free entry in the ring */
    struct vq_vring_used_elem ring[RING_SIZE];  /* The ring of descriptor table entries */
} vq_vring_used_t;

/* Entry in the descriptor table */
typedef struct vq_vring_desc {
    uint64_t addr;      /* Address of the buffer in the shared memory */
    uint32_t len;       /* Length of the buffer */
    uint16_t flags;     /* Flag of the buffer */
    uint16_t next;      /* Index of the next descriptor table entry in the scatter list */
} vq_vring_desc_t;

/* Handle for iterating through a scatter list */
typedef struct virtqueue_ring_object {
    uint32_t cur;       /* The current index in desc table */
    uint32_t first;     /* The head of the scatter list in desc table */
} virtqueue_ring_object_t;

/* A device-side virtqueue */
typedef struct virtqueue_device {
    void (*notify)(void);       /* Notify function to wake-up driver side */
    void *cookie;               /* User-defined cookie */

    unsigned a_ring_last_seen;  /* Index of the last seen element in the available ring */

    volatile struct vq_vring_avail *avail_ring; /* The available ring */
    volatile struct vq_vring_used *used_ring;   /* The used ring */
    volatile struct vq_vring_desc *desc_table;  /* The descriptor table */
} virtqueue_device_t;

/* A driver-side virtqueue */
typedef struct virtqueue_driver {
    void (*notify)(void);       /* Notify function to wake-up device side */
    void *cookie;               /* User-defined cookie */

    unsigned free_desc_head;    /* The head of the free list in the descriptor table */
    unsigned u_ring_last_seen;  /* Index of the last seen element in the used ring */

    volatile struct vq_vring_avail *avail_ring; /* The available ring */
    volatile struct vq_vring_used *used_ring;   /* The used ring */
    volatile struct vq_vring_desc *desc_table;  /* The descritor table */
} virtqueue_driver_t;

/* Initialise a driver-side virtqueue.
 * @param vq the driver virtqueue
 * @param avail_ring pointer to the shared available ring
 * @param used_ring pointer to the shared used ring
 * @param desc pointer to the shared descriptor table
 * @param notify the notify function to wake up device side
 * @param cookie user's cookie
 */
void virtqueue_init_driver(virtqueue_driver_t *vq, volatile vq_vring_avail_t *avail_ring,
                           volatile vq_vring_used_t *used_ring, volatile vq_vring_desc_t *desc, void (*notify)(void),
                           void *cookie);

/* Initialise a device-side virtqueue.
 * @param vq the device virtqueue
 * @param avail_ring pointer to the shared available ring
 * @param used_ring pointer to the shared used ring
 * @param desc pointer to the shared descriptor table
 * @param notify the notify function to wake up driver side
 * @param cookie user's cookie
 */
void virtqueue_init_device(virtqueue_device_t *vq, volatile vq_vring_avail_t *avail_ring,
                           volatile vq_vring_used_t *used_ring, volatile vq_vring_desc_t *desc, void (*notify)(void),
                           void *cookie);

/* Initialise the descriptor table (create the free list) */
void virtqueue_init_desc_table(volatile vq_vring_desc_t *table);

/* Initialise the available ring */
void virtqueue_init_avail_ring(volatile vq_vring_avail_t *ring);

/* Initialise the used ring */
void virtqueue_init_used_ring(volatile vq_vring_used_t *ring);

/** Driver side **/

/* Add initial buffer to available ring
 * @param vq the driver virtqueue
 * @param obj the handle to the ring object. If the handle was just initialized, it will
 *            create a new entry in the ring. Any following calls with the same handle
 *            will then chain the buffer in the descriptor table but under the same ring entry.
 * @param buf the buffer to add
 * @param len the length of the buffer
 * @param flag the flag of the buffer
 * @return 1 on success, 0 on failure (ring full)
 */
int virtqueue_add_available_buf(virtqueue_driver_t *vq, virtqueue_ring_object_t *obj,
                                void *buf, unsigned len, vq_flags_t flag);

/* Get buffer from used ring. Dequeue a buffer from the used ring and get an iterator to the scatterlist
 * @param vq the driver side virtqueue
 * @param robj a pointer to the iterator that will be returned
 * @param len a pointer to the length of the buffer that was actually used by the device
 * @return 1 on success, 0 on failure (ring empty)
 */
int virtqueue_get_used_buf(virtqueue_driver_t *vq, virtqueue_ring_object_t *robj, uint32_t *len);

/** Device side **/

/* Add buffer to used ring. Takes an ring object (obtained from a get_available_buf call) and passes it
 * to the used ring.
 * @param vq the device side virtqueue
 * @param robj a pointer to the ring object
 * @param len the length of the buffer that the device actually used
 * @return 1 on success, 0 on failure (ring full)
 */
int virtqueue_add_used_buf(virtqueue_device_t *vq, virtqueue_ring_object_t *robj, uint32_t len);

/* Get buffer from available ring. Returns an iterator to the next available buffer list
 * @param vq the device side virtqueue
 * @param robj a pointer to the iterator that will be returned
 * @return 1 on success, 0 on failure (ring empty)
 */
int virtqueue_get_available_buf(virtqueue_device_t *vq, virtqueue_ring_object_t *robj);

/** Iteration functions **/

/* Initialise a ring object */
void virtqueue_init_ring_object(virtqueue_ring_object_t *obj);

/* Get the size of a scattered list in the available ring from its handle
 * @param vq the device side virtqueue
 * @param robj a pointer to the ring object/handle
 * @return the length of the scatterlist
 */
uint32_t virtqueue_scattered_available_size(virtqueue_device_t *vq, virtqueue_ring_object_t *robj);

/* Iteration function through an available buffer scatterlist. Returns the next buffer in the list.
 * @param vq the device side virtqueue
 * @param robj the handle/iterator upon which to iterate
 * @param buf a pointer to the address of the returned buffer
 * @param len a pointer to the length of the returned buffer
 * @param flag a pointer to the flag of the returned buffer
 * @return 1 on success, 0 on failure (no more buffer available)
 */
int virtqueue_gather_available(virtqueue_device_t *vq, virtqueue_ring_object_t *robj,
                               void **buf, unsigned *len, vq_flags_t *flag);

/* Iteration function through a used buffer scatterlist. Returns the next buffer in the list.
 * @param vq the driver side virtqueue
 * @param robj the handle/iterator upon which to iterate
 * @param buf a pointer to the address of the returned buffer
 * @param len a pointer to the length of the returned buffer
 * @param flag a pointer to the flag of the returned buffer
 * @return 1 on success, 0 on failure (no more buffer available)
 */
int virtqueue_gather_used(virtqueue_driver_t *vq, virtqueue_ring_object_t *robj,
                          void **buf, unsigned *len, vq_flags_t *flag);
