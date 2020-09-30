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

#include "sdhc.h"

#include <autoconf.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "services.h"
#include "mmc.h"

#define DBG_INFO "info:"

//#define DEBUG
#undef DEBUG
#ifdef DEBUG
#define D(x, ...) printf(__VA_ARGS__)
#else
#define D(...) do{}while(0)
#endif

#define DS_ADDR               0x00 //DMA System Address
#define BLK_ATT               0x04 //Block Attributes
#define CMD_ARG               0x08 //Command Argument
#define CMD_XFR_TYP           0x0C //Command Transfer Type
#define CMD_RSP0              0x10 //Command Response0
#define CMD_RSP1              0x14 //Command Response1
#define CMD_RSP2              0x18 //Command Response2
#define CMD_RSP3              0x1C //Command Response3
#define DATA_BUFF_ACC_PORT    0x20 //Data Buffer Access Port
#define PRES_STATE            0x24 //Present State
#define PROT_CTRL             0x28 //Protocol Control
#define SYS_CTRL              0x2C //System Control
#define INT_STATUS            0x30 //Interrupt Status
#define INT_STATUS_EN         0x34 //Interrupt Status Enable
#define INT_SIGNAL_EN         0x38 //Interrupt Signal Enable
#define AUTOCMD12_ERR_STATUS  0x3C //Auto CMD12 Error Status
#define HOST_CTRL_CAP         0x40 //Host Controller Capabilities
#define WTMK_LVL              0x44 //Watermark Level
#define MIX_CTRL              0x48 //Mixer Control
#define FORCE_EVENT           0x50 //Force Event
#define ADMA_ERR_STATUS       0x54 //ADMA Error Status Register
#define ADMA_SYS_ADDR         0x58 //ADMA System Address
#define DLL_CTRL              0x60 //DLL (Delay Line) Control
#define DLL_STATUS            0x64 //DLL Status
#define CLK_TUNE_CTRL_STATUS  0x68 //CLK Tuning Control and Status
#define VEND_SPEC             0xC0 //Vendor Specific Register
#define MMC_BOOT              0xC4 //MMC Boot Register
#define VEND_SPEC2            0xC8 //Vendor Specific 2 Register
#define HOST_VERSION          0xFC //Host Version (0xFE adjusted for alignment)


/* Block Attributes Register */
#define BLK_ATT_BLKCNT_SHF      16        //Blocks Count For Current Transfer
#define BLK_ATT_BLKCNT_MASK     0xFFFF    //Blocks Count For Current Transfer
#define BLK_ATT_BLKSIZE_SHF     0         //Transfer Block Size
#define BLK_ATT_BLKSIZE_MASK    0xFFF     //Transfer Block Size

/* Command Transfer Type Register */
#define CMD_XFR_TYP_CMDINX_SHF  24        //Command Index
#define CMD_XFR_TYP_CMDINX_MASK 0x3F      //Command Index
#define CMD_XFR_TYP_CMDTYP_SHF  22        //Command Type
#define CMD_XFR_TYP_CMDTYP_MASK 0x3       //Command Type
#define CMD_XFR_TYP_DPSEL       (1 << 21) //Data Present Select
#define CMD_XFR_TYP_CICEN       (1 << 20) //Command Index Check Enable
#define CMD_XFR_TYP_CCCEN       (1 << 19) //Command CRC Check Enable
#define CMD_XFR_TYP_RSPTYP_SHF  16        //Response Type Select
#define CMD_XFR_TYP_RSPTYP_MASK 0x3       //Response Type Select

