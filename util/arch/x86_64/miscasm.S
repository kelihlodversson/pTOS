/*
 * miscasm.S - x86-64 GEMDOS caller-side trap glue
 *
 * Copyright (C) 2001 by following authors
 * Copyright (C) 2013-2022 by The EmuTOS development team.
 * Copyright (C) 2018-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"

        .globl  C_SYM(trap1)          // call GEMDOS
        .globl  C_SYM(trap1_pexec)    // Reentrant Pexec call

/*
 * trap1 - GEMDOS entry point: long trap1(int fn, ...), matching ARM's own
 * miscasm.S (max 3 real arguments -- every GEMDOS call except Pexec fits
 * that budget, see bdos/bdosmain.c's funcs[] table).
 *
 * fn arrives in %edi (SysV integer arg 1, variadic doesn't change this),
 * up to 3 real args in %esi/%edx/%ecx -- but bios/arch/x86_64/trap.c's
 * dispatcher wants the opcode in RAX (upper 32 bits = X86_64_TRAP_GEMDOS,
 * trap.h) and the real args in RDI/RSI/RDX, so this reshuffles before
 * `syscall`. No stack use before `syscall`, preserving trapasm.S's own
 * %rsp%16==8 entry assumption (its own comment has the full story).
 */
        .text
C_SYM(trap1):
        movl    %edi, %eax      /* fn */
        btsq    $32, %rax       /* | (X86_64_TRAP_GEMDOS << 32) */
        movq    %rsi, %rdi      /* arg1 */
        movq    %rdx, %rsi      /* arg2 */
        movq    %rcx, %rdx      /* arg3 */
        syscall
        ret

/*
 * trap1_pexec - long trap1_pexec(short mode, const char *path,
 * const char *tail, const char *env): Pexec is GEMDOS's one call needing
 * a 4th real argument (trap.c's dispatcher reads it from R10, per
 * trap.h's calling convention). mode/path/tail already arrive in
 * EDI/RSI/RDX -- exactly where the dispatcher wants them -- so only env
 * (RCX, the SysV 4th integer argument) needs moving to R10 before
 * `syscall` clobbers RCX.
 */
        .text
C_SYM(trap1_pexec):
        movq    %rcx, %r10      /* env: RCX (4th C arg) -> R10 */
        movl    $0x4b, %eax     /* GEMDOS Pexec opcode */
        btsq    $32, %rax       /* | (X86_64_TRAP_GEMDOS << 32) */
        syscall
        ret
