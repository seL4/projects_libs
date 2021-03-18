/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#define SDHC0_PADDR 0x12510000
#define SDHC1_PADDR 0x12520000
#define SDHC2_PADDR 0x12530000
#define SDHC3_PADDR 0x12540000
#define SDHC4_PADDR 0x12550000

#define SDHC0_SIZE  0x1000
#define SDHC1_SIZE  0x1000
#define SDHC2_SIZE  0x1000
#define SDHC3_SIZE  0x1000
#define SDHC4_SIZE  0x1000

#define SDHC0_IRQ   105
#define SDHC1_IRQ   106
#define SDHC2_IRQ   107
#define SDHC3_IRQ   108
#define SDHC4_IRQ   109

enum sdio_id {
    SDHC0 = 0,
    SDHC1,
    SDHC2,
    SDHC3,
    SDHC4,
    NSDHC,
    SDHC_DEFAULT = SDHC2
};
