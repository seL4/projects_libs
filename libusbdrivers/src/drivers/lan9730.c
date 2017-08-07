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

/**
 * @brief Microchip LAN9730 USB to 10/100 Ethernet adaptor
 * @see https://www.microchip.com/wwwproducts/en/LAN9730
 */
#include <autoconf.h>
#ifdef CONFIG_LIB_LWIP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <usb/drivers/lan9730.h>

#include <netif/etharp.h>
#include <lwip/stats.h>

#include "../services.h"

#define COL_RX  "\e[42;30m"
#define COL_TX  "\e[43;30m"
#define COL_DEF "\e[0;0m"

//#define ETH_TRAFFIC_DEBUG
#define ETH_ENABLE_IRQS

#define INT_MACRTO   BIT(19)
#define INT_RXFIFO   BIT(18)
#define INT_TXSTOP   BIT(17)
#define INT_RXSTOP   BIT(16)
#define INT_PHY      BIT(15)
#define INT_TXE      BIT(14)
#define INT_TDFU     BIT(13)
#define INT_TDF0     BIT(12)
#define INT_RXDF     BIT(11)
#define INT_GPIO(x)  (((x) & MASK(10)) << 0)

/* SCSR (System Control and Status Registers) */
#define REG_ID_REV          0x000
#define REG_INT_STS         0x008
#define REG_RX_CFG          0x00C
#define REG_TX_CFG          0x010
#define REG_HW_CFG          0x014
#define REG_RX_FIFO_INF     0x018
#define REG_TX_FIFO_INF     0x01C
#define REG_PMT_CTL         0x020
#define REG_LED_GPIO_CFG    0x024
#define REG_GPIO_CFG        0x028
#define REG_AFC_CFG         0x02C
#define REG_E2P_CMD         0x030
#define REG_E2P_DATA        0x034
#define REG_BURST_CAP       0x038
#define REG_DP_SEL          0x040
#define REG_DP_CMD          0x044
#define REG_DP_ADDR         0x048
#define REG_DP_DATA0        0x04C
#define REG_DP_DATA1        0x050
#define REG_GPIO_WAKE       0x064
#define REG_INT_EP_CTL      0x068
#define REG_BULK_IN_DLY     0x06C
#define REG_DBG_RX_FIFO_LVL 0x070
#define REG_DBG_RX_FIFO_PTR 0x074
#define REG_DBG_TX_FIFO_LVL 0x078
#define REG_DBG_TX_FIFO_PTR 0x07C
#define REG_HS_ATTR         0x0A0
#define REG_FS_ATTR         0x0A4
#define REG_STRNG_ATTR0     0x0A8
#define REG_STRNG_ATTR1     0x0AC
#define REG_FLAG_ATTR       0x0B0
/* MCSR (MAC Control and Status Registers */
#define REG_MAC_CR          0x100
#define REG_ADDRH           0x104
#define REG_ADDRL           0x108
#define REG_HASHH           0x10C
#define REG_HASHL           0x110
#define REG_MII_ACCESS      0x114
#define REG_MII_DATA        0x118
#define REG_FLOW            0x11C
#define REG_VLAN1           0x120
#define REG_VLAN2           0x124
#define REG_WUFF            0x128
#define REG_WUCSR           0x12C
#define REG_COE_CR          0x130

/**** PHY registers ****/
/* IEEE Defined Registers */
#define PHYREG_BASIC_CONTROL                          0
#define PHYREG_BASIC_STATUS                           1
#define PHYREG_PHY_ID1                                2
#define PHYREG_PHY_ID2                                3
#define PHYREG_AUTONEG_ADVERTISEMENT                  4
#define PHYREG_AUTONEG_LINK_PARTNER_ABILITY           5
#define PHYREG_AUTONEG_EXPANSION                      6
#define PHYREG_AUTONEG_NEXT_PAGE                      7
#define PHYREG_AUTONEG_LINK_PARTNER_NEXT_PAGE_ABILITY 8
#define PHYREG_1000BASET_CONTROL                      9
#define PHYREG_1000BASET_STATUS                      10
#define PHYREG_EXTENDED_CONTROL                      11
#define PHYREG_EXTENDED_DATA_WRITE                   12
#define PHYREG_EXTENDED_DATA_READ                    13
#define PHYREG_EXTENDED_STATUS                       15

/*** KSZ9021 ***/
#if 1
/* Vendor Specific Registers */
#define PHYREG_REMOTE_LOOPBACK_LED_MODE              17
#define PHYREG_LINKMD_CABLE_DIAGNOSTIC               18
#define PHYREG_DIGITAL_PMA_PCS_STATUS                19
#define PHYREG_RXER_COUNTER                          21
#define PHYREG_INTERRUPT_CONTROL_STATUS              27
#define PHYREG_DIGITAL_DEBUG_CONTROL1                28
#define PHYREG_PHY_CONTROL                           31

