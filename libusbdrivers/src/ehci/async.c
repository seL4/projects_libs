/*
 * Copyright 2016, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <usb/drivers/usbhub.h>
#include "ehci.h"
#include "../services.h"

static inline int
_is_enabled_async(struct ehci_host* edev)
{
    return edev->op_regs->usbsts & EHCISTS_ASYNC_EN;
}

static inline void
_enable_async(struct ehci_host* edev)
{
    edev->op_regs->usbcmd |= EHCICMD_ASYNC_EN;
    while (!_is_enabled_async(edev));
}

static inline void
_disable_async(struct ehci_host* edev)
{
    edev->op_regs->usbcmd &= ~EHCICMD_ASYNC_EN;
    while (_is_enabled_async(edev));
}

enum usb_xact_status
qtd_get_status(volatile struct TD* qtd)
{
    uint32_t t = qtd->token;
    if (t & TDTOK_SACTIVE) {
        /* Note that we have already returned an error code
         * if this TD is still pending due to an error in
         * a previous TD */
        return XACTSTAT_PENDING;

    } else if (t & TDTOK_SHALTED){
        if (t & TDTOK_SXACTERR) {
            return XACTSTAT_ERROR;
        } else if (t & TDTOK_ERROR) {
            return XACTSTAT_HOSTERROR;
        }
        printf("EHCI: Unknown QTD error code 0x%x\n", t);
        return XACTSTAT_HOSTERROR;

    } else {
        return XACTSTAT_SUCCESS;
    }
}

enum usb_xact_status
qhn_get_status(struct QHn * qhn)
{
    int i;
    if (qhn->ntdns) {
        /* If the QHN has not been picked up by the HC yet, the
         * overlay will not be valid. Check the status of the TDs */
        for (i = 0; i < qhn->ntdns; i++) {
            enum usb_xact_status stat;
            stat = qtd_get_status(qhn->tdns[i].td);
            if (stat != XACTSTAT_SUCCESS) {
                return stat;
            }
        }
    }
    /* All TDs complete, return the status of the QH */
    return qtd_get_status(&qhn->qh->td_overlay);
}

static inline int
_qhn_is_active(struct QHn* qhn)
{
    return qhn->qh->td_overlay.token & TDTOK_SACTIVE;
}

static inline int
qhn_get_bytes_remaining(struct QHn *qhn)
{
    int sum = 0;
    int i;

    for (i = 0; i < qhn->ntdns; i++) {
       sum += TDTOK_GET_BYTES(qhn->tdns[i].td->token);
    }

    return sum;
}

int
qhn_cb(struct QHn *qhn, enum usb_xact_status stat)
{
    return qhn->cb(qhn->token, stat, qhn_get_bytes_remaining(qhn));
}

/****************************
 **** Queue manipulation ****
 ****************************/

int
td_set_buf(volatile struct TD* td, uintptr_t buf, int len)
{
    int i = 0;
    usb_assert(td);
    /* Reset the length field in the TD */
    td->token &= ~(TDTOK_BYTES_MASK | TDTOK_C_PAGE_MASK);
    /* Fill the buffers */
    if (len && buf) {
        uintptr_t buf_end = buf + len;
        /* Write the first buffer if we are not page aligned */
        if (buf & 0xfff) {
            td->buf[i++] = buf;
            buf = (buf & ~0xfff) + 0x1000;
        }
        /* Write subsequent pages */
        while (buf < buf_end) {
            if (i >= sizeof(td->buf) / sizeof(*td->buf)) {
                DBG_MEM("Out of TD buffer fields\n");
                return -1;
            } else {
                td->buf[i++] = buf;
                buf += 0x1000;
            }
        }
        td->token |= TDTOK_BYTES(len);
    }
    /* Clear remaining buffers */
    while (i < sizeof(td->buf) / sizeof(*td->buf)) {
        td->buf[i++] = 0;
    }
    /* Done! */
    return 0;
}

