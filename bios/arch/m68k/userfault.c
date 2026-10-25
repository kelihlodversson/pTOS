/*
 * userfault.c - an exception in a process in its own address space (68040, 68060)
 *
 * vectors.S's any_vec calls m68k_user_fault() on the kernel stack of a
 * process that took an exception in user mode, with the exception frame at
 * user_fault_frame: a bus error, an address error, an illegal instruction, a
 * trap with no handler.  The process is ended; nothing else is touched.
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#if CONF_WITH_USER_ASPACE

#include "emutos.h"
#include "tosvars.h"
#include "procmem.h"
#include "aspace32.h"
#if CONF_WITH_DEMAND_PAGING
#include "bdosdefs.h"
#include "../bdos/kproc.h"
#include "gemerror.h"
#endif

extern void proc_fault(const char *cause, UQUAD code, UQUAD address, UQUAD pc, UQUAD sp) NORETURN;

ULONG user_fault_frame;                 /* the exception frame (any_vec) */

void m68k_user_fault(void) NORETURN;

static const char *cause_of(LONG vector)
{
    switch (vector) {
    case 2:  return "bus error";
    case 3:  return "address error";
    case 4:  return "illegal instruction";
    case 5:  return "divide by zero";
    case 6:  return "CHK";
    case 7:  return "TRAPV";
    case 8:  return "privilege violation";
    case 9:  return "trace";
    case 10: return "line-A";
    case 11: return "line-F";
    case 14: return "format error";
    default: return vector >= 32 && vector < 48 ? "TRAP" : "exception";
    }
}

void m68k_user_fault(void)
{
    const UBYTE *f = (const UBYTE *)user_fault_frame;
    ULONG pc = *(const ULONG *)(f + 2);
    UWORD fv = *(const UWORD *)(f + 6);
    ULONG address = pc;

    /* the 68040 access error (format 7) has the fault address at offset 20
     * (offset 8 is the effective address, which can be another page); the
     * address of a format 2 frame is at offset 8 */
    if ((fv >> 12) == 7)
        address = *(const ULONG *)(f + 20);
    else if ((fv >> 12) == 2)
        address = *(const ULONG *)(f + 8);
    proc_fault(cause_of(proc_enum), (UQUAD)proc_enum, address, pc, (UQUAD)(ULONG)proc_usp);
}

#if CONF_WITH_DEMAND_PAGING
extern PD *run;                         /* bdos/proc.c */

/* offsets in the 68040 access error frame (format 7) */
#define F7_SSW      12
#define F7_WB3S     14
#define F7_WB2S     16
#define F7_WB1S     18
#define F7_FADDR    20
#define F7_WB3A     24
#define F7_WB3D     28
#define F7_WB2A     32
#define F7_WB2D     36
#define F7_WB1A     40
#define F7_WB1D     44

#define SSW_READ    0x0100              /* the access was a read */
#define SSW_TM      0x0007              /* its function code */
#define WB_VALID    0x0080
#define WB_SIZE     0x0060              /* 0 long, 1 byte, 2 word, 3 line */
#define WB_TM       0x0007

/*
 * A write the 68040 still owes (a write-back of the frame): done now that the
 * page is there, for a user address only.  FALSE if it cannot be.
 */
static BOOL write_back(UWORD status, ULONG address, ULONG data)
{
    if (!(status & WB_VALID))
        return TRUE;
    if ((status & WB_TM) != 1 || address >= USER_VA_LIMIT)
        return FALSE;
    switch (status & WB_SIZE) {
    case 0x00:
        return kproc_copy_to_user(address, &data, 4);
    case 0x20:
        {
            UBYTE b = (UBYTE)data;

            return kproc_copy_to_user(address, &b, 1);
        }
    case 0x40:
        {
            UWORD w = (UWORD)data;

            return kproc_copy_to_user(address, &w, 2);
        }
    default:                            /* a line: only a MOVE16 or a cache push */
        return FALSE;
    }
}

/*
 * A bus error in a process in user mode (vectors.S's buserr_user), with the
 * exception frame at user_fault_frame: if the page is one of its lazy memory (or a file page it
 * writes to), back it and return 1 -- the 68040 runs a faulting read again,
 * and the writes it still owed are done here -- else 0, and the fault is the
 * process's own (m68k_user_fault()).  Only the 68040 frame is understood.
 */
int m68k_user_busfault(void);

int m68k_user_busfault(void)
{
    const UBYTE *f = (const UBYTE *)user_fault_frame;
    UWORD fv = *(const UWORD *)(f + 6);
    UWORD ssw = *(const UWORD *)(f + F7_SSW);
    ULONG address = *(const ULONG *)(f + F7_FADDR);
    ASPACE_ROOT root = kproc_user_root(run);
    int answer;

    if ((fv >> 12) != 7 || address >= USER_VA_LIMIT || !root || root != aspace_hw_current())
        return 0;
    answer = kproc_demand_page(run, address,
                               (ssw & SSW_READ) ? (((ssw & SSW_TM) == 2) ? ASPACE_PROT_EXEC : 0)
                                                : ASPACE_PROT_WRITE);
    if (answer == 2)
        proc_fault("no physical page left", (UQUAD)ssw, address, *(const ULONG *)(f + 2),
                   (UQUAD)(ULONG)proc_usp);
    if (answer == 3)
        proc_fault("file mapping I/O error", (UQUAD)ssw, address, *(const ULONG *)(f + 2),
                   (UQUAD)(ULONG)proc_usp);
    if (answer != 1)
        return 0;
    if (!write_back(*(const UWORD *)(f + F7_WB1S), *(const ULONG *)(f + F7_WB1A),
                    *(const ULONG *)(f + F7_WB1D)) ||
        !write_back(*(const UWORD *)(f + F7_WB2S), *(const ULONG *)(f + F7_WB2A),
                    *(const ULONG *)(f + F7_WB2D)) ||
        !write_back(*(const UWORD *)(f + F7_WB3S), *(const ULONG *)(f + F7_WB3A),
                    *(const ULONG *)(f + F7_WB3D)))
        return 0;
    return 1;
}
#endif /* CONF_WITH_DEMAND_PAGING */

#endif /* CONF_WITH_USER_ASPACE */