/* Extended Registers */
#define PHYREG_COMMON_CONTROL                       256
#define PHYREG_STRAP_STATUS                         257
#define PHYREG_OPERATION_MODE_STRAP_OVERRIDE        258
#define PHYREG_OPERATION_MODE_STRAP_STATUS          259
#define PHYREG_RGMII_CLOCK_AND_CONTROL_PAD_SKEW     260
#define PHYREG_RGMII_RX_DATA_PAD_SKEW               261
#define PHYREG_ANALOG_TEST_REGISTER                 263
#endif

/* Basic control */
#define PHYBC_SOFT_RESET        BIT(15)
#define PHYBC_LOOPBACK          BIT(14)
#define PHYBC_SPEED_100         BIT(13)
#define PHYBC_AUTONEG_EN        BIT(12)
#define PHYBC_POWER_DOWN        BIT(11)
#define PHYBC_AUTONEG_RESTART   BIT( 9)
#define PHYBC_FULLDUPLEX        BIT( 8)
#define PHYBC_COLLISION_TST     BIT( 7)
/* Status */
#define PHYBS_100BASE_T4        BIT(15)
#define PHYBS_100BASE_TX_FULL   BIT(14)
#define PHYBS_100BASE_TX_HALF   BIT(13)
#define PHYBS_10BASE_TX_FULL    BIT(12)
#define PHYBS_10BASE_TX_HALF    BIT(11)
#define PHYBS_AUTONEG_DONE      BIT( 5)
#define PHYBS_REMOTE_FAULT      BIT( 4)
#define PHYBS_AUTONEG_AVAIL     BIT( 3)
#define PHYBS_LINKUP            BIT( 2)
#define PHYBS_JABBER            BIT( 1)
#define PHYBS_EXTCAP            BIT( 0)
/* Auto neg AD */
#define PHYAN_AD_REMOTE_FAULT   BIT(13)
#define PHYAN_AD_PAUSE_NONE     (0x0 * BIT(10))
#define PHYAN_AD_PAUSE_SYM      (0x1 * BIT(10))
#define PHYAN_AD_PAUSE_ASYM     (0x2 * BIT(10))
#define PHYAN_AD_PAUSE_BOTH     (0x3 * BIT(10))
#define PHYAN_AD_PAUSE_MASK     PHYAN_AD_PAUSE_BOTH
#define PHYAN_AD_100BASE_FULL   BIT(8)
#define PHYAN_AD_100BASE_HALF   BIT(7)
#define PHYAN_AD_10BASE_FULL    BIT(6)
#define PHYAN_AD_10BASE_HALF    BIT(5)
/* Link partner ability */
#define PHYAN_ADLP_NEXT_PAGE    BIT(15)
#define PHYAN_ADLP_ACK          BIT(14)
#define PHYAN_ADLP_REMOTE_FAULT BIT(13)
#define PHYAN_ADLP_PAUSE_NONE   (0x0 * BIT(10))
#define PHYAN_ADLP_PAUSE_SYM    (0x1 * BIT(10))
#define PHYAN_ADLP_PAUSE_ASYM   (0x2 * BIT(10))
#define PHYAN_ADLP_PAUSE_BOTH   (0x3 * BIT(10))
#define PHYAN_ADLP_PAUSE_MASK   PHYAN_AD_PAUSE_BOTH
#define PHYAN_ADLP_100BASE_T4   BIT(9)
#define PHYAN_ADLP_100BASE_FULL BIT(8)
#define PHYAN_ADLP_100BASE_HALF BIT(7)
#define PHYAN_ADLP_10BASE_FULL  BIT(6)
#define PHYAN_ADLP_10BASE_HALF  BIT(5)

/***********************/

#define MACCR_FULL_DUPLEX BIT(20)
#define MACCR_TXEN        BIT( 3)
#define MACCR_RXEN        BIT( 2)

#define FEAT_REMOTE_WAKEUP     0x00
#define FEAT_EPSTALL           0x02
#define STAT_REMOTE_WAKEUP     BIT(1)
#define STAT_SELF_POWERED      BIT(0)
#define STAT_BULK_STALL        BIT(0)

/* Bitfields for RX usb packet header */
#define RXSTAT_FILTERING_FAIL      BIT(30)
#define RXSTAT_FRAME_LENGTH(x)     ((x) << 16)
#define RXSTAT_FRAME_LENGTH_MASK   FRAME_LENGTH(0x3fff)
#define RXSTAT_GET_FRAME_LENGTH(x) (((x) & RXSTAT_FRAME_LENGTH_MASK) >> 16)
#define RXSTAT_ERROR_STATUS        BIT(15)
#define RXSTAT_BROADCAST_FRAME     BIT(13)
#define RXSTAT_LENGTH_ERROR        BIT(12)
#define RXSTAT_RUNT_FRAME          BIT(11)
#define RXSTAT_MULTICAST_FRAME     BIT(10)
#define RXSTAT_FRAME_TOO_LONG      BIT( 7)
#define RXSTAT_COLLISION_SEEN      BIT( 6)
#define RXSTAT_FRAME_TYPE          BIT( 5)
#define RXSTAT_RECEIVE_WDG_TO      BIT( 4)
#define RXSTAT_MII_ERROR           BIT( 3)
#define RXSTAT_DRIBBLING_BIT       BIT( 2)
#define RXSTAT_CRC_ERROR           BIT( 1)

