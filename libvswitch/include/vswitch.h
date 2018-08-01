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

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <net/ethernet.h>

#include <virtqueue.h>

/* TODO: Need to introduce or paramterise this variable into the
 * build system
 */
#define CONFIG_SEL4VSWITCH_NUM_NODES           (4)
/* MAC address print format*/
#define PR_MAC802_ADDR                      "%x:%x:%x:%x:%x:%x"
/* Expects a *pointer* to a struct ether_addr */
#define PR_MAC802_ADDR_ARGS(a)              (a)->ether_addr_octet[0], \
                                            (a)->ether_addr_octet[1], \
                                            (a)->ether_addr_octet[2], \
                                            (a)->ether_addr_octet[3], \
                                            (a)->ether_addr_octet[4], \
                                            (a)->ether_addr_octet[5]

/*
 * Represents the virtqueues used in a given link between
 * two nodes.
 */
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

/*
 * Each component participating in a vswitch topology should have a
 * MAC address assigned to them. It is expected during the initialisation of
 * these nodes they will call "vswitch_connect()" to register the
 * MAC address of the other nodes it is linked to. This will inturn
 * populate the vswitch_t data structure.
 *
 * Furthermore, each node is expected to have a send and recieve
 * virtqueue for each destination node it has connection to.
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

/** Initializes metadata to track a destination node connected to the VSWITCH
 * bcast domain.
 *
 * During initialization of each component it is expected to
 * call this function to register the MAC address and virtqueues of all
 * the other nodes it is connected to.
 *
 * @param lib An initialized handle an instance of this library.
 * @param guest_macaddr A pointer to a mac address for the Guest VM being
 *                      registered.
 * @param send_virtqueue An initialized handle to a send virtqueue for
 *                        the destination being registered.
 * @param recv_virtqueue An initialized handle to a recieve virtqueue for
 *                        the destination being registered.
 */
int vswitch_connect(vswitch_t *lib,
                        struct ether_addr *guest_macaddr,
                        virtqueue_t *send_virtqueue,
                        virtqueue_t *recv_virtqueue);

/** Checks to see if a destination with the MAC address "mac" has been registered with
 * the library.
 *
 * @param lib Initialized instance of this library.
 * @param mac Mac address of the destination to be looked up.
 * @return Positive integer if the specified MAC address has been registered.
 *         Negative integer if not.
 */
int vswitch_get_destnode_index_by_macaddr(vswitch_t *lib,
                                              struct ether_addr *mac);

/** Used to iterate through all the registered destinations indiscriminately.
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
