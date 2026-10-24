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

    /* the 68040 access error (format 7) and address error give the address of the access */
    if ((fv >> 12) == 7 || (fv >> 12) == 2)
        address = *(const ULONG *)(f + 8);
    proc_fault(cause_of(proc_enum), (UQUAD)proc_enum, address, pc, (UQUAD)(ULONG)proc_usp);
}

#endif /* CONF_WITH_USER_ASPACE */
