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
 * Can only handle max 3 arguments (4 incl the GEMDOS opcode.)
 */
        .text
_trap1:
        push {lr}
        // Arguments are already in the correct registers
        svc 1
        pop {pc}

/*
 *  GEMDOS Pexec() call. This version is reentrant as well, but can handle that extra argument required.
 *  long trap1_pexec(short mode, const char * path, const void * tail, const char * env);
 */
        .text
_trap1_pexec:
        // we need to shift r0-r3 up by one to make room for the Pexec op code in r0
        push {r0-r4 ,lr}
        pop {r1-r4}
        mov r0, #0x4b // Pexec op code in r0
        svc 1
        pop {r4, pc}
