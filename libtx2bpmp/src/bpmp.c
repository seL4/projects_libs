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
#include <platsupport/fdt.h>
#include <platsupport/driver_module.h>
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

struct tx2_bpmp_priv {
    ps_io_ops_t *io_ops;
    tx2_hsp_t hsp;
    bool hsp_initialised;
    struct tegra_ivc ivc;
    void *tx_base; // Virtual address base of the TX shared memory channel
    void *rx_base; // Virtual address base of the RX shared memory channel
    pmem_region_t bpmp_shmems[NUM_SHMEM];
};


static bool bpmp_initialised = false;
static unsigned int bpmp_refcount = 0;
static struct tx2_bpmp_priv bpmp_data = {0};

static int bpmp_call(void *data, int mrq, void *tx_msg, size_t tx_size, void *rx_msg, size_t rx_size)
{
	int ret, err;
	void *ivc_frame;
	struct mrq_request *req;
	struct mrq_response *resp;
	unsigned long timeout = TIMEOUT_THRESHOLD;

    struct tx2_bpmp_priv *bpmp_priv = data;

	if ((tx_size > BPMP_IVC_FRAME_SIZE) || (rx_size > BPMP_IVC_FRAME_SIZE))
		return -EINVAL;

	ret = tegra_ivc_write_get_next_frame(&bpmp_priv->ivc, &ivc_frame);
	if (ret) {
		ZF_LOGE("tegra_ivc_write_get_next_frame() failed: %d\n", ret);
		return ret;
	}

	req = ivc_frame;
	req->mrq = mrq;
	req->flags = BPMP_FLAG_DO_ACK | BPMP_FLAG_RING_DOORBELL;
	memcpy(req + 1, tx_msg, tx_size);

	ret = tegra_ivc_write_advance(&bpmp_priv->ivc);
	if (ret) {
		ZF_LOGE("tegra_ivc_write_advance() failed: %d\n", ret);
		return ret;
	}

	for (; timeout > 0; timeout--) {
		ret = tegra_ivc_channel_notified(&bpmp_priv->ivc);
		if (ret) {
			ZF_LOGE("tegra_ivc_channel_notified() failed: %d\n", ret);
			return ret;
		}

		ret = tegra_ivc_read_get_next_frame(&bpmp_priv->ivc, &ivc_frame);
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

	ret = tegra_ivc_read_advance(&bpmp_priv->ivc);
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

static void bpmp_ivc_notify(struct tegra_ivc *ivc, void *token)
{
	struct tx2_bpmp_priv *bpmp_priv = token;
	int ret;

	ret = tx2_hsp_doorbell_ring(&bpmp_priv->hsp, BPMP_DBELL);
	if (ret)
		ZF_LOGF("Failed to ring BPMP's doorbell in the HSP: %d\n", ret);
}

static int bpmp_destroy(void *data)
{
    struct tx2_bpmp_priv *bpmp_priv = data;

    bpmp_refcount--;

    if (bpmp_refcount != 0) {
        /* Only cleanup the BPMP structure if there are no more references that are valid. */
        return 0;
    }

    if (bpmp_priv->hsp_initialised) {
        ZF_LOGF_IF(tx2_hsp_destroy(&bpmp_priv->hsp),
                   "Failed to clean up after a failed BPMP initialisation process!");
    }

    /* Unmapping the shared memory also destroys the IVC */
    if (bpmp_priv->tx_base) {
        ps_io_unmap(&bpmp_priv->io_ops->io_mapper, bpmp_priv->tx_base, bpmp_data.bpmp_shmems[TX_SHMEM].length);
    }

    if (bpmp_priv->rx_base) {
        ps_io_unmap(&bpmp_priv->io_ops->io_mapper, bpmp_priv->rx_base, bpmp_data.bpmp_shmems[RX_SHMEM].length);
    }

    return 0;
}

static int allocate_register_callback(pmem_region_t pmem, unsigned curr_num, size_t num_regs, void *token)
{
    if (curr_num == 1) {
        bpmp_data.tx_base = ps_pmem_map(bpmp_data.io_ops, pmem, false, PS_MEM_NORMAL);
        if (!bpmp_data.tx_base) {
            ZF_LOGE("Failed to map the TX BPMP channel");
            return -EIO;
        }
        bpmp_data.bpmp_shmems[TX_SHMEM] = pmem;

    } else if (curr_num == 2) {
        bpmp_data.rx_base = ps_pmem_map(bpmp_data.io_ops, pmem, false, PS_MEM_NORMAL);
        if (!bpmp_data.rx_base) {
            ZF_LOGE("Failed to map the RX BPMP channel");
            return -EIO;
        }
        bpmp_data.bpmp_shmems[RX_SHMEM] = pmem;

    }
    return 0;
}


int tx2_bpmp_init(ps_io_ops_t *io_ops, struct tx2_bpmp *bpmp)
{
    if (!io_ops || !bpmp) {
        ZF_LOGE("Arguments are NULL!");
        return -EINVAL;
    }

    if (bpmp_initialised) {
        /* If we've initialised the BPMP once, just fill the private data with
         * what we've initialised */
        goto success;
    }

    int ret = 0;
    /* Not sure if this is too long or too short. */
    unsigned long timeout = TIMEOUT_THRESHOLD;

    ret = tx2_hsp_init(io_ops, &bpmp_data.hsp, "/tegra-hsp@3c00000");
    if (ret) {
        ZF_LOGE("Failed to initialise the HSP device for BPMP");
        return ret;
    }

    bpmp_data.io_ops = io_ops;

    bpmp_data.hsp_initialised = true;
    ps_fdt_cookie_t *cookie = NULL;

    ret = ps_fdt_read_path(&io_ops->io_fdt, &io_ops->malloc_ops, "/bpmp", &cookie);
    if (ret) {
        ZF_LOGE("Failed to find %s in device tree", "/bpmp");
        return -ENODEV;
    }

    /* walk the registers and allocate them */
    ret = ps_fdt_walk_registers(&io_ops->io_fdt, cookie, allocate_register_callback, NULL);
    if (ret) {
        ZF_LOGE("Failed to walk fdt node");
        return -ENODEV;
    }
    ret = ps_fdt_cleanup_cookie(&io_ops->malloc_ops, cookie);
    if (ret) {
        return -ENODEV;
    }

    ret = tegra_ivc_init(&bpmp_data.ivc, (unsigned long) bpmp_data.rx_base, (unsigned long) bpmp_data.tx_base,
                         BPMP_IVC_FRAME_COUNT, BPMP_IVC_FRAME_SIZE, bpmp_ivc_notify, (void *) &bpmp_data);
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
    ret = ps_interface_register(&io_ops->interface_registration_ops, TX2_BPMP_INTERFACE,
                                  bpmp, NULL);
    if (ret) {
        ZF_LOGE("Failed to register the BPMP interface");
        goto fail;
    }

success:
    bpmp_refcount++;

    bpmp->data = &bpmp_data;
    bpmp->call = bpmp_call;
    bpmp->destroy = bpmp_destroy;
    bpmp_initialised = true;
    /* Register this BPMP interface so that the reset driver can access it */

    return 0;

fail:
    ZF_LOGF_IF(bpmp_destroy(&bpmp_data), "Failed to cleanup the BPMP after a failed initialisation");
    return ret;
}



int tx2_bpmp_init_module(ps_io_ops_t *io_ops, const char *device_path) {
    struct tx2_bpmp *bpmp;
    int error = ps_calloc(&io_ops->malloc_ops, 1, sizeof(*bpmp), (void **)&bpmp);
    if (error) {
        ZF_LOGE("Failed to allocate struct for tx2_bpmp");
        return -1;
    }
    error =  tx2_bpmp_init(io_ops, bpmp);
    if (error) {
        ZF_LOGE("Failed to initialize bpmp driver");
        return -1;
    }
    return 0;

}

static const char*compatible_strings[] = {
    "nvidia,tegra186-bpmp",
    NULL
};


PS_DRIVER_MODULE_DEFINE(bpmp, compatible_strings, tx2_bpmp_init_module);
