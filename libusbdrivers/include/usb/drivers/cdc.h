/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _USB_CDC_H_
#define _USB_CDC_H_

#include <usb/usb.h>

/* PSTN - Abstract Control Model (PSTN spec 6.2.2) */
#define ACM_CTRL_DTR  0x1
#define ACM_CTRL_RTS  0x2

#define ACM_STOP_1BIT   0
#define ACM_STOP_15BIT  1
#define ACM_STOP_2BIT   2

#define ACM_PARITY_NONE   0
#define ACM_PARITY_ODD    1
#define ACM_PARITY_EVEN   2
#define ACM_PARITY_MARK   3
#define ACM_PARITY_SPACE  4

enum acm_comm_feature {
	ABSTRACT_STATE  = 1,
	COUNTRY_SETTING = 2
};

struct acm_line_coding {
	uint32_t dwDTERate;
	uint8_t  bCharFormat;
	uint8_t  bParityType;
	uint8_t  bDataBits;
} __attribute__((packed));

int usb_cdc_bind(usb_dev_t *usb_dev);

/* Communication Device Class Requests */
void cdc_send_encap_cmd(usb_dev_t *udev, const void *buf, int len);
void cdc_get_encap_resp(usb_dev_t *udev, void *buf, int len);

void acm_set_comm_feature(usb_dev_t *udev, enum acm_comm_feature f, uint16_t state);
uint16_t acm_get_comm_feature(usb_dev_t *udev, enum acm_comm_feature f);
void acm_clear_comm_feature(usb_dev_t *udev, enum acm_comm_feature f);
void acm_set_line_coding(usb_dev_t *udev, const struct acm_line_coding *coding);
void acm_get_line_coding(usb_dev_t *udev, struct acm_line_coding *coding);
void acm_set_ctrl_line_state(usb_dev_t *udev, uint8_t ctrl);
void acm_send_break(usb_dev_t *udev, uint16_t us);

/* Data interface functions */
int usb_cdc_read(usb_dev_t *udev, void *buf, int len);
int usb_cdc_write(usb_dev_t *udev, const void *buf, int len);

#endif /* _USB_CDC_H_ */