/* Bitfields for TX and RX usb packet header */
#define TXCMDA_DATA_START_OFFSET(x)   ((x) << 16)
#define TXCMDA_DATA_START_OFFSET_MASK DATA_START_OFFSET(0x3)
#define TXCMDA_FIRST_SEGMENT          BIT(13)
#define TXCMDA_LAST_SEGMENT           BIT(12)
#define TXCMDA_BUFFER_SIZE(x)         ((x) << 0)
#define TXCMDA_BUFFER_SIZE_MASK       BUFFER_SIZE(0x7ff)

#define TXCMDB_CHECKSUM_ENABLE     BIT(14)
#define TXCMDB_ADD_CRC_DISABLE     BIT(13)
#define TXCMDB_DISABLE_ETH_PADDING BIT(12)
#define TXCMDB_FRAME_LENGTH(x)     ((x) << 0)
#define TXCMDB_FRAME_LENGTH_MASK   TXSTAT_FRAME_LENGTH(0x7ff)

#define RXSTS_FILTERING_FAIL  BIT(30)
#define RXSTS_FRAME_LENGTH(x) (((x) >> 16) & 0x3fff)
#define RXSTS_ERROR_STATUS    BIT(15)
#define RXSTS_BROADCAST_FRAME BIT(13)
#define RXSTS_LENGTH_ERROR    BIT(12)
#define RXSTS_RUNT_FRAME      BIT(11)
#define RXSTS_MULTICAST_FRAME BIT(10)
#define RXSTS_FRAME_TOO_LONG  BIT( 7)
#define RXSTS_COLLISION_SEEN  BIT( 6)
#define RXSTS_FRAME_TYPE      BIT( 5)
#define RXSTS_RX_WDT_TO       BIT( 4)
#define RXSTS_MII_ERROR       BIT( 3)
#define RXSTS_DRIBBLING_BIT   BIT( 2)
#define RXSTS_CRC_ERROR       BIT( 1)

/* Bitfields for interrupt USB packet */
#define ETHINT_MACRTO   BIT(19)
#define ETHINT_RX_FULL  BIT(18)
#define ETHINT_TXSTOP   BIT(17)
#define ETHINT_RXSTOP   BIT(16)
#define ETHINT_PHY      BIT(15)
#define ETHINT_TX_EMPTY BIT(14)
#define ETHINT_TDFU     BIT(13)
#define ETHINT_TDFO     BIT(12)
#define ETHINT_RXDF     BIT(11)
#define ETHINT_GPIO(x)  (((x) >> 0) & 0x7ff)

/* Bitfields for TX/RX configuration */
#define ETHTXCFG_TXON    BIT(2)
#define ETHTXCFG_TXSTOP  BIT(1)
#define ETHTXCFG_TXFLUSH BIT(0)
#define ETHRXCFG_RXFLUSH BIT(0)

struct usb_eth {
	struct usb_dev *udev;
/// Endpoints
	struct endpoint *ep_in;
	struct endpoint *ep_out;
	struct endpoint *ep_int;
/// IRQs
	struct xact reg_read_xact[2];
	struct xact reg_write_xact[2];
	struct xact rx_xact;
#if defined(ETH_ENABLE_IRQS)
	uint32_t *intbm;
	struct xact int_xact;
#endif
};

static inline struct usb_eth *netif_get_eth_driver(struct netif *netif)
{
	return (struct usb_eth *)netif->state;
}

static inline struct usbreq
__clear_ep_feature_req(uint16_t ep, uint16_t feature)
{
	struct usbreq r = {
		.bmRequestType = (USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_OTHER),
		.bRequest = CLR_FEATURE,
		.wValue = feature,
		.wIndex = ep,
		.wLength = 0
	};
	return r;
}

static inline struct usbreq __clear_remote_wakeup(uint16_t ep)
{
	return __clear_ep_feature_req(ep, FEAT_REMOTE_WAKEUP);
}

static inline struct usbreq __clear_epstall(uint16_t ep)
{
	return __clear_ep_feature_req(ep, FEAT_EPSTALL);
}

/* Get the status of an endpoint, or the device if endpoint 0 */
static inline struct usbreq __get_status_req(uint16_t ep)
{
	struct usbreq r = {
		.bmRequestType = (USB_DIR_IN | USB_TYPE_STD | USB_RCPT_DEVICE),
		.bRequest = GET_STATUS,
		.wValue = 0,
		.wIndex = ep,
		.wLength = 2
	};
	if (ep != 0) {
		/* Endpoint request type */
		r.bmRequestType |= USB_RCPT_ENDPOINT;
	}
	if (ep & 0x1) {
		/* Direction */
		r.wIndex |= BIT(7);
	}
	return r;
}

