/*
 * relocate.S - jump from the low, EFI-loaded copy of this image to its
 * higher-half virtual alias, after loading the page tables that map both
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"

        .text

/*
 * NORETURN void x86_64_relocate_higher_half(UQUAD high_half_target,
 *                                            UQUAD cr3_value,
 *                                            void *new_stack_top);
 *
 * SysV AMD64 ABI: rdi = high_half_target, rsi = cr3_value,
 * rdx = new_stack_top.
 *
 * high_half_target is the higher-half virtual address to continue at once
 * the new page tables are live -- computed in C (startup.c), as
 * X86_64_KERNEL_VIRT_BASE + (&x86_64_higher_half_main - image_base), since
 * &x86_64_higher_half_main is already this image's actual (low, EFI-
 * chosen) runtime address there: like every other absolute pointer in this
 * PIE image, the firmware's PE loader already relocated it to account for
 * wherever this image was actually loaded, before efi_main() ever ran.
 * new_stack_top must fall inside the range x86_64_build_page_tables()
 * (pgtable.c) was told to map: it keeps working immediately after the CR3
 * switch because that range is identity-mapped, without needing its own
 * high-half translation here.
 */
        .globl  C_SYM(x86_64_relocate_higher_half)

C_SYM(x86_64_relocate_higher_half):
        mov     %rdx, %rsp
        mov     %rsi, %cr3
        jmp     *%rdi

        /* Not reached: %rdi points at x86_64_higher_half_main(), which is
         * NORETURN. This is a safety net in case that ever changes. */
.Lhang:
        cli
        hlt
        jmp     .Lhang
