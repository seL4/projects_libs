/*
 * Copyright 2016, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#include "../debug.h"
#include "ehci.h"

/****** DEBUG printing *******/
static const char*
dump_colour(enum usb_xact_status stat)
{
    switch (stat) {
    case XACTSTAT_PENDING:
        return CYELLOW;
    case XACTSTAT_ERROR:
    case XACTSTAT_HOSTERROR:
        return CRED;
    case XACTSTAT_SUCCESS:
        return CGREEN;
    default:
        return "";
    }
}

void
dump_qtd(volatile struct TD* qtd)
{
    int pid;
    uint32_t tok;
    const char* col = dump_colour(qtd_get_status(qtd));
    printf(CINVERT"%s", col);
    printf("-- td 0x%08x\n", (uint32_t)qtd);
    printf(CREGULAR"%s", col);
    printf("-    next: 0x%08x | 0x%08x (%s)\n",
           qtd->next, qtd->next & ~0x1f,
           (qtd->next & 0x1) ? "TERMINATE" : "CONTINUE");
    printf("- altnext: 0x%08x | 0x%08x (%s)\n",
           qtd->alt, qtd->alt & ~0x1f,
           (qtd->alt & 0x1) ? "TERMINATE" : "CONTINUE");
    /* Complicated token */
    tok = qtd->token;
    printf("-   token: 0x%08x | ", tok);
    printf("%s", (tok & (1UL << 31)) ? "TOG;" : "");
    printf("totx %d;", (tok >> 16) & 0x7fff);
    printf("%s", (tok & (1UL << 15)) ? "IOC;" : "");
    printf("page %d;", (tok >> 12) & 0x7);
    printf("errs %d;", (tok >> 10) & 0x3);
    pid = (tok >> 8) & 0x3;
    printf("pid %d (%s)", pid, (pid == 0) ? "OUT" :
           (pid == 1) ? "IN" :
           (pid == 2) ? "SETUP" : "RES");
    if (tok & BIT(7)) {
        printf(";ACTIVE");
    }
    if (tok & BIT(6)) {
        printf(";HALTED");
    }
    if (tok & BIT(5)) {
        printf(";BUFFER ERROR");
    }
    if (tok & BIT(4)) {
        printf(";BABBLE DETECTED");
    }
    if (tok & BIT(3)) {
        printf(";TRANSACTION ERR");
    }
    if (tok & BIT(2)) {
        printf(";MISSED uFRAME");
    }
    if (tok & BIT(1)) {
        printf(";COMPLETE SPLIT");
    }
    if (tok & BIT(0)) {
        printf(";DO PING");
    }
    printf("\n");
    /* buffer list */
    {
        int i;
        for (i = 0; i < 5; i++) {
            if (qtd->buf[i] & ~(0x1000 - 1)) {
                printf("- buffer%d: 0x%08x | 0x%08x",
                       i, qtd->buf[i], qtd->buf[i] & ~0xfff);

                if (i == 0) {
                    printf(" (0x%x bytes left)\n", qtd->buf[i] & 0xfff);
                } else {
                    printf("\n");
                }
            }
        }
    }
    set_colour(COL_DEF);
}

void
dump_qhn(struct QHn* qhn)
{
    uint32_t v;
    volatile struct QH* qh;
    const char* col;
    int i;
    col = dump_colour(qhn_get_status(qhn));
    qh = qhn->qh;
    printf(CINVERT"%s", col);
    printf("++ qh 0x%08x\n", (uint32_t)qh);
    printf(CREGULAR"%s", col);
    printf("+ link: 0x%08x | 0x%08x (%s|",
           qh->qhlptr, qh->qhlptr & ~0xf,
           (qh->qhlptr & 0x1) ? "TERMINATE" : "CONTINUE");
    switch ((qh->qhlptr >> 1) & 0x3) {
    case 0:
        printf("ITD)\n") ;
        break;
    case 1:
        printf("QH)\n")  ;
        break;
    case 2:
        printf("SITD)\n");
        break;
    case 3:
        printf("FSTN)\n");
        break;
    }
    v = qh->epc[0];
    printf("+ epc0: 0x%08x| addr %d(%d); NAC reload %d;max pkt %d",
           v, v & 0x7f, (v >> 8) & 0xf, v >> 28, (v >> 16) & 0x7ff);
    if (v & (1 << 27)) {
        printf(";C");
    }
    if (v & (1 << 15)) {
        printf(";H");
    }
    if (v & (1 << 14)) {
        printf(";DTC");
    }
    if (v & (1 <<  7)) {
        printf(";I");
    }

    v = (v >> 12) & 0x3;
    switch (v) {
    case 0:
        printf("; 12Mbs");
        break;
    case 1:
        printf(";1.5Mbs");
        break;
    case 2:
        printf(";480Mbs");
        break;
    case 3:
        printf(";???Mbs");
        break;
    }
    printf("\n");
    printf("C-PROG Mask: 0x%x s-bytes 0x%x frametag 0x%x\n",
           (qh->td_overlay.buf[1] >> 0) & 0xff,
           (qh->td_overlay.buf[2] >> 5) & 0x7f,
           (qh->td_overlay.buf[2] >> 0) & 0x1f);
    v = qh->epc[1];
    printf("+ epc1: 0x%08x; hub %d; port %d; txc %d; uF c%d s%d\n",
           v, (v >> 16) & 0x7f, (v >> 23) & 0x7f, v >> 30,
           (v >> 8) & 0xff, v & 0xff );
    printf("+ current: 0x%08x\n", qh->td_cur);
    dump_qtd(&qh->td_overlay);
    for (i = 0; i < qhn->ntdns; i++) {
        dump_qtd(qhn->tdns[i].td);
    }
    set_colour(COL_DEF);
}

