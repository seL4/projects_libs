/*
 * Copyright 2020, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */

#include <errno.h>

#include <platsupport/reset.h>
#include <platsupport/plat/reset.h>

/* NVIDIA interface */
#include <tx2bpmp/bpmp.h> /* struct mrq_reset_request */

extern uint32_t mrq_reset_id_map[];

typedef struct tx2_reset {
    ps_io_ops_t *io_ops;
    struct tx2_bpmp *bpmp;
} tx2_reset_t;

static inline bool check_valid_reset(reset_id_t id)
{
    return (RESET_TOP_GTE <= id && id < NRESETS);
}

static int tx2_reset_common(void *data, reset_id_t id, bool assert)
{
    if (!check_valid_reset(id)) {
        ZF_LOGE("Invalid reset ID");
        return -EINVAL;
    }

    tx2_reset_t *reset = data;
    uint32_t bpmp_reset_id = mrq_reset_id_map[id];

    /* Setup a message and make a call to BPMP */
    struct mrq_reset_request req = { .reset_id = bpmp_reset_id };
    req.cmd = (assert) ? CMD_RESET_ASSERT : CMD_RESET_DEASSERT;

    int bytes_recvd = tx2_bpmp_call(reset->bpmp, MRQ_RESET, &req, sizeof(req), NULL, 0);
    if (bytes_recvd < 0) {
        return -EIO;
    }

    return 0;
}

static int tx2_reset_assert(void *data, reset_id_t id)
{
    return tx2_reset_common(data, id, true);
}

static int tx2_reset_deassert(void *data, reset_id_t id)
{
    return tx2_reset_common(data, id, false);
}

static int interface_search_handler(void *handler_data, void *interface_instance, char **properties)
{
    /* Select the first one that is registered */
    tx2_reset_t *reset = handler_data;
    reset->bpmp = (struct tx2_bpmp *) interface_instance;
    return PS_INTERFACE_FOUND_MATCH;
}

int reset_sys_init(ps_io_ops_t *io_ops, void *dependencies, reset_sys_t *reset_sys)
{
    if (!io_ops || !reset_sys) {
        if (!io_ops) {
            ZF_LOGE("null io_ops argument");
        }

        if (!reset_sys) {
            ZF_LOGE("null reset_sys argument");
        }

        return -EINVAL;
    }

    int error = 0;
    bool bpmp_allocated = false;
    tx2_reset_t *reset = NULL;
    error = ps_calloc(&io_ops->malloc_ops, 1, sizeof(tx2_reset_t), (void **) &reset_sys->data);
    if (error) {
        ZF_LOGE("Failed to allocate memory for reset sys internal structure");
        error = -ENOMEM;
        goto fail;
    }

    reset = reset_sys->data;

    if (dependencies) {
        reset->bpmp = (struct tx2_bpmp *) dependencies;
    } else {
        /* See if there's a registered interface for the BPMP, if not, then we
         * initialise one ourselves. */
        error = ps_interface_find(&io_ops->interface_registration_ops, TX2_BPMP_INTERFACE,
                                  interface_search_handler, reset);
        if (error) {
            error = ps_calloc(&io_ops->malloc_ops, 1, sizeof(struct tx2_bpmp), (void **) &reset->bpmp);
            if (error) {
                ZF_LOGE("Failed to allocate memory for the BPMP structure to be initialised");
                goto fail;
            }

            error = tx2_bpmp_init(io_ops, reset->bpmp);
            if (error) {
                ZF_LOGE("Failed to initialise the BPMP");
                goto fail;
            }
        }
    }

    reset_sys->reset_assert = &tx2_reset_assert;
    reset_sys->reset_deassert = &tx2_reset_deassert;

    return 0;

fail:

    if (reset_sys->data) {
        if (reset->bpmp) {
            ZF_LOGF_IF(ps_free(&io_ops->malloc_ops, sizeof(struct tx2_bpmp), (void *) reset->bpmp),
                       "Failed to free the BPMP structure after a failed reset subsystem initialisation");
        }
        ZF_LOGF_IF(ps_free(&io_ops->malloc_ops, sizeof(tx2_reset_t), (void *) reset_sys->data),
                   "Failed to free the reset private data after a failed reset subsystem initialisation");
    }

    return error;
}