/* System Control Register */
#define SYS_CTRL_INITA          (1 << 27) //Initialization Active
#define SYS_CTRL_RSTD           (1 << 26) //Software Reset for DAT Line
#define SYS_CTRL_RSTC           (1 << 25) //Software Reset for CMD Line
#define SYS_CTRL_RSTA           (1 << 24) //Software Reset for ALL
#define SYS_CTRL_DTOCV_SHF      16        //Data Timeout Counter Value
#define SYS_CTRL_DTOCV_MASK     0xF       //Data Timeout Counter Value
#define SYS_CTRL_SDCLKS_SHF     8         //SDCLK Frequency Select
#define SYS_CTRL_SDCLKS_MASK    0xFF      //SDCLK Frequency Select
#define SYS_CTRL_DVS_SHF        4         //Divisor
#define SYS_CTRL_DVS_MASK       0xF       //Divisor
#define SYS_CTRL_CLK_INT_EN     (1 << 0)  //Internal clock enable (exl. IMX6)
#define SYS_CTRL_CLK_INT_STABLE (1 << 1)  //Internal clock stable (exl. IMX6)
#define SYS_CTRL_CLK_CARD_EN    (1 << 2)  //SD clock enable       (exl. IMX6)

/* Interrupt Status Register */
#define INT_STATUS_DMAE         (1 << 28) //DMA Error            (only IMX6)
#define INT_STATUS_TNE          (1 << 26) //Tuning Error
#define INT_STATUS_ADMAE        (1 << 25) //ADMA error           (exl. IMX6)
#define INT_STATUS_AC12E        (1 << 24) //Auto CMD12 Error
#define INT_STATUS_OVRCURE      (1 << 23) //Bus over current     (exl. IMX6)
#define INT_STATUS_DEBE         (1 << 22) //Data End Bit Error
#define INT_STATUS_DCE          (1 << 21) //Data CRC Error
#define INT_STATUS_DTOE         (1 << 20) //Data Timeout Error
#define INT_STATUS_CIE          (1 << 19) //Command Index Error
#define INT_STATUS_CEBE         (1 << 18) //Command End Bit Error
#define INT_STATUS_CCE          (1 << 17) //Command CRC Error
#define INT_STATUS_CTOE         (1 << 16) //Command Timeout Error
#define INT_STATUS_ERR          (1 << 15) //Error interrupt      (exl. IMX6)
#define INT_STATUS_TP           (1 << 14) //Tuning Pass
#define INT_STATUS_RTE          (1 << 12) //Re-Tuning Event
#define INT_STATUS_CINT         (1 << 8)  //Card Interrupt
#define INT_STATUS_CRM          (1 << 7)  //Card Removal
#define INT_STATUS_CINS         (1 << 6)  //Card Insertion
#define INT_STATUS_BRR          (1 << 5)  //Buffer Read Ready
#define INT_STATUS_BWR          (1 << 4)  //Buffer Write Ready
#define INT_STATUS_DINT         (1 << 3)  //DMA Interrupt
#define INT_STATUS_BGE          (1 << 2)  //Block Gap Event
#define INT_STATUS_TC           (1 << 1)  //Transfer Complete
#define INT_STATUS_CC           (1 << 0)  //Command Complete

/* Host Controller Capabilities Register */
#define HOST_CTRL_CAP_VS18      (1 << 26) //Voltage Support 1.8V
#define HOST_CTRL_CAP_VS30      (1 << 25) //Voltage Support 3.0V
#define HOST_CTRL_CAP_VS33      (1 << 24) //Voltage Support 3.3V
#define HOST_CTRL_CAP_SRS       (1 << 23) //Suspend/Resume Support
#define HOST_CTRL_CAP_DMAS      (1 << 22) //DMA Support
#define HOST_CTRL_CAP_HSS       (1 << 21) //High Speed Support
#define HOST_CTRL_CAP_ADMAS     (1 << 20) //ADMA Support
#define HOST_CTRL_CAP_MBL_SHF   16        //Max Block Length
#define HOST_CTRL_CAP_MBL_MASK  0x3       //Max Block Length

/* Mixer Control Register */
#define MIX_CTRL_MSBSEL         (1 << 5)  //Multi/Single Block Select.
#define MIX_CTRL_DTDSEL         (1 << 4)  //Data Transfer Direction Select.
#define MIX_CTRL_DDR_EN         (1 << 3)  //Dual Data Rate mode selection
#define MIX_CTRL_AC12EN         (1 << 2)  //Auto CMD12 Enable
#define MIX_CTRL_BCEN           (1 << 1)  //Block Count Enable
#define MIX_CTRL_DMAEN          (1 << 0)  //DMA Enable

