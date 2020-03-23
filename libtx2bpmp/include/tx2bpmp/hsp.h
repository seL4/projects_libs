/*
 * Copright 2019, Data61
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

#include <platsupport/io.h>
#include <utils/util.h>

#define TX2_HSP_PADDR 0x3c00000
#define TX2_HSP_SIZE 0xa0000

#define __HSP_CHECK_ARGS(function)                                                                  \
    do {                                                                                            \
        if (!hsp) { ZF_LOGE("hsp is NULL"); return -EINVAL; }                                       \
        if (!hsp->function) { ZF_LOGE(#function " function is not implemented"); return -ENOSYS; }  \
    } while(0)

/*
 * This is a very basic driver implementation of the TX2 HSP mechanisms. So
 * far, this only supports the doorbell functionality of the HSP mechanisms.
 *
 * The doorbells are essentially a signalling mechanism that allows the
 * processor and co-processors to notify one another of incoming requests. The
 * HSP mechanisms also offer more sophisticated synchronisation mechanisms like
 * semaphores, message queues, and stateful semaphores. However, because of
 * time and a lack of a need for these advanced features, support for these
 * advanced features are missing.
 *
 * This driver is mainly used by only the BPMP driver in this same library, but
 * it is possible to use it for other purposes, see DESIGN.md in the root of
 * the libtx2bpmp folder for more details.
 */

enum tx2_doorbell_id {
    /* See Table 121, above Section 14.5.2 in the technical reference manual */
    CCPLEX_PM_DBELL, // CCPLEX power management
    CCPLEX_TZ_UNSECURE_DBELL, // CCPLEX TrustZone not secure
    CCPLEX_TZ_SECURE_DBELL, // CCPLEX TrustZone secure
    BPMP_DBELL,
    SPE_DBELL,
    SCE_DBELL,
    APE_DBELL
};

typedef struct tx2_hsp {
    void *data;
    int (*ring)(void *data, enum tx2_doorbell_id db_id);
    int (*check)(void *data, enum tx2_doorbell_id db_id);
    int (*destroy)(void *data);
} tx2_hsp_t;

/*
 * Initialises the TX2 HSP interface.
 *
 * @param io_ops Initialised IO ops interface.
 * @param hsp Empty tx2 hsp struct that will be filled in.
 *
 * @return 0 on success, otherwise an error code.
 */
int tx2_hsp_init(ps_io_ops_t *io_ops, tx2_hsp_t *hsp);

/*
 * Destroys an initialised TX2 HSP interface.
 *
 * @param hsp Initialised HSP interface that will be destroyed.
 *
 * @return 0 on success, otherwise an error code.
 */
static inline int tx2_hsp_destroy(tx2_hsp_t *hsp)
{
    __HSP_CHECK_ARGS(destroy);
    return hsp->destroy(hsp->data);
}

/*
 * Rings the doorbell of a specific device module.
 *
 * @param hsp Initialised HSP interface.
 * @param db_id The ID of the desired doorbell to ring.
 *
 * @return 0 on success, otherwise an error code.
 */
static inline int tx2_hsp_doorbell_ring(tx2_hsp_t *hsp, enum tx2_doorbell_id db_id)
{
    __HSP_CHECK_ARGS(ring);
    return hsp->ring(hsp->data, db_id);
}

/*
 * Checks if a specific device module has rung our doorbell.
 *
 * @param hsp Initialised HSP interface.
 * @param db_id The ID of the corresponding device module doorbell to check.
 *
 * @return 0 on success, otherwise an error code.
 */
static inline int tx2_hsp_doorbell_check(tx2_hsp_t *hsp, enum tx2_doorbell_id db_id)
{
    __HSP_CHECK_ARGS(check);
    return hsp->check(hsp->check, db_id);
}