static inline struct usbreq __reg_write_req(uint16_t addr)
{
	struct usbreq r = {
		.bmRequestType = (USB_DIR_OUT | USB_TYPE_VEN | USB_RCPT_DEVICE),
		.bRequest = 0xA0,
		.wValue = 0,
		.wIndex = addr,
		.wLength = 4
	};
	return r;
}

static inline struct usbreq __reg_read_req(uint16_t addr)
{
	struct usbreq r = {
		.bmRequestType = (USB_DIR_IN | USB_TYPE_VEN | USB_RCPT_DEVICE),
		.bRequest = 0xA1,
		.wValue = 0,
		.wIndex = addr,
		.wLength = 4
	};
	return r;
}

static int write_register(struct usb_eth *eth, int addr, uint32_t v)
{
	struct usbreq *r;
	uint32_t *d;
	int err;

	r = xact_get_vaddr(&eth->reg_write_xact[0]);
	d = xact_get_vaddr(&eth->reg_write_xact[1]);
	*r = __reg_write_req(addr);
	*d = v;

	/* FIXME: Find out why the delay is important? */
	msdelay(100);
	err = usbdev_schedule_xact(eth->udev, eth->udev->ep_ctrl,
				   eth->reg_write_xact, 2, NULL, NULL);
	if (err) {
		ZF_LOGF("Transaction error\n");
		abort();
	}
	return err;
}

static int read_register(struct usb_eth *eth, int addr, uint32_t *v)
{
	struct usbreq *r;
	uint32_t *d;
	int err;

	r = xact_get_vaddr(&eth->reg_read_xact[0]);
	d = xact_get_vaddr(&eth->reg_read_xact[1]);
	*r = __reg_read_req(addr);

	/* FIXME: Find out why the delay is important? */
	msdelay(100);
	err = usbdev_schedule_xact(eth->udev, eth->udev->ep_ctrl,
				   eth->reg_read_xact, 2, NULL, NULL);
	if (err) {
		ZF_LOGF("Transaction error\n");
		abort();
	}
	*v = *d;
	return err;
}

static int read_phy_register(struct usb_eth *eth, int addr, uint32_t *v)
{
	uint32_t data;
	int err;
	/* Issue the request */
	data = BIT(11) | (addr << 6) | BIT(0);
	err = write_register(eth, REG_MII_ACCESS, data);;
	if (err) {
		ZF_LOGF("Write PHY error\n");
		abort();
	}
	/* Wait for completion */
	do {
		err = read_register(eth, REG_MII_ACCESS, &data);
		if (err) {
			ZF_LOGF("Read PHY error\n");
			abort();
		}
	} while (data & BIT(0));

	/* Read the data */
	err = read_register(eth, REG_MII_DATA, v);
	if (err) {
		ZF_LOGF("Read PHY error\n");
		abort();
	}
	return err;
}

static int write_phy_register(struct usb_eth *eth, int addr, uint32_t v)
{
	uint32_t data;
	int err;
	/* Setup the data */
	err = write_register(eth, REG_MII_DATA, v);
	if (err) {
		ZF_LOGF("Write PHY error\n");
		abort();
	}
	/* Issue the request */
	data = BIT(11) | (addr << 6) | BIT(1) | BIT(0);
	err = write_register(eth, REG_MII_ACCESS, data);;
	if (err) {
		ZF_LOGF("Write PHY error\n");
		abort();
	}
	/* Wait for completion */
	do {
		err = read_register(eth, REG_MII_ACCESS, &data);
		if (err) {
			ZF_LOGF("Read PHY error\n");
			abort();
		}
	} while (data & BIT(0));
	return 0;
}