/* Watermark Level register */
#define WTMK_LVL_WR_WML_SHF     16        //Write Watermark Level
#define WTMK_LVL_RD_WML_SHF     0         //Read  Watermark Level

#define writel(v, a)  (*(volatile uint32_t*)(a) = (v))
#define readl(a)      (*(volatile uint32_t*)(a))

enum dma_mode {
    DMA_MODE_NONE = 0,
    DMA_MODE_SDMA,
    DMA_MODE_ADMA
};

typedef enum {
    DIV_1   = 0x0,
    DIV_2   = 0x1,
    DIV_3   = 0x2,
    DIV_4   = 0x3,
    DIV_5   = 0x4,
    DIV_6   = 0x5,
    DIV_7   = 0x6,
    DIV_8   = 0x7,
    DIV_9   = 0x8,
    DIV_10  = 0x9,
    DIV_11  = 0xa,
    DIV_12  = 0xb,
    DIV_13  = 0xc,
    DIV_14  = 0xd,
    DIV_15  = 0xe,
    DIV_16  = 0xf,
} divisor;

/* Selecting the prescaler value varies between SDR and DDR mode. When the
 * value is set, this is accounted for with a bitshift (PRESCALER_X >> 1) */
typedef enum {
    PRESCALER_1   = 0x0, //Only available in SDR mode
    PRESCALER_2   = 0x1,
    PRESCALER_4   = 0x2,
    PRESCALER_8   = 0x4,
    PRESCALER_16  = 0x8,
    PRESCALER_32  = 0x10,
    PRESCALER_64  = 0x20,
    PRESCALER_128 = 0x40,
    PRESCALER_256 = 0x80,
    PRESCALER_512 = 0x100, //Only available in DDR mode
} sdclk_frequency_select;

typedef enum {
    CLOCK_INITIAL = 0,
    CLOCK_OPERATIONAL
} clock_mode;

typedef enum {
    SDCLK_TIMES_2_POW_29 = 0xf,
    SDCLK_TIMES_2_POW_28 = 0xe,
    SDCLK_TIMES_2_POW_14 = 0x0,
} data_timeout_counter_val;

static inline sdhc_dev_t sdio_get_sdhc(sdio_host_dev_t *sdio)
{
    return (sdhc_dev_t)sdio->priv;
}

/** Print uSDHC registers. */
UNUSED static void print_sdhc_regs(struct sdhc *host)
{
    int i;
    for (i = DS_ADDR; i <= HOST_VERSION; i += 0x4) {
        printf("%x: %X\n", i, readl(host->base + i));
    }
}

static inline enum dma_mode get_dma_mode(struct sdhc *host, struct mmc_cmd *cmd)
{
    if (cmd->data == NULL) {
        return DMA_MODE_NONE;
    }
    if (cmd->data->pbuf == 0) {
        return DMA_MODE_NONE;
    }
    /* Currently only SDMA supported */
    return DMA_MODE_SDMA;
}

static inline int cap_sdma_supported(struct sdhc *host)
{
    uint32_t v;
    v = readl(host->base + HOST_CTRL_CAP);
    return !!(v & HOST_CTRL_CAP_DMAS);
}

static inline int cap_max_buffer_size(struct sdhc *host)
{
    uint32_t v;
    v = readl(host->base + HOST_CTRL_CAP);
    v = ((v >> HOST_CTRL_CAP_MBL_SHF) & HOST_CTRL_CAP_MBL_MASK);
    return 512 << v;
}

