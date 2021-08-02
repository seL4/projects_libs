/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdio.h>

#include <virtqueue.h>

#define IS_POWER_OF_2(x) (!((x) & ((x) - 1)))

void virtqueue_init_driver(virtqueue_driver_t *vq, unsigned queue_len, vq_vring_avail_t *avail_ring,
                           vq_vring_used_t *used_ring, vq_vring_desc_t *desc, void (*notify)(void),
                           void *cookie)
{
    vq->free_desc_head = 0;
    vq->queue_len = queue_len;
    vq->u_ring_last_seen = vq->queue_len - 1;
    vq->avail_ring = avail_ring;
    vq->used_ring = used_ring;
    vq->desc_table = desc;
    vq->notify = notify;
    vq->cookie = cookie;
    virtqueue_init_desc_table(desc, vq->queue_len);
    virtqueue_init_avail_ring(avail_ring);
    virtqueue_init_used_ring(used_ring);

}

void virtqueue_init_device(virtqueue_device_t *vq, unsigned queue_len, vq_vring_avail_t *avail_ring,
                           vq_vring_used_t *used_ring, vq_vring_desc_t *desc, void (*notify)(void),
                           void *cookie)
{
    vq->queue_len = queue_len;
    vq->a_ring_last_seen = vq->queue_len - 1;
    vq->avail_ring = avail_ring;
    vq->used_ring = used_ring;
    vq->desc_table = desc;
    vq->notify = notify;
    vq->cookie = cookie;
}

/*@ requires \block_length(table) == queue_len * sizeof(vq_vring_desc_t);
  @ requires \valid(table + (0 .. queue_len - 1));
  @*/
void virtqueue_init_desc_table(vq_vring_desc_t *table, unsigned queue_len)
{
    unsigned i;
    /*@ loop variant queue_len - i; */
    for (i = 0; i < queue_len; i++) {
        table[i].addr = 0;
        table[i].len = 0;
        table[i].flags = 0;
        table[i].next = i + 1;
    }
    /*@ assert table[0].addr == 0; */
}

/*@ requires \valid(ring);
  @ ensures \valid(ring);
  @ ensures ring->flags == 0 && ring->idx == 0;
  @*/
void virtqueue_init_avail_ring(vq_vring_avail_t *ring)
{
    ring->flags = 0;
    ring->idx = 0;
  /*@ assert \valid(ring); */
  /*@ assert ring->flags == 0 && ring->idx == 0; */
}

/*@ requires \valid(ring);
  @ ensures \valid(ring);
  @ ensures ring->flags == 0 && ring->idx == 0;
  @*/
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
    if (new == vq->queue_len) {
        return new;
    }
    vq->free_desc_head = vq->desc_table[new].next;
    desc = vq->desc_table + new;

    // casting pointers to integers directly is not allowed, must cast the
    // pointer to a uintptr_t first
    desc->addr = (uintptr_t)buf;

    desc->len = len;
    desc->flags = flag;
    desc->next = vq->queue_len;

    if (prev < vq->queue_len) {
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

int virtqueue_add_available_buf(virtqueue_driver_t *vq, virtqueue_ring_object_t *obj,
                                void *buf, unsigned len, vq_flags_t flag)
{
    unsigned idx;

    /* If descriptor table full */
    if ((idx = vq_add_desc(vq, buf, len, flag, obj->cur)) == vq->queue_len) {
        return 0;
    }
    obj->cur = idx;

    /* If this is the first buffer in the descriptor chain */
    if (obj->first >= vq->queue_len) {
        obj->first = idx;
        vq->avail_ring->ring[vq->avail_ring->idx] = idx;
        vq->avail_ring->idx = (vq->avail_ring->idx + 1) & (vq->queue_len - 1);
    }
    return 1;
}

int virtqueue_get_used_buf(virtqueue_driver_t *vq, virtqueue_ring_object_t *obj, uint32_t *len)
{
    unsigned next = (vq->u_ring_last_seen + 1) & (vq->queue_len - 1);

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
    vq->used_ring->idx = (cur + 1) & (vq->queue_len - 1);
    return 1;
}

int virtqueue_get_available_buf(virtqueue_device_t *vq, virtqueue_ring_object_t *robj)
{
    unsigned next = (vq->a_ring_last_seen + 1) & (vq->queue_len - 1);

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
    obj->cur = (uint32_t) -1;
    obj->first = (uint32_t) -1;
}

uint32_t virtqueue_scattered_available_size(virtqueue_device_t *vq, virtqueue_ring_object_t *robj)
{
    uint32_t ret = 0;
    unsigned cur = robj->first;

    while (cur < vq->queue_len) {
        ret += vq->desc_table[cur].len;
        cur = vq->desc_table[cur].next;
    }
    return ret;
}

int virtqueue_gather_available(virtqueue_device_t *vq, virtqueue_ring_object_t *robj,
                               void **buf, unsigned *len, vq_flags_t *flag)
{
    unsigned idx = robj->cur;

    if (idx >= vq->queue_len) {
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
    if (robj->cur >= vq->queue_len) {
        return 0;
    }
    robj->cur = vq_pop_desc(vq, robj->cur, buf, len, flag);
    return 1;
}
