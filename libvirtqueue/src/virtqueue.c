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

#include <virtqueue.h>

void virtqueue_init_driver(virtqueue_driver_t *vq, vq_vring_avail_t *avail_ring,
                           vq_vring_used_t *used_ring, vq_vring_desc_t *desc, void (*notify)(void),
                           void *cookie)
{
    vq->free_desc_head = 0;
    vq->u_ring_last_seen = RING_SIZE - 1;
    vq->avail_ring = avail_ring;
    vq->used_ring = used_ring;
    vq->desc_table = desc;
    vq->notify = notify;
    vq->cookie = cookie;
}

void virtqueue_init_device(virtqueue_device_t *vq, vq_vring_avail_t *avail_ring,
                           vq_vring_used_t *used_ring, vq_vring_desc_t *desc, void (*notify)(void),
                           void *cookie)
{
    vq->a_ring_last_seen = RING_SIZE - 1;
    vq->avail_ring = avail_ring;
    vq->used_ring = used_ring;
    vq->desc_table = desc;
    vq->notify = notify;
    vq->cookie = cookie;
}

void virtqueue_init_desc_table(vq_vring_desc_t *table)
{
    unsigned i;
    for (i = 0; i < DESC_TABLE_SIZE; i++) {
        table[i].addr = 0;
        table[i].len = 0;
        table[i].flags = 0;
        table[i].next = i + 1;
    }
}

void virtqueue_init_avail_ring(vq_vring_avail_t *ring)
{
    ring->flags = 0;
    ring->idx = 0;
}

void virtqueue_init_used_ring(vq_vring_used_t *ring)
{
    ring->flags = 0;
    ring->idx = 0;
}

static unsigned vq_add_desc(virtqueue_driver_t *vq, void *buf, unsigned len,
                            vq_flags_t flag, int prev)
{
    unsigned new;
    vq_vring_desc_t *desc;

    new = vq->free_desc_head;
    if (new == DESC_TABLE_SIZE) {
        return new;
    }
    vq->free_desc_head = vq->desc_table[new].next;
    desc = vq->desc_table + new;

    // casting pointers to integers directly is not allowed, must cast the
    // pointer to a uintptr_t first
    desc->addr = (uintptr_t)buf;

    desc->len = len;
    desc->flags = flag;
    desc->next = DESC_TABLE_SIZE;

    if (prev != DESC_TABLE_SIZE) {
        desc = vq->desc_table + prev;
        desc->next = new;
    }
    return new;
}

static unsigned vq_pop_desc(virtqueue_driver_t *vq, unsigned idx,
                            void **buf, unsigned *len, vq_flags_t *flag)
{
    unsigned next = vq->desc_table[idx].next;

    // casting integers to pointers directly is not allowed, must cast the
    // integer to a uintptr_t first
    *buf = (void *)(uintptr_t)(vq->desc_table[idx].addr);

    *len = vq->desc_table[idx].len;
    *flag = vq->desc_table[idx].flags;
    vq->desc_table[idx].next = vq->free_desc_head;
    vq->free_desc_head = idx;

    return next;
}

static void vq_free_chain(virtqueue_driver_t *vq, unsigned head)
{
    while (head != DESC_TABLE_SIZE) {
        vq->desc_table[head].next = vq->free_desc_head;
        vq->free_desc_head = head;
        head = vq->desc_table[head].next;
    }
}

int virtqueue_add_available_buf(virtqueue_driver_t *vq, virtqueue_ring_object_t *obj,
                                void *buf, unsigned len, vq_flags_t flag)
{
    unsigned idx;

    /* If descriptor table full */
    if ((idx = vq_add_desc(vq, buf, len, flag, obj->cur)) == DESC_TABLE_SIZE) {
        return 0;
    }
    obj->cur = idx;

    /* If this is the first buffer in the descriptor chain */
    if (obj->first == DESC_TABLE_SIZE) {
        obj->first = idx;
        vq->avail_ring->ring[vq->avail_ring->idx] = idx;
        vq->avail_ring->idx = (vq->avail_ring->idx + 1) & (RING_SIZE - 1);
    }
    return 1;
}

int virtqueue_get_used_buf(virtqueue_driver_t *vq, virtqueue_ring_object_t *obj, uint32_t *len)
{
    unsigned next = (vq->u_ring_last_seen + 1) & (RING_SIZE - 1);

    if (next == vq->used_ring->idx) {
        return 0;
    }
    obj->first = vq->used_ring->ring[next].id;
    *len = vq->used_ring->ring[next].len;
    obj->cur = obj->first;
    vq->u_ring_last_seen = next;
    return 1;
}

int virtqueue_add_used_buf(virtqueue_device_t *vq, virtqueue_ring_object_t *robj, uint32_t len)
{
    unsigned cur = vq->used_ring->idx;

    vq->used_ring->ring[cur].id = robj->first;
    vq->used_ring->ring[cur].len = len;
    vq->used_ring->idx = (cur + 1) & (RING_SIZE - 1);
    return 1;
}

int virtqueue_get_available_buf(virtqueue_device_t *vq, virtqueue_ring_object_t *robj)
{
    unsigned next = (vq->a_ring_last_seen + 1) & (RING_SIZE - 1);

    if (next == vq->avail_ring->idx) {
        return 0;
    }
    robj->first = vq->avail_ring->ring[next];
    robj->cur = robj->first;
    vq->a_ring_last_seen = next;
    return 1;
}

void virtqueue_init_ring_object(virtqueue_ring_object_t *obj)
{
    obj->cur = DESC_TABLE_SIZE;
    obj->first = DESC_TABLE_SIZE;
}

uint32_t virtqueue_scattered_available_size(virtqueue_device_t *vq, virtqueue_ring_object_t *robj)
{
    uint32_t ret = 0;
    unsigned cur = robj->first;

    while (cur != DESC_TABLE_SIZE) {
        ret += vq->desc_table[cur].len;
        cur = vq->desc_table[cur].next;
    }
    return ret;
}

int virtqueue_gather_available(virtqueue_device_t *vq, virtqueue_ring_object_t *robj,
                               void **buf, unsigned *len, vq_flags_t *flag)
{
    unsigned idx = robj->cur;

    if (idx == DESC_TABLE_SIZE) {
        return 0;
    }

    // casting integers to pointers directly is not allowed, must cast the
    // integer to a uintptr_t first
    *buf = (void *)(uintptr_t)(vq->desc_table[idx].addr);

    *len = vq->desc_table[idx].len;
    *flag = vq->desc_table[idx].flags;
    robj->cur = vq->desc_table[idx].next;
    return 1;
}

int virtqueue_gather_used(virtqueue_driver_t *vq, virtqueue_ring_object_t *robj,
                          void **buf, unsigned *len, vq_flags_t *flag)
{
    if (robj->cur == DESC_TABLE_SIZE) {
        return 0;
    }
    robj->cur = vq_pop_desc(vq, robj->cur, buf, len, flag);
    return 1;
}
