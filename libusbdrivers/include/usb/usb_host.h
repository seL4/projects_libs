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

/*
 * These functions need to be implemented by platform
 * specific code.
 */

#ifndef __USB_USB_HOST_H_
#define __USB_USB_HOST_H_

#include <platsupport/io.h>
#include <usb/plat/usb.h>

enum usb_speed {
/// 1.5Mbps connection
    USBSPEED_LOW  = 0,
/// 12Mbps connection
    USBSPEED_FULL = 1,
/// 480Mbps connection
    USBSPEED_HIGH = 2
};

/*
 * Endpoint types
 * NOTE: do not change the defined order, see USB 2.0 spec(9.6.6)
 */
enum usb_endpoint_type {
    EP_CONTROL = 0,
    EP_ISOCHRONOUS,
    EP_BULK,
    EP_INTERRUPT
};

/* NOTE: do not change the defined order, see USB 2.0 spec(9.6.6) */
enum usb_endpoint_dir {
    EP_DIR_OUT = 0,
    EP_DIR_IN
};

struct endpoint {
    enum usb_endpoint_type type;
    uint8_t   num;               // Endpoint number
    enum usb_endpoint_dir  dir;  // Endpoint direction
    uint16_t  max_pkt;   // Maximum packet size
    uint8_t   interval;  // Interval for polling or NAK rate for Bulk/Control

    /* For host controller driver only, actually holds queue head. */
    void      *hcpriv;
};

enum usb_xact_type {
/// Input PID
    PID_IN,
/// Output PID
    PID_OUT,
/// Setup PID
    PID_SETUP
};

enum usb_xact_status {
/// The transaction completed successfully
    XACTSTAT_SUCCESS,
/// The transaction has not been processed
    XACTSTAT_PENDING,
/// The transaction was cancelled due to disconnect, etc
    XACTSTAT_CANCELLED,
/// There was an error in processing the transaction
    XACTSTAT_ERROR,
/// The host exhibited a failure during the transaction.
    XACTSTAT_HOSTERROR
};

/* TODO: The xact size has to meet the QTD limitation at the moment */
#define MAX_XACT_SIZE  (5 * PAGE_SIZE_4K)
struct xact {
/// Transfer type
    enum usb_xact_type type;
/// DMA buffer to exchange
    void* vaddr;
    uintptr_t paddr;
/// The length of @ref{buf}
    int len;
};

static inline void* xact_get_vaddr(struct xact* xact)
{
    return xact->vaddr;
}

static inline uintptr_t xact_get_paddr(struct xact* xact)
{
    return xact->paddr;
}

/** Callback type for asynchronous USB transactions
 * @param[in] token  An unmodified opaque token as passed to
 *                   the associated transaction request.
 * @param[in] stat   The status of the transaction.
 * @param[in] rbytes The number of bytes remaining in the transaction.
 *                   This value is generally 0 on successful transmission
 *                   unless a short read or write occurs.
 * @return           1 if the transaction should be rescheduled,
 *                   otherwise, 0.
 */
typedef int (*usb_cb_t)(void* token, enum usb_xact_status stat, int rbytes);


typedef struct sync_ops {
	void *(*sync_init)(int val);
	int (*sync_lock)(void *lock);
	int (*sync_unlock)(void *lock);
	int (*sync_destroy)(void *lock);
} sync_ops_t;

struct usb_host;
typedef struct usb_host usb_host_t;

struct usb_host {
    /// Device ID
    enum usb_host_id id;
    /// Number of ports provided by this host controller
    int nports;

    /// DMA allocator
    ps_dma_man_t* dman;

    /// Synchronization operations
    sync_ops_t* sync;

    /// Submit a transaction for transfer.
    int (*schedule_xact)(usb_host_t* hdev, uint8_t addr, int8_t hub_addr, uint8_t hub_port,
                         enum usb_speed speed, struct endpoint *ep,
                         struct xact* xact, int nxact, usb_cb_t cb, void* t);
    /// Cancel all transactions for a given device endpoint
    int (*cancel_xact)(usb_host_t* hdev, struct endpoint *ep);
    /// Handle an IRQ
    void (*handle_irq)(usb_host_t* hdev);

    /// IRQ numbers tied to this device
    const int* irqs;
    /// Host private data
    struct usb_hc_data* pdata;
};


/**
 * Schedules a USB transaction
 * @param[in] hdev     The host controller that should be used for the transfer
 * @param[in] addr     The destination USB device address
 * @param[in] hub_addr The USB device address of the hub at which the destination
 *                     device is connected. -1 must be used if the device is not
 *                     connected to a hub (i.e. when it is the root hub).
 *                     0 may be used if the device is a SPEED_FULL device.
 * @param[in] hub_port The port at which the destination device is connected to
 *                     its parent hub.
 *                     0 may be used if the device is a SPEED_FULL device.
 * @param[in] speed    The USB speed of the device.
 * @param[in] ep       The destination endpoint of the destination device.
 * @param[in] xact     An array of packet descriptors.
 * @param[in] nxact    The number of packet descriptors in the array.
 * @param[in] cb       A callback function to call on completion.
 *                     NULL will result in blocking operation.
 * @param[in] t        A token to pass, unmodified, to the provided callback
 *                     function on completion.
 * @return             Negative values represent failure, otherwise, the
 *                     number of bytes remaining to be transferred is returned.
 */
static inline int
usb_hcd_schedule(usb_host_t* hdev, uint8_t addr, uint8_t hub_addr, uint8_t hub_port,
                 enum usb_speed speed, struct endpoint *ep,
                 struct xact* xact, int nxact, usb_cb_t cb, void* t)
{
    return hdev->schedule_xact(hdev, addr, hub_addr, hub_port, speed, ep,
                               xact, nxact, cb, t);
}

static inline void
usb_hcd_handle_irq(usb_host_t* hdev)
{
    hdev->handle_irq(hdev);
}

static inline int
usb_hcd_count_ports(usb_host_t* hdev)
{
    return hdev->nports;
}

/**
 * Initialise USB host controller.
 * This function should only be called if you wish to use a raw API for the usb host controller, otherwise,
 * this function will be called by usb_init and the appropriate book keeping for device management
 * will be created and maintained.
 * @param[in]  id     The id of the host controller to initialise
 * @param[in]  ioops  a list of io operation functions.
 *                    of the initialised host controller
 * @param[in]  sync   a list of synchronization operation functions.
 * @param[out] hdev   A host structure to populate. This must
 *                    already be filled with a DMA allocator.
 *                    and the device ID.
 * @return            0 on success
 */
int usb_host_init(enum usb_host_id id, ps_io_ops_t* ioops, sync_ops_t *sync, usb_host_t* hdev);

/** Return a list of IRQ numbers handled by the provided host
 * @param[in]  host   A handle to the USB host device in question
 * @param[out] nirqs  The number of IRQs handled by this host.
 * @return            A NULL terminated list of IRQs
 */
const int* usb_host_irqs(usb_host_t* host, int* nirqs);


#endif /* __USB_USB_HOST_H_ */

