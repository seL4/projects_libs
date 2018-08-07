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

typedef void (*notify_fn)(void);

typedef struct {
    virtqueue_header_t *header;
    volatile void *buffer;
    notify_fn notify;
    virtqueue_role_t role;
    void *cookie;
} virtqueue_t;


/** Enqueues an available buffer into the virtqueue_buff_t. Called by
 *  the driver role.
 *
 * @param virtqueue A handle to a virtqueue.
 * @param buff A pointer to a buffer of data from the caller which would
 *               like to enqueue into the available ring of buffers.
 * @return Success code. 0 for success, -1 for failure
 */
typedef int (*virtqueue_enqueue_available_buff_fn)(virtqueue_t *virtqueue,
        volatile void *buffer, size_t buffer_size);

/** Enqueues a used buffer into the virtqueue_buff_t. Called by
 *  the device role.
 *
 * @param virtqueue A handle to a virtqueue.
 * @param buff A pointer to a buffer of data from the caller which would
 *               like to enqueue into the used ring of buffers.
 * @return Success code. 0 for success, -1 for failure
 */
typedef int (*virtqueue_enqueue_used_buff_fn)(virtqueue_t *virtqueue,
        volatile void *buffer, size_t buffer_size);

/** Dequeues an available buffer from the virtqueue_buff_t. Called by
 *  the device role.
 *
 * @param virtqueue A handle to a virtqueue.
 * @param buff A pointer to a buffer (pointer) that the caller would
 *               like us to initialise with an available buffer.
 * @return Success code. 0 for success, -1 for failure
 */
typedef int (*virtqueue_dequeue_available_buff_fn)(virtqueue_t *virtqueue,
        volatile void **buffer, size_t *buffer_size);

/** Dequeues a used buffer from the virtqueue_buff_t. Called by
 *  the driver role.
 *
 * @param virtqueue A handle to a virtqueue.
 * @param buff A pointer to a buffer (pointer) that the caller would
 *               like us to initialise with a used buffer.
 * @return Success code. 0 for success, -1 for failure
 */
typedef int (*virtqueue_dequeue_used_buff_fn)(virtqueue_t *virtqueue,
        volatile void **buffer, size_t *buffer_size);

/** Signals the virtqueue which will wake up the "waiting end" of this
 * queue and tell it there is one or more buffer queued up for it to
 * consume.
 *
 * Call this after a call to virtqueue_enqueue_available_buff().
 */
typedef int (*virtqueue_signal_fn)(virtqueue_t *vq);


/** Poll the virtqueue to see if there is an available work unit.
 *
 * @param vq Initialized instance of the virtqueue.
 * @return 1 to indicate that work is available, 0 otherwise
 */
typedef int (*virtqueue_poll_fn)(virtqueue_t *vq);

/* virtqueue_driver_t interface
 *
 * This is the side responsible for allocating and freeing buffers.
 * It can enqueue into available, and dequeue from used.
 * signal signals the device partner
 * poll polls for work in used queue
 */
typedef struct {
    virtqueue_t *data;
    virtqueue_enqueue_available_buff_fn enqueue_available_buff;
    virtqueue_dequeue_used_buff_fn dequeue_used_buff;
    virtqueue_signal_fn signal;
    virtqueue_poll_fn poll;
} virtqueue_driver_t;

/* virtqueue_device_t interface
 *
 * This side cannot allocate or free buffers.
 * It can dequeue from available, and enqueu into used.
 * signal signals the driver partner
 * poll polls for work in available queue.
 */
typedef struct {
    virtqueue_t *data;
    virtqueue_dequeue_available_buff_fn dequeue_available_buff;
    virtqueue_enqueue_used_buff_fn enqueue_used_buff;
    virtqueue_signal_fn signal;
    virtqueue_poll_fn poll;
} virtqueue_device_t;


/** Initialises a new virtqueue handle on the driver side.
 *
 * @param drv Pointer to a handle (pointer) to will be initialised with a
 *             virtqueue instance .
 * @param notify Pointer to the notify/signal function for the given
 *             virtqueue
 * @param shared_header_data The shared window of memory used for meta
 *             header information for the virtqueue
 * @param cookie Memory that the initialiser wishes to cache for storing
 *             personal state
 * @param malloc_ops The interface for allocating and freeing memory
 * @return Success code. 0 for success, -1 for failure
 */
