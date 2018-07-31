/*
 * Copyright 2018, Data61
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

#include <stddef.h>
#include <inttypes.h>
#include <platsupport/io.h>

/* The role of the client using a particular virtqueue */
typedef enum virtqueue_role {
    VIRTQUEUE_UNASSIGNED,
    VIRTQUEUE_DRIVER,
    VIRTQUEUE_DEVICE
} virtqueue_role_t;

/* The shared between a virtqueue driver and device */
typedef struct {
    unsigned int available_flag;
    unsigned int used_flag;
    size_t buffer_size;
} virtqueue_header_t;

/* TODO - For later development: Rings & Descriptor tables
 * typedef struct buffring {
 *      desc_table_t desc_table;
 *      avail_ring_t avail_ring;
 *      used_ring_t used_ring;
 * }
 */

typedef void (*notify_fn_t)(void);

typedef struct {
    virtqueue_header_t *header;
    volatile void *buffer;
    notify_fn_t notify;
    virtqueue_role_t role;
    void *cookie;
} virtqueue_t;

/** Initialises a new virtqueue handle
 *
 * @param vq Pointer to a handle (pointer) to will be initialised with a
 *             virtqueue instance .
 * @param notify Pointer to the notify/signal function for the given
 *             virtqueue
 * @param role The role of the client initialising the virtqueue
 * @param shared_header_data The shared window of memory used for meta
 *             header information for the virtqueue
 * @param cookie Memory that the initialiser wishes to cache for storing
 *             personal state
 * @return Success code. 0 for success, -1 for failure
 */
int virtqueue_init(virtqueue_t **vq, notify_fn_t notify,
        virtqueue_role_t role,
        virtqueue_header_t *shared_header_data, void *cookie,
        ps_malloc_ops_t *malloc_ops);

/** Frees the virtqueue. Released any managed memory for the
 * virtqueue handle
 */
void virtqueue_free(virtqueue_t *vq);

/** Enqueues an available buffer into the virtqueue_buff_t. Called by
 *  the driver role.
 *
 * @param virtqueue A handle to a virtqueue.
 * @param buff A pointer to a buffer of data from the caller which would
 *               like to enqueue into the available ring of buffers.
 * @return Success code. 0 for success, -1 for failure
 */
int virtqueue_enqueue_available_buff(virtqueue_t *virtqueue,
        void *buffer, size_t buffer_size);

/** Enqueues a used buffer into the virtqueue_buff_t. Called by
 *  the device role.
 *
 * @param virtqueue A handle to a virtqueue.
 * @param buff A pointer to a buffer of data from the caller which would
 *               like to enqueue into the used ring of buffers.
 * @return Success code. 0 for success, -1 for failure
 */
int virtqueue_enqueue_used_buff(virtqueue_t *virtqueue,
        void *buffer, size_t buffer_size);

/** Dequeues an available buffer from the virtqueue_buff_t. Called by
 *  the device role.
 *
 * @param virtqueue A handle to a virtqueue.
 * @param buff A pointer to a buffer (pointer) that the caller would
 *               like us to initialise with an available buffer.
 * @return Success code. 0 for success, -1 for failure
 */
int virtqueue_dequeue_available_buff(virtqueue_t *virtqueue,
        void **buffer, size_t *buffer_size);

/** Dequeues a used buffer from the virtqueue_buff_t. Called by
 *  the driver role.
 *
 * @param virtqueue A handle to a virtqueue.
 * @param buff A pointer to a buffer (pointer) that the caller would
 *               like us to initialise with a used buffer.
 * @return Success code. 0 for success, -1 for failure
 */
int virtqueue_dequeue_used_buff(virtqueue_t *virtqueue,
        void **buffer, size_t *buffer_size);

/** Signals the virtqueue which will wake up the "waiting end" of this
 * queue and tell it there is one or more buffer queued up for it to
 * consume.
 *
 * Call this after a call to virtqueue_enqueue_available_buff().
 */
int virtqueue_signal(virtqueue_t *vq);


/** Poll the virtqueue to see if there is an available work unit.
 *
 * @param vq Initialized instance of the virtqueue.
 * @return 1 to indicate that work is available, 0 otherwise
 */
int virtqueue_poll(virtqueue_t *vq);
