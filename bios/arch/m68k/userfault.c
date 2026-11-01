/*
 * userfault.c - an exception in a process in its own address space (68030, 68040, 68060)
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

/* the 68030 bus error frames (format A, 32 bytes, and format B, 92 bytes) */
#define F30_SSW     10
#define F30_STAGEB  0x24                /* format B only: the address of the instruction pipe stage B */
#define F30_FADDR   16                  /* the address of the data cycle that failed */

#define SSW30_FC    0x8000              /* a fault on the instruction pipe, stage C */
#define SSW30_FB    0x4000              /* ... stage B */
#define SSW30_DF    0x0100              /* a fault on a data cycle */
#define SSW30_RM    0x0080              /* the cycle was a read-modify-write */
#define SSW30_READ  0x0040
#define SSW30_SIZ   0x0030              /* 0 long, 1 byte, 2 word, 3 three bytes */
#define SSW30_FCODE 0x0007

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
    else if ((fv >> 12) == 2 || (fv >> 12) == 4)   /* (68060 access error: format 4) */
        address = *(const ULONG *)(f + 8);
    else if (((fv >> 12) == 0xa || (fv >> 12) == 0xb) && (*(const UWORD *)(f + F30_SSW) & SSW30_DF))
        address = *(const ULONG *)(f + F30_FADDR);    /* (68030 bus error: the data cycle that failed) */
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

#define SSW_MA      0x0800              /* misaligned: it crosses a page */
#define SSW_LK      0x0200              /* locked: a read-modify-write cycle */
#define SSW_READ    0x0100              /* the access was a read */
#define SSW_TM      0x0007              /* its function code */
#define WB_VALID    0x0080
#define WB_SIZE     0x0060              /* 0 long, 1 byte, 2 word, 3 line */
#define WB_TM       0x0007

/* the 68060 access error frame (format 4): the fault address at 8, the fault status long word at 12 */
#define F4_FADDR    8
#define F4_FSLW     12

#define FSLW_MA     0x08000000UL        /* misaligned: it crosses a page */
#define FSLW_LK     0x02000000UL        /* locked: a read-modify-write cycle */
#define FSLW_WRITE  0x00800000UL        /* R/W is 01 (write) or 11 (read-modify-write); 10 is a read */
#define FSLW_SIZE   0x00600000UL        /* 0 long, 1 byte, 2 word, 3 line, as the 68040's */

/*
 * A write the 68040 still owes (a write-back of the frame): done now that the
 * page is there, for a user address only.  FALSE if it cannot be.
 */