static int sdhc_next_cmd(sdhc_dev_t host)
{
    struct mmc_cmd *cmd = host->cmd_list_head;
    uint32_t val;
    uint32_t mix_ctrl;

    /* Enable IRQs */
    val = (INT_STATUS_ADMAE | INT_STATUS_OVRCURE | INT_STATUS_DEBE
           | INT_STATUS_DCE   | INT_STATUS_DTOE    | INT_STATUS_CRM
           | INT_STATUS_CINS  | INT_STATUS_CIE     | INT_STATUS_CEBE
           | INT_STATUS_CCE   | INT_STATUS_CTOE    | INT_STATUS_TC
           | INT_STATUS_CC);
    if (get_dma_mode(host, cmd) == DMA_MODE_NONE) {
        val |= INT_STATUS_BRR | INT_STATUS_BWR;
    }
    writel(val, host->base + INT_STATUS_EN);

    /* Check if the Host is ready for transit. */
    while (readl(host->base + PRES_STATE) & (SDHC_PRES_STATE_CIHB | SDHC_PRES_STATE_CDIHB));
    while (readl(host->base + PRES_STATE) & SDHC_PRES_STATE_DLA);

    /* Two commands need to have at least 8 clock cycles in between.
     * Lets assume that the hcd will enforce this. */
    //udelay(1000);

    /* Write to the argument register. */
    D(DBG_INFO, "CMD: %d with arg %x ", cmd->index, cmd->arg);
    writel(cmd->arg, host->base + CMD_ARG);

    if (cmd->data) {
        /* Use the default timeout. */
        val = readl(host->base + SYS_CTRL);
        val &= ~(0xffUL << 16);
        val |= 0xE << 16;
        writel(val, host->base + SYS_CTRL);

        /* Set the DMA boundary. */
        val = (cmd->data->block_size & BLK_ATT_BLKSIZE_MASK);
        val |= (cmd->data->blocks << BLK_ATT_BLKCNT_SHF);
        writel(val, host->base + BLK_ATT);

        /* Set watermark level */
        val = cmd->data->block_size / 4;
        if (val > 0x80) {
            val = 0x80;
        }
        if (cmd->index == MMC_READ_SINGLE_BLOCK) {
            val = (val << WTMK_LVL_RD_WML_SHF);
        } else {
            val = (val << WTMK_LVL_WR_WML_SHF);
        }
        writel(val, host->base + WTMK_LVL);

        /* Set Mixer Control */
        mix_ctrl = MIX_CTRL_BCEN;
        if (cmd->data->blocks > 1) {
            mix_ctrl |= MIX_CTRL_MSBSEL;
        }
        if (cmd->index == MMC_READ_SINGLE_BLOCK) {
            mix_ctrl |= MIX_CTRL_DTDSEL;
        }

        /* Configure DMA */
        if (get_dma_mode(host, cmd) != DMA_MODE_NONE) {
            /* Enable DMA */
            mix_ctrl |= MIX_CTRL_DMAEN;
            /* Set DMA address */
            writel(cmd->data->pbuf, host->base + DS_ADDR);
        }
        /* Record the number of blocks to be sent */
        host->blocks_remaining = cmd->data->blocks;
    }

    /* The command should be MSB and the first two bits should be '00' */
    val = (cmd->index & CMD_XFR_TYP_CMDINX_MASK) << CMD_XFR_TYP_CMDINX_SHF;
    val &= ~(CMD_XFR_TYP_CMDTYP_MASK << CMD_XFR_TYP_CMDTYP_SHF);
    if (cmd->data) {
        if (host->version == 2) {
            /* Some controllers implement MIX_CTRL as part of the XFR_TYP */
            val |= mix_ctrl;
        } else {
            writel(mix_ctrl, host->base + MIX_CTRL);
        }
    }

    /* Set response type */
    val &= ~CMD_XFR_TYP_CICEN;
    val &= ~CMD_XFR_TYP_CCCEN;
    val &= ~(CMD_XFR_TYP_RSPTYP_MASK << CMD_XFR_TYP_RSPTYP_SHF);
    switch (cmd->rsp_type) {
    case MMC_RSP_TYPE_R2:
        val |= (0x1 << CMD_XFR_TYP_RSPTYP_SHF);
        val |= CMD_XFR_TYP_CCCEN;
        break;
    case MMC_RSP_TYPE_R3:
    case MMC_RSP_TYPE_R4:
        val |= (0x2 << CMD_XFR_TYP_RSPTYP_SHF);
        break;
    case MMC_RSP_TYPE_R1:
    case MMC_RSP_TYPE_R5:
    case MMC_RSP_TYPE_R6:
        val |= (0x2 << CMD_XFR_TYP_RSPTYP_SHF);
        val |= CMD_XFR_TYP_CICEN;
        val |= CMD_XFR_TYP_CCCEN;
        break;
    case MMC_RSP_TYPE_R1b:
    case MMC_RSP_TYPE_R5b:
        val |= (0x3 << CMD_XFR_TYP_RSPTYP_SHF);
        val |= CMD_XFR_TYP_CICEN;
        val |= CMD_XFR_TYP_CCCEN;
        break;
    default:
        break;
    }

    if (cmd->data) {
        val |= CMD_XFR_TYP_DPSEL;
    }

    /* Issue the command. */
    writel(val, host->base + CMD_XFR_TYP);
    return 0;
}



