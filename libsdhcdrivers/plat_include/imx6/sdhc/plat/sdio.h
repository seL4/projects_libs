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

#pragma once

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

enum sdio_id {
    SDHC1 = 1,
    SDHC2,
    SDHC3,
    SDHC4,
    NSDHC,
    SDHC_DEFAULT = SDHC4
};
