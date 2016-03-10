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
#define REQUEST_SENSE      0x03
#define READ_6             0x08
#define WRITE_6            0x0A
#define INQUIRY            0x12
#define MODE_SELECT_6      0x15
#define MODE_SENSE_6       0x1A
#define SEND_DIAGNOSTIC    0x1D

#define READ_CAPACITY_10   0x25
#define READ_10            0x28
#define WRITE_10           0x2A

#define LOG_SELECT         0x4C
#define LOG_SENSE          0x4D
#define MODE_SELECT_10     0x55
#define MODE_SENSE_10      0x5A

#define READ_CAPACITY_16   0x9E
#define READ_12            0xA8
#define WRITE_12           0xAA

int scsi_init_disk(usb_dev_t udev);
