/*
 * miscasm_rpi.S - Some small helper functions in assembler
 *
 * Copyright (C) 2001 by following authors
 * Copyright (C) 2013 by The EmuTOS development team.
 *
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"

        .globl  _trap1          // call GEMDOS
        .globl  _trap1_pexec    // Reentrant Pexec call


/*
 * trap1 - trap 1 (GEMDOS) entry point. Reentrant as opposed to the m68k version
 */
        .text
_trap1:
        push {r7, lr}
        // The system call op code is in r0, so we shift down the arguments by
        // one and move r0 to r7 - the system call number register
        mov r7, r0
        mov r0, r1
        mov r1, r2
        mov r2, r3
        svc 1
        pop {r7, pc}

/*
 *  GEMDOS Pexec() call. This version is reentrant as well
 *  long trap1_pexec(short mode, const char * path, const void * tail, const char * env);
 */
        .text
_trap1_pexec:
        // we assume r0-r3 already contain the correct arguments
        push {r7 ,lr}
        mov r7, #0x4b // Pexec op code in r7
        svc 1
        pop {r7, pc}
