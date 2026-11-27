/*
 * x32crt.S - entry point of the x32 (ring 3) EmuCON
 *
 * The initial ELF stack is already aligned for a System V call.
 * A build without the filesystem ELF loader retains a private boot entry.
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#include "config.h"

        .text
        .globl  _start
        .type   _start, @function
_start:
        xorl    %ebp, %ebp              /* end of the frame chain */
#if CONF_WITH_ELF_LOADER
        movl    %esp, %edi
        movl    %edx, %esi
        call    __libc_start_elf
        ud2
#else
        jmp     x32_start
#endif
        .size   _start, . - _start
        .section .note.GNU-stack,"",@progbits