int virtqueue_init_drv(virtqueue_driver_t **drv, notify_fn notify,
        virtqueue_header_t *shared_header_data, void *cookie,
        ps_malloc_ops_t *malloc_ops);

/** Initialises a new virtqueue handle on the device side.
 *
 * @param dev Pointer to a handle (pointer) to will be initialised with a
 *             virtqueue instance .
 * @param notify Pointer to the notify/signal function for the given
 *             virtqueue
 * @param shared_header_data The shared window of memory used for meta
 *             header information for the virtqueue
 * @param cookie Memory that the initialiser wishes to cache for storing
 *             personal state
 * @param malloc_ops The interface for allocating and freeing memory
 * @return Success code. 0 for success, -1 for failure
 */
int virtqueue_init_dev(virtqueue_device_t **dev, notify_fn notify,
        virtqueue_header_t *shared_header_data, void *cookie,
        ps_malloc_ops_t *malloc_ops);


/** Frees the virtqueue_driver_t. Released any managed memory for the
 * virtqueue handle
 */
int virtqueue_free_drv(virtqueue_driver_t *drv, ps_malloc_ops_t *malloc_ops);

/** Frees the virtqueue_device_t. Released any managed memory for the
 * virtqueue handle
 */
int virtqueue_free_dev(virtqueue_device_t *dev, ps_malloc_ops_t *malloc_ops);


/* The below functions are wrappers for the function type definitons defined above */

static inline int virtqueue_enqueue_available_buff(virtqueue_driver_t *drv, volatile void *buffer, size_t buffer_size) {
    if (drv == NULL) {
        ZF_LOGE("drv is NULL");
        return -1;
    }
    if (buffer == NULL) {
        ZF_LOGE("buffer is NULL");
        return -1;
    }
    if (buffer_size < 0) {
        ZF_LOGE("buffer_size is invalid");
        return -1;
    }
    return drv->enqueue_available_buff(drv->data, buffer, buffer_size);
}

static inline int virtqueue_dequeue_used_buff(virtqueue_driver_t *drv, volatile void **buffer, size_t *buffer_size) {
    if (drv == NULL) {
        ZF_LOGE("drv is NULL");
        return -1;
    }
    if (buffer == NULL) {
        ZF_LOGE("buffer is NULL");
        return -1;
    }
    if (buffer_size < 0) {
        ZF_LOGE("buffer_size is invalid");
        return -1;
    }
    return drv->dequeue_used_buff(drv->data, buffer, buffer_size);
}

static inline int virtqueue_signal_drv(virtqueue_driver_t *drv) {
    if (drv == NULL) {
        ZF_LOGE("drv is NULL");
        return -1;
    }
    return drv->signal(drv->data);
}

static inline int virtqueue_poll_drv(virtqueue_driver_t *drv) {
    if (drv == NULL) {
        ZF_LOGE("drv is NULL");
        return -1;
    }
    return drv->poll(drv->data);
}


static inline int virtqueue_dequeue_available_buff(virtqueue_device_t *dev, volatile void **buffer, size_t *buffer_size) {
    if (dev == NULL) {
        ZF_LOGE("dev is NULL");
        return -1;
    }
    if (buffer == NULL) {
        ZF_LOGE("buffer is NULL");
        return -1;
    }
    if (buffer_size < 0) {
        ZF_LOGE("buffer_size is invalid");
        return -1;
    }
    return dev->dequeue_available_buff(dev->data, buffer, buffer_size);
}

static inline int virtqueue_enqueue_used_buff(virtqueue_device_t *dev, volatile void *buffer, size_t buffer_size) {
    if (dev == NULL) {
        ZF_LOGE("dev is NULL");
        return -1;
    }
    if (buffer == NULL) {
        ZF_LOGE("buffer is NULL");
        return -1;
    }
    if (buffer_size < 0) {
        ZF_LOGE("buffer_size is invalid");
        return -1;
    }
    return dev->enqueue_used_buff(dev->data, buffer, buffer_size);
}

static inline int virtqueue_signal_dev(virtqueue_device_t *dev) {
    if (dev == NULL) {
        ZF_LOGE("dev is NULL");
        return -1;
    }
    return dev->signal(dev->data);
}

static inline int virtqueue_poll_dev(virtqueue_device_t *dev) {
    if (dev == NULL) {
        ZF_LOGE("dev is NULL");
        return -1;
    }
    return dev->poll(dev->data);
}
