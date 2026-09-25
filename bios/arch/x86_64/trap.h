/*
 * trap.h - x86-64 GEMDOS/BIOS/XBIOS trap dispatch
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef X86_64_TRAP_H
#define X86_64_TRAP_H

#include "portab.h"

/*
 * Every pTOS system call -- GEMDOS, BIOS and XBIOS alike -- reaches the
 * kernel through the `syscall` instruction, not an `int`-based gate: real
 * m68k hardware (and ARM's own simulation of it, bios/arch/arm/
 * vectorsasm.S) gives each its own CPU trap number (1/13/14) with a
 * shared low-memory vector table steering them to
 * gemtrap()/biostrap()/xbiostrap(); x86-64 has no such hardware table to
 * populate, and nothing here reads back what bios_init() (bios/bios.c)
 * writes into it (VEC_GEM/VEC_BIOS/VEC_XBIOS, bios/vectors.h) -- see
 * x86_64_map_low_vectors() (pgtable.c). `syscall` is also the mechanism
 * #333's eventual real ring-3 user processes will need anyway, so this
 * builds that path now rather than an `int`-based one that would just
 * need replacing later.
 *
 * Entry-time calling convention for `syscall`:
 *
 *   RAX = (trap_class << 32) | function_number
 *   RDI, RSI, RDX, R10 = up to 4 real arguments
 *
 * R10 stands in for the "4th argument register" rather than RCX: the
 * `syscall` instruction itself clobbers RCX (loaded with the return RIP)
 * and R11 (loaded with the pre-syscall RFLAGS), so neither is available
 * as an argument register -- exactly the reason the Linux x86-64 syscall
 * ABI substitutes R10 for RCX in the same spot, which this mirrors.
 *
 * trap_class matches the historic m68k trap number for the same call
 * class (1 = GEMDOS, 13 = BIOS, 14 = XBIOS) -- not read back from
 * anywhere, just kept for continuity with vectors.h's existing
 * VEC_TRAP1/13/14 naming. function_number is whatever that class's own
 * handler table (bdos/bdosmain.c's osif(), bios.c's bios_vecs[],
 * xbios.c's xbios_vecs[]) already expects.
 *
 * Only 4 real argument registers, matching ARM's own convention (rather
 * than the 2 more x86-64 could spare after RCX/R11 are excluded): the
 * handful of BIOS/XBIOS calls needing more than that already have an
 * established solution, reused unchanged -- a pointer to one of
 * include/biosargs.h's structs as the single real argument.
 *
 * No ring-3 support yet (#333/#334): every caller today is already at
 * CPL0, so x86_64_trap_init() leaves STAR's SYSRET (user) segments unset
 * and the entry stub (trapasm.S) returns via a manual RFLAGS-restore-and-jump
 * rather than `sysretq`, which unconditionally forces CPL3 -- not what a
 * ring0-to-ring0 call needs, and not safely usable at all yet with no
 * ring-3 GDT entries. Revisit both once real user-mode processes exist.
 */
#define X86_64_TRAP_GEMDOS 1
#define X86_64_TRAP_BIOS 13
#define X86_64_TRAP_XBIOS 14

/*
 * The register frame trapasm.S's entry stub builds on the stack before
 * calling x86_64_trap_dispatch(), overlaid as a struct -- field order
 * matches memory order low-to-high address, i.e. the reverse of the
 * trampoline's push sequence (last pushed = lowest address = first
 * field). rcx and r11 are the `syscall`-provided return RIP and RFLAGS
 * (see above), saved here only so trapasm.S can restore them for its manual
 * return -- x86_64_trap_dispatch() has no reason to read or write either.
 * Unlike x86_64_exception_frame_t (panic.h), there is no CPU-pushed
 * portion at all: `syscall` does not build a stack frame the way an
 * exception or `int` does, so everything here is trapasm.S's own doing.
 */
typedef struct {
    UQUAD r15, r14, r13, r12, r10, r9, r8, rbp, rdi, rsi, rdx, rbx, rax, r11, rcx;
} PACKED x86_64_trap_frame_t;

/* Called from trapasm.S's entry stub. Reads frame->rax/rdi/rsi/rdx/r10 per
 * the calling convention above, dispatches directly to osif()/
 * bios_vecs[]/xbios_vecs[] (no vector-table indirection: this arch does
 * not need ARM's dynamic-hooking generality, since bios_init()'s
 * VEC_GEM/VEC_BIOS/VEC_XBIOS writes go nowhere this ever reads), and
 * leaves the result in frame->rax for trapasm.S to restore before returning. */
void x86_64_trap_dispatch(x86_64_trap_frame_t *frame);

/*
 * Enables the `syscall`/`sysret` extension (IA32_EFER.SCE) and points
 * IA32_STAR/IA32_LSTAR/IA32_FMASK at this arch's entry stub
 * (x86_64_syscall_entry, trapasm.S) and kernel code/data selectors (gdt.h).
 * Must run after x86_64_gdt_init(), whose X86_64_KERNEL_CODE_SEL/
 * X86_64_KERNEL_DATA_SEL this reuses for STAR -- they need no dedicated
 * segments of their own, since SYSCALL's kernel CS/SS requirement (CS
 * immediately followed by SS) is exactly this GDT's existing layout.
 */
void x86_64_trap_init(void);

#endif /* X86_64_TRAP_H */
