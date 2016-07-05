/*
 * Copyright 2015, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

/* Operation Code */
#define TEST_UNIT_READY    0x00
#define REZERO_UNIT        0x01
#define REQUEST_SENSE      0x03
#define FORMAT_UNIT        0x04

#define INQUIRY            0x12
#define START_STOP         0x1B
#define SEND_DIAGNOSTIC    0x1D
#define ALLOW_REMOVAL      0x1E

#define READ_FORMAT_CAP    0x23
#define READ_CAPACITY      0x25
#define READ_10            0x28
#define WRITE_10           0x2A
#define SEEK               0x2B
#define WRITE_VERIFY       0x2E
#define VERIFY             0x2F

#define MODE_SELECT        0x55
#define MODE_SENSE         0x5A
#define READ_12            0xA8
#define WRITE_12           0xAA

int ufi_init_disk(usb_dev_t udev);