/** Pass control to the devices IRQ handler
 * @param[in] sd_dev  The sdhc interface device that triggered
 *                    the interrupt event.
 */
static int sdhc_handle_irq(sdio_host_dev_t *sdio, int irq UNUSED)
{
    sdhc_dev_t host = sdio_get_sdhc(sdio);
    struct mmc_cmd *cmd = host->cmd_list_head;
    uint32_t int_status;

    int_status = readl(host->base + INT_STATUS);
    if (!cmd) {
        /* Clear flags */
        writel(int_status, host->base + INT_STATUS);
        return 0;
    }
    /** Handle errors **/
    if (int_status & INT_STATUS_TNE) {
        LOG_ERROR("Tuning error");
    }
    if (int_status & INT_STATUS_OVRCURE) {
        LOG_ERROR("Bus overcurrent"); /* (exl. IMX6) */
    }
    if (int_status & INT_STATUS_ERR) {
        LOG_ERROR("CMD/DATA transfer error"); /* (exl. IMX6) */
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_AC12E) {
        LOG_ERROR("Auto CMD12 Error");
        cmd->complete = -1;
    }
    /** DMA errors **/
    if (int_status & INT_STATUS_DMAE) {
        LOG_ERROR("DMA Error");
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_ADMAE) {
        LOG_ERROR("ADMA error");       /*  (exl. IMX6) */
        cmd->complete = -1;
    }
    /** DATA errors **/
    if (int_status & INT_STATUS_DEBE) {
        LOG_ERROR("Data end bit error");
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_DCE) {
        LOG_ERROR("Data CRC error");
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_DTOE) {
        LOG_ERROR("Data transfer error");
        cmd->complete = -1;
    }
    /** CMD errors **/
    if (int_status & INT_STATUS_CIE) {
        LOG_ERROR("Command index error");
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_CEBE) {
        LOG_ERROR("Command end bit error");
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_CCE) {
        LOG_ERROR("Command CRC error");
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_CTOE) {
        LOG_ERROR("CMD Timeout...");
        cmd->complete = -1;
    }

    if (int_status & INT_STATUS_TP) {
        LOG_INFO("Tuning pass");
    }
    if (int_status & INT_STATUS_RTE) {
        LOG_INFO("Retuning event");
    }
    if (int_status & INT_STATUS_CINT) {
        LOG_INFO("Card interrupt");
    }
    if (int_status & INT_STATUS_CRM) {
        LOG_INFO("Card removal");
        cmd->complete = -1;
    }
    if (int_status & INT_STATUS_CINS) {
        LOG_INFO("Card insertion");
    }
    if (int_status & INT_STATUS_DINT) {
        LOG_INFO("DMA interrupt");
    }
    if (int_status & INT_STATUS_BGE) {
        LOG_INFO("Block gap event");
    }

    /* Command complete */
    if (int_status & INT_STATUS_CC) {
        /* Command complete */
        switch (cmd->rsp_type) {
        case MMC_RSP_TYPE_R2:
            cmd->response[0] = readl(host->base + CMD_RSP0);
            cmd->response[1] = readl(host->base + CMD_RSP1);
            cmd->response[2] = readl(host->base + CMD_RSP2);
            cmd->response[3] = readl(host->base + CMD_RSP3);
            break;
        case MMC_RSP_TYPE_R1b:
            if (cmd->index == MMC_STOP_TRANSMISSION) {
                cmd->response[3] = readl(host->base + CMD_RSP3);
            } else {
                cmd->response[0] = readl(host->base + CMD_RSP0);
            }
            break;
        case MMC_RSP_TYPE_NONE:
            break;
        default:
            cmd->response[0] = readl(host->base + CMD_RSP0);
        }

        /* If there is no data segment, the transfer is complete */
        if (cmd->data == NULL) {
            assert(cmd->complete == 0);
            cmd->complete = 1;
        }
    }
    /* DATA: Programmed IO handling */
    if (int_status & (INT_STATUS_BRR | INT_STATUS_BWR)) {
        volatile uint32_t *io_buf;
        uint32_t *usr_buf;
        assert(cmd->data);
        assert(cmd->data->vbuf);
        assert(cmd->complete == 0);
        if (host->blocks_remaining) {
            io_buf = (volatile uint32_t *)((void *)host->base + DATA_BUFF_ACC_PORT);
            usr_buf = (uint32_t *)cmd->data->vbuf;
            if (int_status & INT_STATUS_BRR) {
                /* Buffer Read Ready */
                int i;
                for (i = 0; i < cmd->data->block_size; i += sizeof(*usr_buf)) {
                    *usr_buf++ = *io_buf;
                }
            } else {
                /* Buffer Write Ready */
                int i;
                for (i = 0; i < cmd->data->block_size; i += sizeof(*usr_buf)) {
                    *io_buf = *usr_buf++;
                }
            }
            host->blocks_remaining--;
        }
    }
    /* Data complete */
    if (int_status & INT_STATUS_TC) {
        assert(cmd->complete == 0);
        cmd->complete = 1;
    }
    /* Clear flags */
    writel(int_status, host->base + INT_STATUS);

    /* If the transaction has finished */
    if (cmd != NULL && cmd->complete != 0) {
        if (cmd->next == NULL) {
            /* Shutdown */
            host->cmd_list_head = NULL;
            host->cmd_list_tail = &host->cmd_list_head;
        } else {
            /* Next */
            host->cmd_list_head = cmd->next;
            sdhc_next_cmd(host);
        }
        cmd->next = NULL;
        /* Send callback if required */
        if (cmd->cb) {
            cmd->cb(sdio, 0, cmd, cmd->token);
        }
    }

    return 0;
}

