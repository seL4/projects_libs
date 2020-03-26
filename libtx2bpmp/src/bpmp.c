/*
 * Copyright (c) 2016, NVIDIA CORPORATION.
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

/*
 * This is a port of the Tegra186 BPMP sources from U-boot with some additional
 * modifications. Similar to the Tegra IVC protocol, there's no documentation
 * on the BPMP module ABI.
 */

#include <string.h>

#include <platsupport/pmem.h>
#include <tx2bpmp/bpmp.h>
#include <tx2bpmp/hsp.h>
#include <tx2bpmp/ivc.h>
#include <utils/util.h>

#define BPMP_IVC_FRAME_COUNT 1
#define BPMP_IVC_FRAME_SIZE 128

#define BPMP_FLAG_DO_ACK	BIT(0)
#define BPMP_FLAG_RING_DOORBELL	BIT(1)

#define TX_SHMEM 0
#define RX_SHMEM 1
#define NUM_SHMEM 2

#define TIMEOUT_THRESHOLD 2000000ul

struct tx2_bpmp {
    tx2_hsp_t hsp;
    bool hsp_initialised;
    struct tegra_ivc ivc;
    void *tx_base; // Virtual address base of the TX shared memory channel
    void *rx_base; // Virtual address base of the RX shared memory channel
};

pmem_region_t bpmp_shmems[NUM_SHMEM] = {
    {
        .type = PMEM_TYPE_DEVICE,
        .base_addr = TX2_BPMP_TX_SHMEM_PADDR,
        .length = TX2_BPMP_TX_SHMEM_SIZE
    },
    {
        .type = PMEM_TYPE_DEVICE,
        .base_addr = TX2_BPMP_RX_SHMEM_PADDR,
        .length = TX2_BPMP_RX_SHMEM_SIZE
    }
};

static bool bpmp_initialised = false;
static unsigned int bpmp_refcount = 0;
static struct tx2_bpmp bpmp_data = {0};

int tx2_bpmp_call(struct tx2_bpmp *bpmp, int mrq, void *tx_msg, size_t tx_size, void *rx_msg, size_t rx_size)
{
	int ret, err;
	void *ivc_frame;
	struct mrq_request *req;
	struct mrq_response *resp;
	unsigned long timeout = TIMEOUT_THRESHOLD;

	if ((tx_size > BPMP_IVC_FRAME_SIZE) || (rx_size > BPMP_IVC_FRAME_SIZE))
		return -EINVAL;

	ret = tegra_ivc_write_get_next_frame(&bpmp->ivc, &ivc_frame);
	if (ret) {
		ZF_LOGE("tegra_ivc_write_get_next_frame() failed: %d\n", ret);
		return ret;
	}

	req = ivc_frame;
	req->mrq = mrq;
	req->flags = BPMP_FLAG_DO_ACK | BPMP_FLAG_RING_DOORBELL;
	memcpy(req + 1, tx_msg, tx_size);

	ret = tegra_ivc_write_advance(&bpmp->ivc);
	if (ret) {
		ZF_LOGE("tegra_ivc_write_advance() failed: %d\n", ret);
		return ret;
	}

	for (; timeout > 0; timeout--) {
		ret = tegra_ivc_channel_notified(&bpmp->ivc);
		if (ret) {
			ZF_LOGE("tegra_ivc_channel_notified() failed: %d\n", ret);
			return ret;
		}

		ret = tegra_ivc_read_get_next_frame(&bpmp->ivc, &ivc_frame);
		if (!ret)
			break;
	}

    if (!timeout) {
        ZF_LOGE("tegra_ivc_read_get_next_frame() timed out (%d)\n", ret);
        return -ETIMEDOUT;
    }

	resp = ivc_frame;
	err = resp->err;
	if (!err && rx_msg && rx_size)
		memcpy(rx_msg, resp + 1, rx_size);

	ret = tegra_ivc_read_advance(&bpmp->ivc);
	if (ret) {
		ZF_LOGE("tegra_ivc_write_advance() failed: %d\n", ret);
		return ret;
	}

	if (err) {
		ZF_LOGE("BPMP responded with error %d\n", err);
		/* err isn't a U-Boot error code, so don't that */
		return -EIO;
	}

	return rx_size;
}