struct QHn*
qhn_new(struct ehci_host* edev, uint8_t address, uint8_t hub_addr,
        uint8_t hub_port, enum usb_speed speed, int ep, int max_pkt,
        int dt, struct xact* xact, int nxact, usb_cb_t cb, void* token) {
    struct QHn *qhn;
    volatile struct QH* qh;
    volatile struct TD* prev_td;
    int i;

    assert(nxact >= 1);

    /* Allocate book keeping node */
    qhn = (struct QHn*)malloc(sizeof(*qhn));
    assert(qhn);
    qhn->ntdns = nxact;
    qhn->rate = 0;
    qhn->cb = cb;
    qhn->token = token;
    qhn->irq_pending = 0;
    qhn->was_cancelled = 0;
    qhn->owner_addr = address;
    qhn->next = NULL;
    /* Allocate QHead */
    qhn->qh = ps_dma_alloc_pinned(edev->dman, sizeof(*qh), 32, 0, PS_MEM_NORMAL, &qhn->pqh);
    assert(qhn->qh);
    qh = qhn->qh;
    /** Initialise QH **/
    qh->qhlptr = QHLP_INVALID;
    /* epc0 */
    switch (speed) {
    case USBSPEED_HIGH:
        qh->epc[0] = QHEPC0_HSPEED;
        break;
    case USBSPEED_FULL:
        qh->epc[0] = QHEPC0_FSPEED;
        break;
    case USBSPEED_LOW :
        qh->epc[0] = QHEPC0_LSPEED;
        break;
    default:
        usb_assert(0);
    }
    qh->epc[0] |= QHEPC0_MAXPKTLEN(max_pkt) | QHEPC0_ADDR(address) |
                  QHEPC0_EP(ep) | QHEPC0_NAKCNT_RL(0x8);
    qh->epc[0] |= QHEPC0_DTC;
    if (xact[0].type == PID_SETUP && speed != USBSPEED_HIGH) {
        qh->epc[0] |= QHEPC0_C;
    }
    /* epc1 */
    qh->epc[1] = QHEPC1_HUB_ADDR(hub_addr) | QHEPC1_PORT(hub_port) | QHEPC1_MULT(1);
    /* TD overlay */
    qh->td_cur = TDLP_INVALID;

    qh->td_overlay.token = 0;
    qh->td_overlay.next = TDLP_INVALID;
    qh->td_overlay.alt = TDLP_INVALID;

    /* Initialise all TDs */
    qhn->tdns = malloc(sizeof(*qhn->tdns) * qhn->ntdns);
    usb_assert(qhn->tdns);
    prev_td = &qh->td_overlay;
    for (i = 0; i < qhn->ntdns; i++) {
        /* Initialise TD */
        struct TD* td;
        uintptr_t ptd = 0;
        int err;
        td = ps_dma_alloc_pinned(edev->dman, sizeof(*td), 32, 0, PS_MEM_NORMAL, &ptd);
        usb_assert(td);
        td->next = TDLP_INVALID;
        td->alt = TDLP_INVALID;
        td->token = 0;
        err = td_set_buf(td, xact_get_paddr(&xact[i]), xact[i].len);
        usb_assert(!err);
        /* Transfer type */
        switch (xact[i].type) {
        case PID_INT  :
        case PID_IN   :
            td->token = TDTOK_PID_IN   ;
            break;
        case PID_OUT  :
            td->token = TDTOK_PID_OUT  ;
            break;
        case PID_SETUP:
            td->token = TDTOK_PID_SETUP;
            break;
        default:
            usb_assert(0);
        };
        /* Data toggle */
        if (xact[i].type == PID_SETUP) {
            dt = 0;
        }
        if (dt++ & 1 || (xact[0].type == PID_SETUP && i == nxact - 1)) {
            td->token |= TDTOK_DT;
        }
        /* etc */
        td->token |= TDTOK_BYTES(xact[i].len) |
                     TDTOK_C_ERR(3) |
                     TDTOK_SACTIVE;
        qhn->tdns[i].td = td;
        qhn->tdns[i].ptd = ptd;
        qhn->tdns[i].xact = xact[i];
        /* Link the previous TD */
        td->next = TDLP_INVALID;
        prev_td->next = ptd;
        prev_td = td;
    }

    /* Terminate the TD list and add IOC if requested */
    prev_td->token |= (cb) ? TDTOK_IOC : 0;

#if defined(DEBUG_DES)
    dump_qhn(qhn);
#endif
    return qhn;
}

void
qhn_destroy(ps_dma_man_t* dman, struct QHn* qhn)
{
    int i;
#ifdef EHCI_TRAFFIC_DEBUG
    printf("Completed QH:\n");
    dump_qhn(qhn);
#endif
    for (i = 0; i < qhn->ntdns; i++) {
        ps_dma_free_pinned(dman, (void*)qhn->tdns[i].td, sizeof(*qhn->tdns[i].td));
    }
    ps_dma_free_pinned(dman, (void*)qhn->qh, sizeof(*qhn->qh));
    free(qhn->tdns);
    free(qhn);
}

int
clear_async_xact(struct ehci_host* edev, void* token)
{
    /* Clear from the async list. */
    if (edev->alist_tail) {
        struct QHn *prev, *cur, *tail;
        /* We cache the tail due to distructive updated within the loop */
        prev = tail = edev->alist_tail;
        do {
            cur = prev->next;
            assert(cur != NULL);
            if (cur->token == token) {
                /* Remove it. The doorbell will notify the client */
                cur->was_cancelled = 1;
                _async_remove_next(edev, prev);
                return 0;
            } else {
                prev = cur;
            }
        } while (cur != tail);
    }
    return 1;
}