static BOOL write_back(UWORD status, ULONG address, ULONG data, BOOL lane_aligned)
{
    if (!(status & WB_VALID))
        return TRUE;
    if ((status & WB_TM) != 1 || address >= USER_VA_LIMIT)
        return FALSE;
    /*
     * The data of write-back 1 sits in the byte lanes its address selects (a
     * byte at offset 1 in bits 23-16, a word at offset 2 in bits 15-0); the
     * others are right-justified.  A long at an odd address is split into several
     * bus cycles by the CPU, in a way not decoded here: the process ends.
     */
    if (lane_aligned) {
        switch (status & WB_SIZE) {
        case 0x20:
            data >>= (3 - (address & 3)) * 8;
            break;
        case 0x40:
            if (address & 1)
                return FALSE;
            data >>= (2 - (address & 2)) * 8;
            break;
        case 0x00:
            if (address & 3)
                return FALSE;
            break;
        }
    }
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
 * process's own (m68k_user_fault()).  The frames of the 68040, 68060 and 68030 are
 * understood.
 */
int m68k_user_busfault(void);

int m68k_user_busfault(void)
{
    const UBYTE *f = (const UBYTE *)user_fault_frame;
    UWORD fv = *(const UWORD *)(f + 6);
    ULONG address, size;
    ASPACE_ROOT root = kproc_user_root(run);
    BOOL write, misaligned, writebacks, fetch = FALSE;
    UWORD ssw = 0;
    UWORD access;
    int answer;

    if ((fv >> 12) == 7) {              /* 68040 */
        static const UBYTE sizes[4] = { 4, 1, 2, 16 };

        ssw = *(const UWORD *)(f + F7_SSW);
        address = *(const ULONG *)(f + F7_FADDR);
        /* a locked access (TAS, CAS) reports a read, and writes */
        write = !(ssw & SSW_READ) || (ssw & SSW_LK);
        misaligned = (ssw & SSW_MA) != 0;
        size = sizes[(ssw >> 5) & 3];
        writebacks = TRUE;
    } else if ((fv >> 12) == 4) {       /* 68060: precise, the instruction runs again; no write-backs */
        static const UBYTE sizes[4] = { 4, 1, 2, 16 };
        ULONG fslw = *(const ULONG *)(f + F4_FSLW);

        address = *(const ULONG *)(f + F4_FADDR);
        write = (fslw & (FSLW_WRITE | FSLW_LK)) != 0;
        misaligned = (fslw & FSLW_MA) != 0;
        size = sizes[(fslw & FSLW_SIZE) >> 21];
        ssw = (UWORD)(fslw >> 16);
        writebacks = FALSE;
    } else if ((fv >> 12) == 0xa || (fv >> 12) == 0xb) {   /* 68030: precise, the cycle is run again by the RTE */
        static const UBYTE sizes[4] = { 4, 1, 2, 3 };

        ssw = *(const UWORD *)(f + F30_SSW);
        /* a fault in a read-modify-write cycle (TAS, CAS) is not served: the CPU
         * cannot be relied on to run the whole cycle again (FreeMiNT does not
         * either): the process's own */
        if (ssw & SSW30_RM)
            return 0;
        if (ssw & SSW30_DF) {
            address = *(const ULONG *)(f + F30_FADDR);
            write = !(ssw & SSW30_READ) || (ssw & SSW30_RM);
            size = sizes[(ssw & SSW30_SIZ) >> 4];
            misaligned = (address & (USER_PAGE_SIZE - 1)) + size > USER_PAGE_SIZE;
        } else if (ssw & (SSW30_FC | SSW30_FB)) {
            /* the instruction pipe: the words after the one that runs (format B
             * names the address of stage B; format A does not, and the pages of the
             * next eight bytes are asked for) */
            address = *(const ULONG *)(f + 2);
            size = 8;
            if ((fv >> 12) == 0xb) {
                /* the long frame gives the address of stage B: stage C is the word before it */
                address = *(const ULONG *)(f + F30_STAGEB) - ((ssw & SSW30_FC) ? 2 : 0);
                size = (ssw & SSW30_FC) ? 4 : 2;
            }
            write = FALSE;
            misaligned = TRUE;
            fetch = TRUE;
        } else {
            return 0;
        }
        writebacks = FALSE;
    } else {
        return 0;
    }
    if (address >= USER_VA_LIMIT || !root || root != aspace_hw_current())
        return 0;
    /* (an instruction fetch asks no more than a read: nothing here is executable
     * for its own sake on a CPU without an execute-disable bit) */
    access = write ? ASPACE_PROT_WRITE : (((fv >> 12) == 7 && (ssw & SSW_TM) == 2) || fetch ? ASPACE_PROT_EXEC : 0);
    answer = kproc_demand_page(run, address, access);
    if (misaligned) {
        /*
         * A misaligned access that crosses a page: the address of the frame can be
         * that of the first byte, in a page that is there, while the page that
         * failed is the next one.  Every page it touches is asked, and what is
         * there already is no failure; the access runs again and, if it
         * needs another page, faults again.
         */
        ULONG last = address + size - 1, page;

        if (last < address || last >= USER_VA_LIMIT)
            return 0;
        for (page = (address & ~(ULONG)(USER_PAGE_SIZE - 1)) + USER_PAGE_SIZE;
             answer <= 1 && page <= last; page += USER_PAGE_SIZE) {
            int more = kproc_demand_page(run, page, access);

            if (more > answer)
                answer = more;
        }
    }
    if (answer == 2)
        proc_fault("no physical page left", (UQUAD)ssw, address, *(const ULONG *)(f + 2),
                   (UQUAD)(ULONG)proc_usp);
    if (answer == 3)
        proc_fault("file mapping I/O error", (UQUAD)ssw, address, *(const ULONG *)(f + 2),
                   (UQUAD)(ULONG)proc_usp);
    if (answer != 1)
        return 0;
    if (writebacks &&
        (!write_back(*(const UWORD *)(f + F7_WB1S), *(const ULONG *)(f + F7_WB1A),
                    *(const ULONG *)(f + F7_WB1D), TRUE) ||
        !write_back(*(const UWORD *)(f + F7_WB2S), *(const ULONG *)(f + F7_WB2A),
                    *(const ULONG *)(f + F7_WB2D), FALSE) ||
        !write_back(*(const UWORD *)(f + F7_WB3S), *(const ULONG *)(f + F7_WB3A),
                    *(const ULONG *)(f + F7_WB3D), FALSE)))
        return 0;
    return 1;
}
#endif /* CONF_WITH_DEMAND_PAGING */

#endif /* CONF_WITH_USER_ASPACE */
