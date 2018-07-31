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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <virtqueue.h>

/* Helper definitions */
static bool is_virtqueue_full(virtqueue_t *virtqueue);

int virtqueue_signal(virtqueue_t *vq) {
    /* Check that the virtqueue object has been created */
    if(vq == NULL) {
        return -1;
    }
    /* Check that the notify function is not NULL */
    if(vq->notify == NULL) {
        return -1;
    }
    vq->notify();
    return 0;
}

int virtqueue_enqueue_available_buff(virtqueue_t *virtqueue,
        void *buffer, size_t buffer_size) {
    /* Check that the virtqueue object has been created */
    if(virtqueue == NULL) {
        return -1;
    }
    if(virtqueue->header == NULL) {
        return -1;
    }
    if(is_virtqueue_full(virtqueue)) {
        return -1;
    }
    /*
     * Set the virtqueue buffer.
     * Note: This will overwrite the current buffer pointer. It is left the the API
     * user to manage the buffer memory and avoid memory leaks.
     */
    virtqueue->buffer = buffer;
    /* Update flags */
    virtqueue->header->available_flag = 1;
    virtqueue->header->used_flag = 0;
    virtqueue->header->buffer_size = buffer_size;
    return 0;
}


int virtqueue_enqueue_used_buff(virtqueue_t *virtqueue,
        void *buffer, size_t buffer_size) {
    /* Check that the virtqueue has been created */
    if(virtqueue == NULL) {
        return -1;
    }
    if(virtqueue->header == NULL) {
        return -1;
    }
    if(virtqueue->header->used_flag) {
        return -1;
    }
    virtqueue->buffer = buffer;
    /* Update flags */
    virtqueue->header->available_flag = 0;
    virtqueue->header->used_flag = 1;
    virtqueue->header->buffer_size = buffer_size;
    /* Won't do anything for now as the buffer address has already been set */
    return 0;
}


int virtqueue_dequeue_used_buff(virtqueue_t *virtqueue,
        void **buffer, size_t *buffer_size) {
    /* Check that the virtqueue has been created */
    if(virtqueue == NULL) {
        return -1;
    }
    if(virtqueue->header == NULL) {
        return -1;
    }
    virtqueue_header_t *header = virtqueue->header;
    /* Check there is a used buffer */
    if(!header->used_flag) {
        return -1;
    }
    /* Return the buffer */
    *buffer = virtqueue->buffer;
    *buffer_size = virtqueue->header->buffer_size;
    /* Update flags */
    virtqueue->header->available_flag = 0;
    virtqueue->header->used_flag = 0;
    return 0;
}


int virtqueue_dequeue_available_buff(virtqueue_t *virtqueue,
        void **buffer, size_t *buffer_size) {
    /* Check that the virtqueue has been created  */
    if(virtqueue == NULL) {
        return -1;
    }
    if(virtqueue->header == NULL) {
        return -1;
    }
    virtqueue_header_t *header = virtqueue->header;
    /* Check there is a buffer available */
    if(!header->available_flag) {
        return -1;
    }
    /* Return the buffer */
    *buffer = virtqueue->buffer;
    *buffer_size = virtqueue->header->buffer_size;
    return 0;
}

int virtqueue_init(virtqueue_t **vq, notify_fn_t notify,
        virtqueue_role_t role,
        virtqueue_header_t *shared_header_data, void *cookie,
        ps_malloc_ops_t *malloc_ops) {
    /* Check that the virtqueue pointer, header data and notify function pointer is not NULL */
    if(vq == NULL || notify == NULL || shared_header_data == NULL) {
        return -1;
    }
    /* Create a new virtqueue object */
    virtqueue_t *new_virtqueue = NULL;
    int err = ps_calloc(malloc_ops, 1, sizeof(virtqueue_t), (void **)&new_virtqueue);
    if(err) {
        return -1;
    }
    /* Initialise the contents of the new virtqueue */
    new_virtqueue->notify = notify;
    new_virtqueue->role = role;
    new_virtqueue->buffer = NULL;
    new_virtqueue->cookie = cookie;
    new_virtqueue->header = shared_header_data;
    new_virtqueue->header->available_flag = 0;
    new_virtqueue->header->used_flag = 0;
    new_virtqueue->header->buffer_size = 0;
    /* Initialise the callers virtqueue pointer */
    *vq = new_virtqueue;
    return 0;
}

void virtqueue_free(virtqueue_t *vq) {
    if(vq == NULL) {
        return;
    }
    /* Leave it the API user to clean up the data structures internals e.g.
     * cookie and buffer */
    free(vq);
}

int virtqueue_poll(virtqueue_t *vq) {
    /* Check that the virtqueue has been created  */
    if(vq == NULL) {
        return -1;
    }
    /* Check that the virtqueue header has been initalised  */
    if(vq->header == NULL) {
        return -1;
    }
    virtqueue_header_t *header = vq->header;
    if(vq->role == VIRTQUEUE_DRIVER) {
        return header->used_flag;
    } else if (vq->role == VIRTQUEUE_DEVICE) {
        return header->available_flag;
    }
    return 0;
}

/* Helpers */
static bool is_virtqueue_full(virtqueue_t *virtqueue) {
    virtqueue_header_t *header = virtqueue->header;
    if (header->used_flag || header->available_flag) {
        return true;
    }
    return false;
}
