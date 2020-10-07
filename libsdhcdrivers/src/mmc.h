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

#include <sdhc/mmc.h>
#include "sdhc.h"

/* MMC Standard Command Index,     Response Type */
#define MMC_GO_IDLE_STATE         0  //NONE
#define MMC_SEND_OP_COND          1  //R3
#define MMC_ALL_SEND_CID          2  //R2
#define MMC_SEND_RELATIVE_ADDR    3  //R1
#define MMC_SET_DSR               4  //NONE
#define MMC_IO_SEND_OP_COND       5  //R4
#define MMC_SWITCH                6  //R1
#define MMC_SELECT_CARD           7  //R1b
#define MMC_SEND_EXT_CSD          8  //R1
#define MMC_SEND_CSD              9  //R2
#define MMC_SEND_CID              10 //R2
#define MMC_READ_DAT_UNTIL_STOP   11 //R1
#define MMC_STOP_TRANSMISSION     12 //R1b
#define MMC_SEND_STATUS           13 //R1
#define MMC_GO_INACTIVE_STATE     15 //NONE
#define MMC_SET_BLOCKLEN          16 //R1
#define MMC_READ_SINGLE_BLOCK     17 //R1
#define MMC_READ_MULTIPLE_BLOCK   18 //R1
#define MMC_WRITE_DAT_UNTIL_STOP  20 //R1
#define MMC_WRITE_BLOCK           24 //R1
#define MMC_WRITE_MULTIPLE_BLOCK  25 //R1
#define MMC_PROGRAM_CID           26 //R1
#define MMC_PROGRAM_CSD           27 //R1
#define MMC_SET_WRITE_PROT        28 //R1b
#define MMC_CLR_WRITE_PROT        29 //R1b
#define MMC_SEND_WRITE_PROT       30 //R1
#define MMC_TAG_SECTOR_START      32 //R1
#define MMC_TAG_SECTOR_END        33 //R1
#define MMC_UNTAG_SECTOR          34 //R1
#define MMC_TAG_ERASE_GROUP_START 35 //R1
#define MMC_TAG_ERASE_GROUP_END   36 //R1
#define MMC_UNTAG_ERASE_GROUP     37 //R1
#define MMC_ERASE                 38 //R1b
#define MMC_FAST_IO               39 //R4
#define MMC_GO_IRQ_STATE          40 //R5
#define MMC_LOCK_UNLOCK           42 //R1b
#define MMC_IO_RW_DIRECT          52 //R5
#define MMC_IO_RW_EXTENDED        53 //R5
#define MMC_APP_CMD               55 //R1
#define MMC_GEN_CMD               56 //R1b
#define MMC_RW_MULTIPLE_REGISTER  60 //R1b
#define MMC_RW_MULTIPLE_BLOCK     61 //R1b

/* Application Specific Command(ACMD). */
#define SD_SET_BUS_WIDTH          6  //R1
#define SD_SD_STATUS              13 //R1
#define SD_SEND_NUM_WR_SECTORS    22 //R1
#define SD_SET_WR_BLK_ERASE_COUNT 23 //R1
#define SD_SD_APP_OP_COND         41 //R1
#define SD_SET_CLR_CARD_DETECT    42 //R1
#define SD_SEND_SCR               51 //R1

/* MMC Voltage Level */
#define MMC_VDD_33_34             (1 << 21)
#define MMC_VDD_32_33             (1 << 20)
#define MMC_VDD_31_32             (1 << 19)
#define MMC_VDD_30_31             (1 << 18)
#define MMC_VDD_29_30             (1 << 17)


enum mmc_rsp_type {
    MMC_RSP_TYPE_NONE = 0,
    MMC_RSP_TYPE_R1,
    MMC_RSP_TYPE_R1b,
    MMC_RSP_TYPE_R2,
    MMC_RSP_TYPE_R3,
    MMC_RSP_TYPE_R4,
    MMC_RSP_TYPE_R5,
    MMC_RSP_TYPE_R5b,
    MMC_RSP_TYPE_R6,
};

enum mmc_card_type {
    CARD_TYPE_UNKNOWN = 0,
    CARD_TYPE_MMC,
    CARD_TYPE_SD,
    CARD_TYPE_SDIO,
};

enum mmc_card_status {
    CARD_STS_ACTIVE = 0,
    CARD_STS_INACTIVE,
    CARD_STS_BUSY,
};

struct mmc_data {
    uintptr_t  pbuf;
    void      *vbuf;
    uint32_t   data_addr;
    uint32_t   block_size;
    uint32_t   blocks;
};

struct mmc_cmd {
    /* Data */
    uint32_t index;
    uint32_t arg;
    uint32_t response[4];
    struct mmc_data *data;
    /* Type */
    enum mmc_rsp_type rsp_type;
    /* For async handling */
    sdio_cb         cb;
    void           *token;
    /* For queueing */
    struct mmc_cmd *next;
    int complete;
};

struct cid {
    uint8_t reserved;
    uint8_t manfid;
    union {
        struct {
            uint8_t  bga;
            uint8_t  oemid;
            char     name[6];
            uint8_t  rev;
            uint32_t serial;
            uint8_t  date;
        } mmc_cid;
        struct {
            uint16_t oemid;
            char     name[5];
            uint8_t  rev;
            uint32_t serial;
            uint16_t date;
        } sd_cid;
    };
} __attribute__((packed));

struct csd {
    uint8_t structure;
    uint8_t tran_speed;
    uint8_t read_bl_len;
    uint32_t c_size;
    uint8_t  c_size_mult;
};

struct mmc_card {
    uint32_t ocr;
    uint32_t raw_cid[4];
    uint32_t raw_csd[4];
    uint16_t raw_rca;
    uint32_t raw_scr[2];
    uint32_t type;
    uint32_t voltage;
    uint32_t version;
    uint32_t high_capacity;
    uint32_t status;
    ps_dma_man_t *dalloc;
    sdio_host_dev_t *sdio;
};

static inline int host_send_command(struct mmc_card *card, struct mmc_cmd *cmd, sdio_cb cb, void *token)
{
    return sdio_send_command(card->sdio, cmd, cb, token);
}

static inline int host_nth_irq(mmc_card_t card, int n)
{
    return sdio_nth_irq(card->sdio, n);
}

static inline int host_handle_irq(struct mmc_card *card, int irq)
{
    return sdio_handle_irq(card->sdio, irq);
}

static inline int host_is_voltage_compatible(struct mmc_card *card, int mv)
{
    return sdio_is_voltage_compatible(card->sdio, mv);
}


static inline int host_reset(struct mmc_card *card)
{
    return sdio_reset(card->sdio);
}





/* void card_data(...) */
