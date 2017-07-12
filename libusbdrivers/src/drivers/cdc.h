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
#ifndef _DRIVERS_CDC_H_
#define _DRIVERS_CDC_H_

#include <usb/drivers/cdc.h>

/* Management Element Request Code (CDC spec 6.2) */
enum cdc_req_code {
	SEND_ENCAPSULATED_COMMAND       = 0x0, //CDC 6.2.1
	GET_ENCAPSULATED_RESPONSE       = 0x1, //CDC 6.2.2
	SET_COMM_FEATURE                = 0x2, //PSTN
	GET_COMM_FEATURE                = 0x3, //PSTN
	CLEAR_COMM_FEATURE              = 0x4, //PSTN
	RESET_FUNCTION                  = 0x5, //MBIM
	/* RESERVED (future use) 0x6h-0Fh */
	SET_AUX_LINE_STATE              = 0x10, //PSTN
	SET_HOOK_STATE                  = 0x11, //PSTN
	PULSE_SETUP                     = 0x12, //PSTN
	SEND_PULSE                      = 0x13, //PSTN
	SET_PULSE_TIME                  = 0x14, //PSTN
	RING_AUX_JACK                   = 0x15, //PSTN
	/* RESERVED (future use) 16h-1Fh */
	SET_LINE_CODING                 = 0x20, //PSTN
	GET_LINE_CODING                 = 0x21, //PSTN
	SET_CONTROL_LINE_STATE          = 0x22, //PSTN
	SEND_BREAK                      = 0x23, //PSTN
	/* RESERVED (future use) 24h-2Fh */
	SET_RINGER_PARMS                = 0x30, //PSTN
	GET_RINGER_PARMS                = 0x31, //PSTN
	SET_OPERATION_PARMS             = 0x32, //PSTN
	GET_OPERATION_PARMS             = 0x33, //PSTN
	SET_LINE_PARMS                  = 0x34, //PSTN

	GET_LINE_PARMS                  = 0x35, //PSTN
	DIAL_DIGITS                     = 0x36, //PSTN
	SET_UNIT_PARAMETER              = 0x37, //ISDN
	GET_UNIT_PARAMETER              = 0x38, //ISDN
	CLEAR_UNIT_PARAMETER            = 0x39, //ISDN
	GET_PROFILE                     = 0x3A, //ISDN
	/* RESERVED (future use) 3Bh-3Fh */
	SET_ETHERNET_MULTICAST_FILTERS  = 0x40, //ECM
	SET_ETHERNET_POWER_MANAGEMENT_PATTERN_FILTER = 0x41, //ECM
	GET_ETHERNET_POWER_MANAGEMENT_PATTERN_FILTER = 0x42, //ECM
	SET_ETHERNET_PACKET_FILTER      = 0x43, //ECM
	GET_ETHERNET_STATISTIC          = 0x44, //ECM
	/* RESERVED (future use) 45h-4Fh */
	SET_ATM_DATA_FORMAT             = 0x50, //ATM
	GET_ATM_DEVICE_STATISTICS       = 0x51, //ATM
	SET_ATM_DEFAULT_VC              = 0x52, //ATM
	GET_ATM_VC_STATISTICS           = 0x53, //ATM
	/* RESERVED (future use) 54h-5Fh */
	/* MDLM Semantic-Model specific Requests 60h – 7Fh */
	GET_NTB_PARAMETERS              = 0x80, //NCM
	GET_NET_ADDRESS                 = 0x81, //NCM
	SET_NET_ADDRESS                 = 0x82, //NCM
	GET_NTB_FORMAT                  = 0x83, //NCM
	SET_NTB_FORMAT                  = 0x84, //NCM
	GET_NTB_INPUT_SIZE              = 0x85, //NCM
	SET_NTB_INPUT_SIZE              = 0x86, //NCM
	GET_MAX_DATAGRAM_SIZE           = 0x87, //NCM
	SET_MAX_DATAGRAM_SIZE           = 0x88, //NCM
	GET_CRC_MODE                    = 0x89, //NCM
	SET_CRC_MODE                    = 0x8A  //NCM
	/* RESERVED (future use) 8Bh-FFh */
};

/* Management Element Notification (CDC spec 6.3) */
#define CDC_NOTIFY_NETWORK_CONNECTION       0x0
#define CDC_NOTIFY_RESPONSE_AVALIABLE       0x1
#define CDC_NOTIFY_AUX_JACK_HOOL_STATE      0x8
#define CDC_NOTIFY_RING_DETECT              0x9
#define CDC_NOTIFY_SERIAL_STATE             0x20
#define CDC_NOTIFY_CALL_STATE_CHANGE        0x28
#define CDC_NOTIFY_LINE_STATE_CHANGE        0x29
#define CDC_NOTIFY_CONNECTION_SPEED_CHANGE  0x2A

struct func_desc {
    uint8_t bFunctionLength;
    uint8_t bDescriptorType;
    uint8_t bDescriptorSubtype;
} __attribute__ ((packed));

struct header_desc {
    uint8_t bFunctionLength;
    uint8_t bDescriptorType;
    uint8_t bDescriptorSubtype;
    uint16_t bcdCDC;
} __attribute__ ((packed));

struct union_desc {
    uint8_t bFunctionLength;
    uint8_t bDescriptorType;
    uint8_t bDescriptorSubtype;
    uint8_t bControlInterface;
    uint8_t bSubordinateInterface[0];
} __attribute__ ((packed));

#endif /* _DRIVERS_CDC_H_ */

