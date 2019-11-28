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

#define TX2_HSP_PADDR 0x3c00000
#define TX2_HSP_SIZE 0xa0000

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
    void *hsp_base;
    void *doorbell_base;
} tx2_hsp_t;

int tx2_hsp_init(ps_io_ops_t *io_ops, tx2_hsp_t *hsp);

int tx2_hsp_destroy(ps_io_ops_t *io_ops, tx2_hsp_t *hsp);

int tx2_hsp_doorbell_ring(tx2_hsp_t *hsp, enum tx2_doorbell_id db_id);

int tx2_hsp_doorbell_check(tx2_hsp_t *hsp, enum tx2_doorbell_id db_id);