static int sdhc_is_voltage_compatible(sdio_host_dev_t *sdio, int mv)
{
    uint32_t val;
    sdhc_dev_t host = sdio_get_sdhc(sdio);
    val = readl(host->base + HOST_CTRL_CAP);
    if (mv == 3300 && (val & HOST_CTRL_CAP_VS33)) {
        return 1;
    } else {
        return 0;
    }
}

static int sdhc_send_cmd(sdio_host_dev_t *sdio, struct mmc_cmd *cmd, sdio_cb cb, void *token)
{
    sdhc_dev_t host = sdio_get_sdhc(sdio);
    int ret;

    /* Initialise callbacks */
    cmd->complete = 0;
    cmd->next = NULL;
    cmd->cb = cb;
    cmd->token = token;
    /* Append to list */
    *host->cmd_list_tail = cmd;
    host->cmd_list_tail = &cmd->next;

    /* If idle, bump */
    if (host->cmd_list_head == cmd) {
        ret = sdhc_next_cmd(host);
        if (ret) {
            return ret;
        }
    }

    /* finalise the transacton */
    if (cb == NULL) {
        /* Wait for completion */
        while (!cmd->complete) {
            sdhc_handle_irq(sdio, 0);
        }
        /* Return result */
        if (cmd->complete < 0) {
            return cmd->complete;
        } else {
            return 0;
        }
    } else {
        /* Defer to IRQ handler */
        return 0;
    }
}