void
_async_complete(struct ehci_host* edev)
{
    if (edev->alist_tail) {
        struct QHn *cur, *prev, *tail;
        /* We cache the tail because edev->alist_tail may change during node removal */
        prev = tail = edev->alist_tail;
        do {
            enum usb_xact_status stat;
            cur = prev->next;
            stat = qhn_get_status(cur);
            if (stat != XACTSTAT_PENDING) {
                if(stat == XACTSTAT_ERROR){
                    printf("--- <Transfer error> ---\n");
                    dump_qhn(cur);
                    printf("------------------------\n");
                }

                /* Call the completion handler and remove cur. prev is updated to point to a new cur */
                if (cur->cb) {
                    qhn_cb(cur, stat);
                    _async_remove_next(edev, prev);
                }
            } else {
                /* Step over */
                prev = cur;
            }
        } while (cur != tail);
    }
}

int
ehci_schedule_async(struct ehci_host* edev, struct QHn* qh_new)
{
    struct QHn *qh_cur;
    if (edev->alist_tail) {
        /* Insert into list */
        qh_cur = edev->alist_tail;
        /* HeadNew.HorizontalPtr = pHeadCurrent.HorizontalPtr */
        qh_new->qh->qhlptr = qh_cur->qh->qhlptr;
        qh_new->next = qh_cur->next;
        /* pHeadCurrent.HorizontalPointer = paddr(pQueueHeadNew) */
        qh_cur->qh->qhlptr = qh_new->pqh | QHLP_TYPE_QH;
        qh_cur->next = qh_new;
    } else {
        /* New list */
        edev->alist_tail = qh_cur = qh_new;
        qh_new->qh->epc[0] |= QHEPC0_H;
        qh_new->qh->qhlptr = qh_new->pqh | QHLP_TYPE_QH;
        qh_new->next = qh_new;
        edev->op_regs->asynclistaddr = qh_new->pqh;
    }

    /* Enable the async schedule */
    _enable_async(edev);

    if (qh_new->cb == NULL) {
        enum usb_xact_status stat;
        uint32_t v;
        /* Wait for TDs to be processed. */
        stat = qhn_wait(qh_new, 3000);
        v = qhn_get_bytes_remaining(qh_new);
        _async_remove_next(edev, qh_cur);
        return (stat == XACTSTAT_SUCCESS) ? v : -1;
    } else {
        edev->alist_tail = qh_new;
        return 0;
    }
}

void
_async_doorbell(struct ehci_host* edev)
{
    while (edev->db_active) {
        struct QHn* qhn;
        qhn = edev->db_active;
        edev->db_active = qhn->next;
        if (qhn->was_cancelled) {
            enum usb_xact_status stat;
            assert(qhn->cb);
            stat = qhn_get_status(qhn);
            if (stat != XACTSTAT_PENDING) {
                /* The transfer must have completed while it was being cancelled */
                qhn_cb(qhn, stat);
            } else {
                qhn_cb(qhn, XACTSTAT_CANCELLED);
            }
        }
        qhn_destroy(edev->dman, qhn);
    }
}

/* Remove from async schedule; turn the schedule off if it is empty */
void
_async_remove_next(struct ehci_host* edev, struct QHn* prev)
{
    struct QHn* q = prev->next;
    if (prev == q) {
        _disable_async(edev);
        edev->alist_tail = NULL;
        edev->op_regs->asynclistaddr = 0;
    } else {
        /* Remove single node from asynch schedule */
        /* If we are removing the "Head", reassign it */
        if (q->qh->epc[0] & QHEPC0_H) {
            q->next->qh->epc[0] |= QHEPC0_H;
        }
        /* If we removed the tail, reassign it */
        if (q == edev->alist_tail) {
            edev->alist_tail = prev;
        }

        prev->qh->qhlptr = q->qh->qhlptr;
        prev->next = q->next;
    }
    /* Add to doorbell list for cleanup */
    q->next = edev->db_pending;
    edev->db_pending = q;
}
void
check_doorbell(struct ehci_host* edev)
{
    if (_is_enabled_async(edev)) {
        if (edev->db_active == NULL && edev->db_pending != NULL) {
            /* Ring the bell */
            edev->db_active = edev->db_pending;
            edev->db_pending = NULL;
            edev->op_regs->usbcmd |= EHCICMD_ASYNC_DB;
        }
    } else {
        /* Clean up all dangling transactions */
        _async_doorbell(edev);
        edev->db_active = edev->db_pending;
        edev->db_pending = NULL;
        _async_doorbell(edev);
    }
}