static void tx2_bpmp_ivc_notify(struct tegra_ivc *ivc, void *token)
{
	struct tx2_bpmp *bpmp = token;
	int ret;

	ret = tx2_hsp_doorbell_ring(&bpmp->hsp, BPMP_DBELL);
	if (ret)
		ZF_LOGF("Failed to ring BPMP's doorbell in the HSP: %d\n", ret);
}

int tx2_bpmp_init(ps_io_ops_t *io_ops, struct tx2_bpmp **bpmp)
{
    if (!io_ops || !bpmp) {
        ZF_LOGE("Arguments are NULL!");
        return -EINVAL;
    }

    if (bpmp_initialised) {
        /* If we've initialised the BPMP once, just return the initialised
         * structure */
        *bpmp = &bpmp_data;
        bpmp_refcount++;
        return 0;
    }

    int ret = 0;
    /* Not sure if this is too long or too short. */
    unsigned long timeout = TIMEOUT_THRESHOLD;

    ret = tx2_hsp_init(io_ops, &bpmp_data.hsp);
    if (ret) {
        ZF_LOGE("Failed to initialise the HSP device for BPMP");
        return ret;
    }

    bpmp_data.hsp_initialised = true;

    bpmp_data.tx_base = ps_pmem_map(io_ops, bpmp_shmems[TX_SHMEM], false, PS_MEM_NORMAL);
    if (!bpmp_data.tx_base) {
        ZF_LOGE("Failed to map the TX BPMP channel");
        ret = -ENOMEM;
        goto fail;
    }

    bpmp_data.rx_base = ps_pmem_map(io_ops, bpmp_shmems[RX_SHMEM], false, PS_MEM_NORMAL);
    if (!bpmp_data.rx_base) {
        ZF_LOGE("Failed to map the RX BPMP channel");
        ret = -ENOMEM;
        goto fail;
    }

    ret = tegra_ivc_init(&bpmp_data.ivc, (unsigned long) bpmp_data.rx_base, (unsigned long) bpmp_data.tx_base,
                         BPMP_IVC_FRAME_COUNT, BPMP_IVC_FRAME_SIZE, tx2_bpmp_ivc_notify, (void *) &bpmp_data);
    if (ret) {
        ZF_LOGE("tegra_ivc_init() failed: %d", ret);
        goto fail;
    }

    tegra_ivc_channel_reset(&bpmp_data.ivc);
    for (; timeout > 0; timeout--) {
        ret = tegra_ivc_channel_notified(&bpmp_data.ivc);
        if (!ret) {
            break;
        }
    }

    if (!timeout) {
        ZF_LOGE("Initial IVC reset timed out (%d)", ret);
        ret = -ETIMEDOUT;
        goto fail;
    }

    *bpmp = &bpmp_data;
    bpmp_refcount++;
    bpmp_initialised = true;

    return 0;

fail:
    ZF_LOGF_IF(tx2_bpmp_destroy(io_ops, &bpmp_data), "Failed to cleanup the BPMP after a failed initialisation");
    return ret;
}

int tx2_bpmp_destroy(ps_io_ops_t *io_ops, struct tx2_bpmp *bpmp)
{
    if (io_ops || bpmp) {
        ZF_LOGE("Invalid arguments!");
        return -EINVAL;
    }

    bpmp_refcount--;

    if (bpmp_refcount != 0) {
        /* Only cleanup the BPMP structure if there are no more references that are valid. */
        return 0;
    }

    if (bpmp_data.hsp_initialised) {
        ZF_LOGF_IF(tx2_hsp_destroy(io_ops, &bpmp_data.hsp),
                   "Failed to clean up after a failed BPMP initialisation process!");
    }

    /* Unmapping the shared memory also destroys the IVC */
    if (bpmp_data.tx_base) {
        ps_io_unmap(&io_ops->io_mapper, bpmp_data.tx_base, bpmp_shmems[TX_SHMEM].length);
    }

    if (bpmp_data.rx_base) {
        ps_io_unmap(&io_ops->io_mapper, bpmp_data.tx_base, bpmp_shmems[RX_SHMEM].length);
    }

    return 0;
}
