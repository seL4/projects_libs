/*
 * Copyright 2018, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <vswitch.h>
#include <utils/zf_log.h>
#include <utils/fence.h>

struct ether_addr null_macaddr = { .ether_addr_octet = {0, 0, 0, 0, 0, 0} };
struct ether_addr bcast_macaddr = { .ether_addr_octet = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
    }
};

static int vswitch_find_free_slot(vswitch_t *lib)
{
    for (int i = 0; i < VSWITCH_NUM_NODES; i++) {
        if (mac802_addr_eq((void *)&lib->nodes[i].addr, &null_macaddr)) {
            return i;
        }
    }

    return -1;
}

int vswitch_init(vswitch_t *lib)
{
    memset((void *)lib, 0, sizeof(*lib));
    return 0;
}

int vswitch_connect(vswitch_t *lib,
                    struct ether_addr *guest_macaddr,
                    virtqueue_driver_t *send_virtqueue,
                    virtqueue_device_t *recv_virtqueue)
{
    int slot;

    assert(lib->n_connected <= VSWITCH_NUM_NODES);

    if (lib->n_connected == VSWITCH_NUM_NODES) {
        ZF_LOGE("No slots remaining to allow client " PR_MAC802_ADDR " to "
                "connect.",
                PR_MAC802_ADDR_ARGS(guest_macaddr));

        return -1;
    }

    slot = vswitch_find_free_slot(lib);
    if (slot < 0) {
        ZF_LOGE("Failed to find free slot for new client " PR_MAC802_ADDR ".",
                PR_MAC802_ADDR_ARGS(guest_macaddr));
        return -1;
    }

    /* Fill out the node structure */
    memcpy((void *)&lib->nodes[slot].addr, guest_macaddr,
           sizeof(*guest_macaddr));
    lib->nodes[slot].virtqueues.send_queue = send_virtqueue;
    lib->nodes[slot].virtqueues.recv_queue = recv_virtqueue;
    lib->n_connected++;

    ZF_LOGI("Added new route to guest at MAC " PR_MAC802_ADDR,
            PR_MAC802_ADDR_ARGS(guest_macaddr));

    return 0;
}

int vswitch_get_destnode_index_by_macaddr(vswitch_t *lib,
                                          struct ether_addr *mac)
{
    for (int i = 0; i < VSWITCH_NUM_NODES; i++) {
        if (mac802_addr_eq((void *)&lib->nodes[i].addr, mac)) {
            return i;
        }
    }

    return -1;
}

vswitch_node_t *vswitch_get_destnode_by_index(vswitch_t *lib, size_t index)
{
    if (mac802_addr_eq((void *)&lib->nodes[index].addr, &null_macaddr)) {
        /* If the index requested is has a NULL mac addr in it, return
         * error.
         */
        return NULL;
    }

    return (vswitch_node_t *)&lib->nodes[index];
}
