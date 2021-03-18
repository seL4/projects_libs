/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <platsupport/io.h>
#include <sdhc/sdio.h>

struct sdhc {
    /* Device data */
    volatile void *base;
    int version;
    int nirqs;
    const int *irq_table;
    /* Transaction queue */
    struct mmc_cmd *cmd_list_head;
    struct mmc_cmd **cmd_list_tail;
    int blocks_remaining;
    /* DMA allocator */
    ps_dma_man_t *dalloc;
};
typedef struct sdhc *sdhc_dev_t;

int sdhc_init(void *iobase, const int *irq_table, int nirqs, ps_io_ops_t *io_ops,
              sdio_host_dev_t *dev);
