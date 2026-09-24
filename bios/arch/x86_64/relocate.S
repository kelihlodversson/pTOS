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
 * new_stack_top must already be this image's higher-half virtual address
 * of its own boot stack's top (startup.c translates it the same way as
 * high_half_target, not the low/identity one), 16-byte aligned per the
 * SysV AMD64 ABI's stack alignment requirement at a public interface.
 */
        .globl  C_SYM(x86_64_relocate_higher_half)

C_SYM(x86_64_relocate_higher_half):
        mov     %rdx, %rsp
        /*
         * jmp, unlike call, does not push a return address, so without
         * this the SysV AMD64 ABI's "%rsp % 16 == 8 at function entry"
         * requirement (as if a call had just pushed one) would be off by
         * 8: x86_64_higher_half_main()'s first 16-byte-aligned SSE access
         * would then fault. new_stack_top is 16-byte aligned, so this
         * reproduces exactly what a call would have left behind.
         */
        sub     $8, %rsp
        mov     %rsi, %cr3
        jmp     *%rdi

        /* Not reached: %rdi points at x86_64_higher_half_main(), which is
         * NORETURN. This is a safety net in case that ever changes. */
.Lhang:
        cli
        hlt
        jmp     .Lhang
