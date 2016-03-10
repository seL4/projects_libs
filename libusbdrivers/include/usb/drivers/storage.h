/*
 * Copyright 2015, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#ifndef _USB_STORAGE_H_
#define _USB_STORAGE_H_

#include <sync/mutex.h>
#include <usb/usb.h>

int usb_storage_bind(usb_dev_t usb_dev, sync_mutex_t *mutex);

#endif /* _USB_STORAGE_H_ */

