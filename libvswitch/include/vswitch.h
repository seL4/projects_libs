/*
 * Copyright 2017, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <net/ethernet.h>

#include <virtqueue.h>

/* Need to introduce this variable into the build system */
#define CONFIG_SEL4VSWITCH_NUM_NODES           (4)
#define PR_MAC802_ADDR                      "%x:%x:%x:%x:%x:%x"
/* Expects a *pointer* to a struct ether_addr */
#define PR_MAC802_ADDR_ARGS(a)              (a)->ether_addr_octet[0], \
                                            (a)->ether_addr_octet[1], \
                                            (a)->ether_addr_octet[2], \
                                            (a)->ether_addr_octet[3], \
                                            (a)->ether_addr_octet[4], \
                                            (a)->ether_addr_octet[5]

typedef struct vswitch_virtqueues_ {
    virtqueue_t *send_queue;
    virtqueue_t *recv_queue;
} vswitch_virtqueues_t;

extern struct ether_addr null_macaddr, bcast_macaddr;

static inline bool
mac802_addr_eq(struct ether_addr *addr0,
               struct ether_addr *addr1)
{
    return memcmp(addr0, addr1, sizeof(*addr0)) == 0;
}

static inline bool
mac802_addr_eq_bcast(struct ether_addr *addr)
{
    return memcmp(addr, &bcast_macaddr, sizeof(*addr)) == 0;
}

typedef struct vswitch_node_ {
    struct ether_addr addr;
    vswitch_virtqueues_t virtqueues;
} vswitch_node_t;

/* An instance of this structure will probably need to go into the shared
 * window that is shared between all the guests.
 *
 * Since it will be written to by multiple guests, and since it is necessary for
 * the other guests to see the writes done by their peers, we must instruct the
 * compiler to commit all stores to memory: hence "volatile".
 *
 * Essentially, CAmkES should have assigned a MAC address to each guest VM.
 * Each of the Guest VMs' templates should call "vswitch_connect()" while it
 * is initializing itself to inform this library of what its MAC address is.
 *
 * Furthermore, each Guest VM will have a bufferqueue of some kind
 * (sel4bufferqueue?) which this library will have to call on to get a
 * pointer to a buffer to use as the incoming queue for each of the guest VMs
 * (->_allocate()).
 *
 * I expect that the locking for the _allocate() method will be done internally
 * to the sel4bufferqueue library, so I shouldn't need to have a lock for each
 * Guest VM's connection in here.
 */
typedef struct vswitch_ {
    int n_connected;
    vswitch_node_t nodes[CONFIG_SEL4VSWITCH_NUM_NODES];
} vswitch_t;

/** Initialize an instance of this library
 * @param lib Uninitialized handle for a prospective instance of this library.
 * @return 0 on success.
 */
int vswitch_init(vswitch_t *lib);

/** Initializes metadata to track a Guest VM node connected to the VSWITCH
 * bcast domain.
 *
 * During initialization of each Guest, each Guest is expected to call this
 * function to register its MAC address and the location of its queue of
 * buffers.
 *
 * @param lib An initialized handle an instance of this library.
 * @param guest_macaddr A pointer to a mac address for the Guest VM being
 *                      registered.
 * @param guest_virtqueue An inialized handle to a virtqueue of some kind for
 *                        the Guest VM being registered. We will use this
 *                        virtqueue as the in-tray for new ethernet frames to be
 *                        delivered to the Guest VM being registered.
 */
int vswitch_connect(vswitch_t *lib,
                        struct ether_addr *guest_macaddr,
                        virtqueue_t *send_virtqueue,
                        virtqueue_t *recv_virtqueue);

/** Checks to see if a Guest with the MAC address "mac" has been registered with
 * the library.
 *
 * @param lib Initialized instance of this library.
 * @param mac Mac address of the destination Guest to be looked up.
 * @return Positive integer if the specified MAC address has been registered.
 *         Negative integer if not.
 */
int vswitch_get_destnode_index_by_macaddr(vswitch_t *lib,
                                              struct ether_addr *mac);

/** Used to iterate through all the registered guests indiscriminately.
 * @param lib Initialized instance of this library.
 * @param index Positive integer from 0 to CONFIG_SEL4VSWITCH_NUM_NODES.
 * @return NULL if an invalid index is supplied. Non-NULL if a valid index is
 *              supplied.
 */
vswitch_node_t *vswitch_get_destnode_by_index(vswitch_t *lib,
                                                      size_t index);

static vswitch_node_t *
vswitch_get_destnode_by_macaddr(vswitch_t *lib,
                                    struct ether_addr *mac)
{
    int idx;

    idx = vswitch_get_destnode_index_by_macaddr(lib, mac);
    if (idx < 0) {
        return NULL;
    }

    return vswitch_get_destnode_by_index(lib, idx);
}