static void sdhc_enable_clock(volatile void *base_addr)
{
    uint32_t val;

    val = readl(base_addr + SYS_CTRL);
    val |= SYS_CTRL_CLK_INT_EN;
    writel(val, base_addr + SYS_CTRL);

    do {
        val = readl(base_addr + SYS_CTRL);
    } while (!(val & SYS_CTRL_CLK_INT_STABLE));

    val |= SYS_CTRL_CLK_CARD_EN;
    writel(val, base_addr + SYS_CTRL);

    return;
}

/* Set the clock divider and timeout */
static int sdhc_set_clock_div(
    volatile void *base_addr,
    divisor dvs_div,
    sdclk_frequency_select sdclks_div,
    data_timeout_counter_val dtocv)
{
    /* make sure the clock state is stable. */
    if (readl(base_addr + PRES_STATE) & SDHC_PRES_STATE_SDSTB) {
        uint32_t val = readl(base_addr + SYS_CTRL);

        /* The SDCLK bit varies with Data Rate Mode. */
        if (readl(base_addr + MIX_CTRL) & MIX_CTRL_DDR_EN) {
            val &= ~(SYS_CTRL_SDCLKS_MASK << SYS_CTRL_SDCLKS_SHF);
            val |= ((sdclks_div >> 1) << SYS_CTRL_SDCLKS_SHF);

        } else {
            val &= ~(SYS_CTRL_SDCLKS_MASK << SYS_CTRL_SDCLKS_SHF);
            val |= (sdclks_div << SYS_CTRL_SDCLKS_SHF);
        }
        val &= ~(SYS_CTRL_DVS_MASK << SYS_CTRL_DVS_SHF);
        val |= (dvs_div << SYS_CTRL_DVS_SHF);

        /* Set data timeout value */
        val |= (dtocv << SYS_CTRL_DTOCV_SHF);
        writel(val, base_addr + SYS_CTRL);
    } else {
        LOG_ERROR("The clock is unstable, unable to change it!\n");
        return -1;
    }

    return 0;
}

static int sdhc_set_clock(volatile void *base_addr, clock_mode clk_mode)
{
    int rslt = -1;

    const bool isClkEnabled = readl(base_addr + SYS_CTRL) & SYS_CTRL_CLK_INT_EN;
    if (!isClkEnabled) {
        sdhc_enable_clock(base_addr);
    }

    /* TODO: Relate the clock rate settings to the actual capabilities of the
     * card and the host controller. The conservative settings chosen should
     * work with most setups, but this is not an ideal solution. According to
     * the RM, the default freq. of the base clock should be at around 200MHz.
     */
    switch (clk_mode) {
    case CLOCK_INITIAL:
        /* Divide the base clock by 512 */
        rslt = sdhc_set_clock_div(base_addr, DIV_16, PRESCALER_32, SDCLK_TIMES_2_POW_14);
        break;
    case CLOCK_OPERATIONAL:
        /* Divide the base clock by 8 */
        rslt = sdhc_set_clock_div(base_addr, DIV_4, PRESCALER_2, SDCLK_TIMES_2_POW_29);
        break;
    default:
        LOG_ERROR("Unsupported clock mode setting\n");
        rslt = -1;
        break;
    }

    if (rslt < 0) {
        LOG_ERROR("Failed to change the clock settings\n");
    }

    return rslt;
}

