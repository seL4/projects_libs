/*
 * Copyright 2017, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(DATA61_BSD)
 */

#include "../../sdhc.h"
#include "../../services.h"

#define SDHC1_PADDR 0x02190000
#define SDHC2_PADDR 0x02194000
#define SDHC3_PADDR 0x02198000
#define SDHC4_PADDR 0x0219C000

#define SDHC1_SIZE  0x1000
#define SDHC2_SIZE  0x1000
#define SDHC3_SIZE  0x1000
#define SDHC4_SIZE  0x1000

#define SDHC1_IRQ   54
#define SDHC2_IRQ   55
#define SDHC3_IRQ   56
#define SDHC4_IRQ   57

static const int
_sdhc_irq_table[] = {
    [SDHC1] = SDHC1_IRQ,
    [SDHC2] = SDHC2_IRQ,
    [SDHC3] = SDHC3_IRQ,
    [SDHC4] = SDHC4_IRQ
};

enum sdio_id
sdio_default_id(void)
{
    return SDHC_DEFAULT;
}

int
sdio_init(enum sdio_id id, ps_io_ops_t* io_ops, sdio_host_dev_t* dev)
{
    void* iobase;
    int ret;
    switch (id) {
    case SDHC1:
        iobase = RESOURCE(io_ops, SDHC1);
        break;
    case SDHC2:
        iobase = RESOURCE(io_ops, SDHC2);
        break;
    case SDHC3:
        iobase = RESOURCE(io_ops, SDHC3);
        break;
    case SDHC4:
        iobase = RESOURCE(io_ops, SDHC4);
        break;
    default:
        return -1;
    }
    if (iobase == NULL) {
        LOG_ERROR("Failed to map device memory for SDHC\n");
        return -1;
    }

    ret = sdhc_init(iobase, &_sdhc_irq_table[id], 1, io_ops, dev);
    if (ret) {
        LOG_ERROR("Failed to initialise SDHC\n");
        return -1;
    }
    return 0;
}