#define DUMPREG(eth, r)                                \
    do {                                               \
        uint32_t v;                                    \
        read_register(eth, r, &v);                     \
        printf("(0x%03x)%20s: 0x%08x\n", r, #r, v);    \
    }while(0)

#define DUMPPHYREG(eth, r)                             \
    do {                                               \
        uint32_t v;                                    \
        read_phy_register(eth, r, &v);                 \
        printf("(0x%03x)%20s: 0x%08x\n", r, #r, v);    \
    }while(0)

UNUSED static void dump_pbuf(struct pbuf *p)
{
	struct pbuf *q;
	int i, j;
	printf("##PBUF##");
	for (q = p, i = 0; q != NULL; q = q->next) {
		char *payload = (char *)q->payload;
		for (j = 0; j < q->len; j++, i++) {
			if ((i % 32) == 0) {
				printf("\n0x%03x: ", i);
			}
			printf("%02x", payload[j]);
		}
	}
	printf(COL_DEF "\n");
}

UNUSED static void dump_rxptr(struct usb_eth *eth)
{
	DUMPREG(eth, REG_RX_FIFO_INF);
	DUMPREG(eth, REG_TX_FIFO_INF);
	DUMPREG(eth, REG_TX_CFG);
}

UNUSED static void dump_registers(struct usb_eth *eth)
{
	printf("### SCSR ###\n");
	DUMPREG(eth, REG_ID_REV);
	DUMPREG(eth, REG_INT_STS);
	DUMPREG(eth, REG_RX_CFG);
	DUMPREG(eth, REG_TX_CFG);
	DUMPREG(eth, REG_HW_CFG);
	DUMPREG(eth, REG_RX_FIFO_INF);
	DUMPREG(eth, REG_TX_FIFO_INF);
	DUMPREG(eth, REG_PMT_CTL);
	DUMPREG(eth, REG_LED_GPIO_CFG);
	DUMPREG(eth, REG_GPIO_CFG);
	DUMPREG(eth, REG_AFC_CFG);
	DUMPREG(eth, REG_E2P_CMD);
	DUMPREG(eth, REG_E2P_DATA);
	DUMPREG(eth, REG_BURST_CAP);
	DUMPREG(eth, REG_DP_SEL);
	DUMPREG(eth, REG_DP_CMD);
	DUMPREG(eth, REG_DP_ADDR);
	DUMPREG(eth, REG_DP_DATA0);
	DUMPREG(eth, REG_DP_DATA1);
	DUMPREG(eth, REG_GPIO_WAKE);
	DUMPREG(eth, REG_INT_EP_CTL);
	DUMPREG(eth, REG_BULK_IN_DLY);
	DUMPREG(eth, REG_DBG_RX_FIFO_LVL);
	DUMPREG(eth, REG_DBG_RX_FIFO_PTR);
	DUMPREG(eth, REG_DBG_TX_FIFO_PTR);
	DUMPREG(eth, REG_DBG_TX_FIFO_PTR);
	DUMPREG(eth, REG_HS_ATTR);
	DUMPREG(eth, REG_FS_ATTR);
	DUMPREG(eth, REG_STRNG_ATTR0);
	DUMPREG(eth, REG_STRNG_ATTR1);
	DUMPREG(eth, REG_FLAG_ATTR);
	/* MCSR (MAC Control and Status Registers */
	printf("### MCSR ###\n");
	DUMPREG(eth, REG_MAC_CR);
	DUMPREG(eth, REG_ADDRH);
	DUMPREG(eth, REG_ADDRL);
	DUMPREG(eth, REG_HASHH);
	DUMPREG(eth, REG_HASHL);
	DUMPREG(eth, REG_MII_ACCESS);
	DUMPREG(eth, REG_MII_DATA);
	DUMPREG(eth, REG_FLOW);
	DUMPREG(eth, REG_VLAN1);
	DUMPREG(eth, REG_VLAN2);
	DUMPREG(eth, REG_WUFF);
	DUMPREG(eth, REG_WUCSR);
	DUMPREG(eth, REG_COE_CR);

	printf("### PHY ###\n");
	DUMPPHYREG(eth, PHYREG_BASIC_CONTROL);
	DUMPPHYREG(eth, PHYREG_BASIC_STATUS);
	DUMPPHYREG(eth, PHYREG_PHY_ID1);
	DUMPPHYREG(eth, PHYREG_PHY_ID2);
	DUMPPHYREG(eth, PHYREG_AUTONEG_ADVERTISEMENT);
	DUMPPHYREG(eth, PHYREG_AUTONEG_LINK_PARTNER_ABILITY);
	DUMPPHYREG(eth, PHYREG_AUTONEG_EXPANSION);
	DUMPPHYREG(eth, PHYREG_AUTONEG_NEXT_PAGE);
	DUMPPHYREG(eth, PHYREG_AUTONEG_LINK_PARTNER_NEXT_PAGE_ABILITY);
	DUMPPHYREG(eth, PHYREG_1000BASET_CONTROL);
	DUMPPHYREG(eth, PHYREG_1000BASET_STATUS);
	DUMPPHYREG(eth, PHYREG_EXTENDED_CONTROL);
	DUMPPHYREG(eth, PHYREG_EXTENDED_DATA_WRITE);
	DUMPPHYREG(eth, PHYREG_EXTENDED_DATA_READ);
	DUMPPHYREG(eth, PHYREG_EXTENDED_STATUS);
	printf("##############################\n");
}

static int mac_init(struct usb_eth *eth)
{
	uint64_t mac = 0xe0ae2a751000;
	uint32_t v;
    /*** MAC ***/
	/* Set MAC address */
	v = (mac >> 32) & 0xffff;
	write_register(eth, REG_ADDRH, v);
	v = (mac >> 0) & 0xffffffff;
	write_register(eth, REG_ADDRL, v);
	/* Enable TX/RX full duplex */
	v = MACCR_FULL_DUPLEX | MACCR_TXEN | MACCR_RXEN;
	write_register(eth, REG_MAC_CR, v);

    /*** PHY ***/
	/* Soft reset */
	v = PHYBC_SOFT_RESET;
	write_phy_register(eth, PHYREG_BASIC_CONTROL, 0);
	do {
		read_phy_register(eth, PHYREG_BASIC_CONTROL, &v);
	} while (v & PHYBC_SOFT_RESET);
	/* Advertisement */
	read_phy_register(eth, PHYREG_AUTONEG_ADVERTISEMENT, &v);
	v |= PHYAN_AD_100BASE_FULL | PHYAN_AD_100BASE_HALF
	    | PHYAN_AD_10BASE_FULL | PHYAN_AD_10BASE_HALF;
	write_phy_register(eth, PHYREG_AUTONEG_ADVERTISEMENT, v);
	/* Start auto negotiate */
	v = PHYBC_AUTONEG_EN | PHYBC_AUTONEG_RESTART;
	write_phy_register(eth, PHYREG_BASIC_CONTROL, v);
	do {
		read_phy_register(eth, PHYREG_BASIC_STATUS, &v);
	} while (!(v & PHYBS_AUTONEG_DONE));
	/* Report the link speed */
	printf("USB-Eth: Link ");
	if (v & PHYBS_LINKUP) {
		int speed;
		char *duplex;
		printf("up, ");
		/* Speed */
		if (v & (PHYBS_100BASE_TX_FULL | PHYBS_100BASE_TX_HALF)) {
			speed = 100;
		} else if (v & (PHYBS_10BASE_TX_FULL | PHYBS_10BASE_TX_HALF)) {
			speed = 10;
		} else {
			speed = -1;
		}
		/* Duplex */
		if (v & (PHYBS_100BASE_TX_FULL | PHYBS_10BASE_TX_FULL)) {
			duplex = "full";
		} else if (v & (PHYBS_100BASE_TX_HALF | PHYBS_10BASE_TX_HALF)) {
			duplex = "half";
		} else {
			duplex = "???";
		}
		/* Report */
		if (v & PHYBS_100BASE_T4) {
			printf("100Mbps T4\n");
		} else {
			printf("%dMbps, %s duplex\n", speed, duplex);
		}

	} else {
		printf("down\n");
	}

	/* Clear IRQs */
	v = 0x0;
	write_register(eth, REG_INT_STS, v);
	/* Enable IRQs */
	v = ETHINT_MACRTO | ETHINT_RX_FULL | ETHINT_TXSTOP | ETHINT_RXSTOP
	    | ETHINT_PHY | ETHINT_TX_EMPTY | ETHINT_TDFU | ETHINT_TDFO
	    | ETHINT_RXDF;
	write_register(eth, REG_INT_EP_CTL, v);
	/* TX on */
	v = ETHTXCFG_TXON;
	write_register(eth, REG_TX_CFG, v);
	return 0;
}

static int do_lan9730_input(struct netif *netif, struct xact *xact)
{
	struct eth_hdr *ethhdr;
	struct pbuf *p, *q;
	uint32_t *hdr;
	char *payload;
	uint32_t sts;
	int len;
	hdr = (uint32_t*) xact_get_vaddr(xact);
	payload = (char*)&hdr[1];
	sts = *hdr;
	if (sts & RXSTS_ERROR_STATUS) {
		printf("error frame status 0x%x\n", sts);
		return -1;
	}
	len = RXSTS_FRAME_LENGTH(sts);
	if (len == 0) {
		return 0;
	}

	/* Construct the packet */
	p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
	if (!p) {
		ZF_LOGF("LWIP out of memory\n");
		abort();
	}
	for (q = p; q != NULL; q = q->next) {
		memcpy(q->payload, payload, q->len);
		payload += q->len;
	}

#ifdef ETH_TRAFFIC_DEBUG
	printf("\n" COL_RX "RX packet (%d bytes)\n", p->tot_len);
	payload = (char*)&hdr[1];
	dump_pbuf(p);
	printf(COL_DEF);
#endif

	ethhdr = p->payload;

	switch (htons(ethhdr->type)) {
		/* IP or ARP packet? */
	case ETHTYPE_IP:
	case ETHTYPE_ARP:
#if PPPOE_SUPPORT
		/* PPPoE packet? */
	case ETHTYPE_PPPOEDISC:
	case ETHTYPE_PPPOE:
#endif /* PPPOE_SUPPORT */
		/* full packet send to tcpip_thread to process */
		if (netif->input(p, netif) != ERR_OK) {
			LWIP_DEBUGF(NETIF_DEBUG,
				    ("ethernetif_input: IP input error\n"));
			pbuf_free(p);
			p = NULL;
		}
		break;

	default:
		pbuf_free(p);
		p = NULL;
		break;
	}
	return 0;
}

static int eth_process_status(struct netif *netif, uint32_t status)
{
	struct usb_eth *eth;
	eth = netif_get_eth_driver(netif);
	int err = 0;
	if (status & INT_RXFIFO) {
		err = lan9730_input(netif);
		status &= ~INT_RXFIFO;
	}
	if (status) {
		ZF_LOGD("Unknown USB-ETH IRQ status: 0x%x\n", status);
	}
	return err;
}

#if defined(ETH_ENABLE_IRQS)
static int
eth_irq_handler(void *token, enum usb_xact_status stat, int bytes_remaining)
{
	struct netif *netif = (struct netif*)token;
	struct usb_eth *eth;
	int err;
	int len;

	if (!token) {
		ZF_LOGF("Invalid token\n");
		abort();
	}
	eth = netif_get_eth_driver(netif);
	len = eth->int_xact.len - bytes_remaining;

	/* Check the status */
	if (stat != XACTSTAT_SUCCESS) {
		ZF_LOGD("Received unsuccessful IRQ\n");
		return 1;
	}
	if (len != 4) {
		ZF_LOGD("Unexpected number of bytes for INT packet (%d)\n", len);
		return 1;
	}

	ZF_LOGD("Handling IRQ\n");
	err = eth_process_status(netif, *eth->intbm);
	if (err) {
		ZF_LOGD("Spurious IRQ\n");
	}

	usbdev_schedule_xact(eth->udev, eth->ep_int, &eth->int_xact, 1,
			     &eth_irq_handler, netif);

	return 1;
}
#endif

static err_t tx_packet(struct usb_eth *eth, struct pbuf *p)
{
	struct xact xact[2];
	struct pbuf *q;
	uint32_t *hdr;
	char *payload;
	int err;

#ifdef ETH_TRAFFIC_DEBUG
	printf("\n" COL_TX "TX packet (%d bytes)\n", p->tot_len);
	dump_pbuf(p);
	printf(COL_DEF);
#endif
	/* Allocate the buffers */
	xact[0].type = PID_OUT;
	xact[0].len = p->tot_len + sizeof(*hdr) * 2;
	/* TODO: This forces a data toggle. It is needed because we do not store
	 * the DATAx that the packet should be sent to. */
	xact[1].type = PID_OUT;
	xact[1].len = 0;
	err = usb_alloc_xact(eth->udev->dman, xact, 2);
	if (err) {
		return ENOMEM;
	}
	hdr = (uint32_t *) xact_get_vaddr(&xact[0]);
	payload = (char *)&hdr[2];
	/* Construct the packet */
	hdr[0] = TXCMDA_DATA_START_OFFSET(0) | TXCMDA_BUFFER_SIZE(p->tot_len);
	hdr[0] |= TXCMDA_FIRST_SEGMENT | TXCMDA_LAST_SEGMENT;
	hdr[1] = TXCMDB_FRAME_LENGTH(p->tot_len);
	hdr[1] |= TXCMDB_ADD_CRC_DISABLE;
	for (q = p; q != NULL; q = q->next) {
		memcpy(payload, q->payload, q->len);
		payload += q->len;
	}
	/* send it */
	err = usbdev_schedule_xact(eth->udev, eth->ep_out, xact, 2, NULL, NULL);
	if (err) {
		ZF_LOGF("Transaction error\n");
		abort();
	}

	return err;
}

int lan9730_input(struct netif *netif)
{
	struct usb_eth *eth;
	struct xact xact;
	int err;

	eth = netif_get_eth_driver(netif);

	/* Allocate a buffer */
	xact.type = PID_IN;
	xact.len = eth->ep_in->max_pkt;
	err = usb_alloc_xact(eth->udev->dman, &xact, 1);
	if (err) {
		return -1;
	}
	/* Read in a frame */
	err = usbdev_schedule_xact(eth->udev, eth->ep_in, &xact, 1, NULL, NULL);
	if (err) {
		ZF_LOGF("Transaction error\n");
		abort();
	}
	if (err <= 0) {
		/* Nothing there. Return */
		err = 0;
	} else {
		/* Forward the usb packet */
		err = do_lan9730_input(netif, &xact);
	}
	/* Clean up */
	usb_destroy_xact(eth->udev->dman, &xact, 1);
	return err;
}

err_t lan9730_linkoutput(struct netif *netif, struct pbuf *p)
{
	struct usb_eth *usb_eth = netif_get_eth_driver(netif);

	err_t ret = 0;

#if ETH_PAD_SIZE
	pbuf_header(p, -ETH_PAD_SIZE);	/* drop the padding word */
#endif

	ret = tx_packet(usb_eth, p);
	pbuf_ref(p);

#if ETH_PAD_SIZE
	pbuf_header(p, ETH_PAD_SIZE);	/* reclaim the padding word */
#endif

	LINK_STATS_INC(link.xmit);

	return ret;
}

err_t lan9730_init(struct netif *netif)
{
	struct usb_eth *usb_eth;
	if (netif->state == NULL) {
		return ERR_ARG;
	}
	usb_eth = netif_get_eth_driver(netif);
	(void)usb_eth;

	netif->output = etharp_output;
	netif->linkoutput = lan9730_linkoutput;

	NETIF_INIT_SNMP(netif, snmp_ifType_ethernet_csmacd,
			100000000 /* TODO Stop hard-coding Mbps */ );

	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
	    NETIF_FLAG_LINK_UP;
	netif->hwaddr_len = 6;
	netif->mtu = 1500;
	netif->hwaddr[0] = 0x00;
	netif->hwaddr[1] = 0x10;
	netif->hwaddr[2] = 0x75;
	netif->hwaddr[3] = 0x2a;
	netif->hwaddr[4] = 0xae;
	netif->hwaddr[5] = 0xe0;
	return ERR_OK;
}

struct netif *lan9730_driver_bind(usb_dev_t *udev)
{
	struct netif *netif;
	struct usb_eth *eth;
	struct usbreq *req;
	struct xact xact;
	int err;

	if (!is_eth(udev)) {
		return NULL;
	}
	/* Allocate memory */
	eth = (struct usb_eth *)malloc(sizeof(*eth));
	if (eth == NULL) {
		return NULL;
	}
	netif = (struct netif *)malloc(sizeof(*netif));
	if (netif == NULL) {
		free(eth);
		return NULL;
	}
	memset(eth, 0, sizeof(*eth));
	memset(netif, 0, sizeof(*netif));

	eth->udev = udev;
	udev->dev_data = (struct udev_priv *)eth;

	err = usbdev_parse_config(udev, NULL, NULL);
	if (err) {
		ZF_LOGF("Invalid descriptors\n");
	}

	/* Activate configuration */
	ZF_LOGD("Configure ETH\n");
	xact.type = PID_SETUP;
	xact.len = sizeof(*req);
	err = usb_alloc_xact(eth->udev->dman, &xact, 1);
	if (err) {
		ZF_LOGE("Out of DMA memory\n");;
		return NULL;
	}
	req = xact_get_vaddr(&xact);
	*req = __set_configuration_req(1);
	err = usbdev_schedule_xact(udev, udev->ep_ctrl, &xact, 1, NULL, NULL);
	if (err < 0) {
		ZF_LOGE("Transaction error\n");
		return NULL;
	}
	usb_destroy_xact(udev->dman, &xact, 1);

	/* Find endpoints */
	for (int i = 0; udev->ep[i] != NULL; i++) {
		if (udev->ep[i]->type == EP_BULK) {
			if (udev->ep[i]->dir == EP_DIR_OUT) {
				eth->ep_out = udev->ep[i];
			} else {
				eth->ep_in = udev->ep[i];
			}
		} else if (udev->ep[i]->type == EP_INTERRUPT) {
			eth->ep_int = udev->ep[i];
		} else {
			continue;
		}
	}

	/* Cache a transactions buffer for register access */
	eth->reg_read_xact[0].type = PID_SETUP;
	eth->reg_read_xact[0].len = sizeof(struct usbreq);
	eth->reg_read_xact[1].type = PID_IN;
	eth->reg_read_xact[1].len = sizeof(uint32_t);
	eth->reg_write_xact[0].type = PID_SETUP;
	eth->reg_write_xact[0].len = sizeof(struct usbreq);
	eth->reg_write_xact[1].type = PID_OUT;
	eth->reg_write_xact[1].len = sizeof(uint32_t);
	err = usb_alloc_xact(eth->udev->dman, eth->reg_read_xact, 2);
	if (err) {
		return NULL;
	}
	err = usb_alloc_xact(eth->udev->dman, eth->reg_write_xact, 2);
	if (err) {
		return NULL;
	}

	mac_init(eth);

#if defined(ETH_ENABLE_IRQS)
	eth->int_xact.type = PID_IN;
	eth->int_xact.len = eth->ep_int->max_pkt;
	err = usb_alloc_xact(udev->dman, &eth->int_xact, 1);
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
		abort();
	}
	eth->intbm = xact_get_vaddr(&eth->int_xact);
	memset(eth->intbm, 0, eth->int_xact.len);
	ZF_LOGD("Registering for INT (%d ms)\n", eth->ep_int->interval);
	usbdev_schedule_xact(udev, eth->ep_int, &eth->int_xact, 1,
			     &eth_irq_handler, netif);
#else
	eth->intbm = NULL;
	eth->int_xact.vaddr = NULL;
	eth->int_xact.len = 0;
	(void)eth_irq_handler;
#endif
	netif_add(netif, NULL, NULL, NULL, eth, lan9730_init, ethernet_input);

	return netif;
}

int lan9730_poll_status(struct netif *netif)
{
	struct usb_eth *eth;
	struct xact xact[1];
	uint32_t *status;
	int err;

	if (netif->state == NULL) {
		return ERR_ARG;
	}
	eth = netif_get_eth_driver(netif);

	/* Allocate the buffers */
	xact[0].type = PID_IN;
	xact[0].len = 4;
	err = usb_alloc_xact(eth->udev->dman, xact, 1);
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
		abort();
	}
	status = (uint32_t *) xact_get_vaddr(&xact[0]);

	err = usbdev_schedule_xact(eth->udev, eth->ep_int, xact, 1, NULL, NULL);
	if (!err) {
		err = eth_process_status(netif, *status);
	}
	usb_destroy_xact(eth->udev->dman, xact, 1);

	return err;
}

#endif /* CONFIG_LIB_LWIP */