/** Software Reset */
static int sdhc_reset(sdio_host_dev_t *sdio)
{
    sdhc_dev_t host = sdio_get_sdhc(sdio);
    uint32_t val;

    /* Reset the host */
    val = readl(host->base + SYS_CTRL);
    val |= SYS_CTRL_RSTA;
    /* Wait until the controller is ready */
    writel(val, host->base + SYS_CTRL);
    do {
        val = readl(host->base + SYS_CTRL);
    } while (val & SYS_CTRL_RSTA);

    /* Enable IRQs */
    val = (INT_STATUS_ADMAE | INT_STATUS_OVRCURE | INT_STATUS_DEBE
           | INT_STATUS_DCE   | INT_STATUS_DTOE    | INT_STATUS_CRM
           | INT_STATUS_CINS  | INT_STATUS_BRR     | INT_STATUS_BWR
           | INT_STATUS_CIE   | INT_STATUS_CEBE    | INT_STATUS_CCE
           | INT_STATUS_CTOE  | INT_STATUS_TC      | INT_STATUS_CC);
    writel(val, host->base + INT_STATUS_EN);
    writel(val, host->base + INT_SIGNAL_EN);

    /* Configure clock for initialization */
    sdhc_set_clock(host->base, CLOCK_INITIAL);

    /* TODO: Select Voltage Level */

    /* Set bus width */
    val = readl(host->base + PROT_CTRL);
    val |= MMC_MODE_4BIT;
    writel(val, host->base + PROT_CTRL);

    /* Wait until the Command and Data Lines are ready. */
    while ((readl(host->base + PRES_STATE) & SDHC_PRES_STATE_CDIHB) ||
           (readl(host->base + PRES_STATE) & SDHC_PRES_STATE_CIHB));

    /* Send 80 clock ticks to card to power up. */
    val = readl(host->base + SYS_CTRL);
    val |= SYS_CTRL_INITA;
    writel(val, host->base + SYS_CTRL);
    while (readl(host->base + SYS_CTRL) & SYS_CTRL_INITA);

    /* Check if a SD card is inserted. */
    val = readl(host->base + PRES_STATE);
    if (val & SDHC_PRES_STATE_CINST) {
        printf("Card Inserted");
        if (!(val & SDHC_PRES_STATE_WPSPL)) {
            printf("(Read Only)");
        }
        printf("...\n");
    } else {
        printf("Card Not Present...\n");
    }

    return 0;
}

static int sdhc_get_nth_irq(sdio_host_dev_t *sdio, int n)
{
    sdhc_dev_t host = sdio_get_sdhc(sdio);
    if (n < 0 || n >= host->nirqs) {
        return -1;
    } else {
        return host->irq_table[n];
    }
}

static uint32_t sdhc_get_present_state_register(sdio_host_dev_t *sdio)
{
    return readl(sdio_get_sdhc(sdio)->base + PRES_STATE);
}

static int sdhc_set_operational(struct sdio_host_dev *sdio)
{
    /*
     * Set the clock to a higher frequency for the operational state.
     *
     * As of now, there are no further checks to validate if the card and the
     * host controller could be driven with a higher rate, therefore the
     * operational clock settings are chosen rather conservative.
     */
    sdhc_dev_t host = sdio_get_sdhc(sdio);
    return sdhc_set_clock(host->base, CLOCK_OPERATIONAL);
}

int sdhc_init(void *iobase, const int *irq_table, int nirqs, ps_io_ops_t *io_ops,
              sdio_host_dev_t *dev)
{
    sdhc_dev_t sdhc;
    /* Allocate memory for SDHC structure */
    sdhc = (sdhc_dev_t)malloc(sizeof(*sdhc));
    if (!sdhc) {
        LOG_ERROR("Not enough memory!\n");
        return -1;
    }
    /* Complete the initialisation of the SDHC structure */
    sdhc->base = iobase;
    sdhc->nirqs = nirqs;
    sdhc->irq_table = irq_table;
    sdhc->dalloc = &io_ops->dma_manager;
    sdhc->cmd_list_head = NULL;
    sdhc->cmd_list_tail = &sdhc->cmd_list_head;
    sdhc->version = ((readl(sdhc->base + HOST_VERSION) >> 16) & 0xff) + 1;
    printf("SDHC version %d.00\n", sdhc->version);
    /* Initialise SDIO structure */
    dev->handle_irq = &sdhc_handle_irq;
    dev->nth_irq = &sdhc_get_nth_irq;
    dev->send_command = &sdhc_send_cmd;
    dev->is_voltage_compatible = &sdhc_is_voltage_compatible;
    dev->reset = &sdhc_reset;
    dev->set_operational = &sdhc_set_operational;
    dev->get_present_state = &sdhc_get_present_state_register;
    dev->priv = sdhc;
    /* Clear IRQs */
    writel(0, sdhc->base + INT_STATUS_EN);
    writel(0, sdhc->base + INT_SIGNAL_EN);
    writel(readl(sdhc->base + INT_STATUS), sdhc->base + INT_STATUS);
    return 0;
}