static void UNUSED
dump_q(struct QHn* qhn)
{
    int i = 1;
    struct QHn* head = qhn;
    printf("##### Start Async list #####\n");
    do {
        printf("{QH %d}\n", i++);
        dump_qhn(qhn);
        qhn = qhn->next;
    } while (qhn && qhn != head);
    printf("##### End Async list #####\n");
}

static void UNUSED
dump_edev(struct ehci_host* edev)
{
    uint32_t sts, cmd, intr;
    sts = edev->op_regs->usbsts;
    cmd = edev->op_regs->usbcmd;
    intr = edev->op_regs->usbintr;
    printf("*** EHCI edevice ***\n");
    printf("* usbcmd\n");
    printf("   IRQ threshold: 0x%x\n", (cmd >> 16) & 0xff);
    printf("    async parked: %s\n", (cmd & EHCICMD_ASYNC_PARK) ? "yes" : "no");
    printf(" async park mode: %d\n", (cmd >> 8) & 0x3);
    printf("     light reset: %s\n", (cmd & EHCICMD_LIGHT_RST) ? "yes" : "no");
    printf("  async doorbell: %s\n", (cmd & EHCICMD_ASYNC_DB) ? "yes" : "no");
    printf("           async: %s\n", (cmd & EHCICMD_ASYNC_EN) ? "Enabled" : "Disabled");
    printf(" frame list size: ");
    switch (cmd & EHCICMD_LIST_SMASK) {
    case EHCICMD_LIST_S1024:
        printf("1024\n");
        break;
    case EHCICMD_LIST_S512 :
        printf("512\n") ;
        break;
    case EHCICMD_LIST_S256 :
        printf("256\n") ;
        break;
    default:
        printf("Unknown\n");
    }
    printf("         hcreset: %s\n", (cmd & EHCICMD_HCRESET) ? "yes" : "no");
    printf("       hcrunstop: %s\n", (cmd & EHCICMD_RUNSTOP) ? "yes" : "no");
    printf("* usbsts\n");
    printf("        async en: %s\n", (sts & EHCISTS_ASYNC_EN) ? "yes" : "no");
    printf("     periodic en: %s\n", (sts & EHCISTS_PERI_EN) ? "yes" : "no");
    printf("     async empty: %s\n", (sts & EHCISTS_ASYNC_EMPTY) ? "yes" : "no");
    printf("       hc halted: %s\n", (sts & EHCISTS_HCHALTED) ? "yes" : "no");
    printf("   async advance: %s\n", (sts & EHCISTS_ASYNC_ADV) ? "yes" : "no");
    printf("        host err: %s\n", (sts & EHCISTS_HOST_ERR) ? "yes" : "no");
    printf(" frame list roll: %s\n", (sts & EHCISTS_FLIST_ROLL) ? "yes" : "no");
    printf("     port change: %s\n", (sts & EHCISTS_PORTC_DET) ? "yes" : "no");
    printf("         usb err: %s\n", (sts & EHCISTS_USBERRINT) ? "yes" : "no");
    printf("       usb event: %s\n", (sts & EHCISTS_USBINT) ? "yes" : "no");
    printf("* usbintr\n");
    printf("   async advance: %s\n", (intr & EHCIINTR_ASYNC_ADV) ? "yes" : "no");
    printf("        host err: %s\n", (intr & EHCIINTR_HOST_ERR) ? "yes" : "no");
    printf(" frame list roll: %s\n", (intr & EHCIINTR_FLIST_ROLL) ? "yes" : "no");
    printf("     port change: %s\n", (intr & EHCIINTR_PORTC_DET) ? "yes" : "no");
    printf("         usb err: %s\n", (intr & EHCIINTR_USBERRINT) ? "yes" : "no");
    printf("       usb event: %s\n", (intr & EHCIINTR_USBINT) ? "yes" : "no");
    printf(" *   Frame index: 0x%x\n", edev->op_regs->frindex);
    printf(" * periodic base: 0x%x\n", edev->op_regs->periodiclistbase);
    printf(" *    async base: 0x%x\n", edev->op_regs->asynclistaddr);
}

