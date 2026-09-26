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
 * #333's real ring-3 user processes (#334) will need, so this builds the
 * genuine entry/exit mechanism now (folded into #349 rather than left for
 * #333 to redo) instead of an `int`-based one that would just need
 * replacing later.
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
 * The entry/exit mechanism (trapasm.S's x86_64_syscall_entry, reached only
 * via the actual `syscall` instruction) is genuinely ring-3-capable, not a
 * ring0-only placeholder: it does the full `swapgs` / stack switch /
 * `sysretq` sequence #333 specifies, verified with a throwaway in-kernel
 * ring-3 harness (no real process yet -- that is #334's job). See
 * x86_64_percpu_t below for the per-CPU state that makes this safe without
 * depending on TSS.rsp0 (which `syscall` never consults).
 *
 * It is reached *only* by a genuine ring-3 caller, deliberately: every
 * current caller (Super()/Setexc()/... from bios/bdos boot code, called
 * the way m68k's "trap #1"/ARM's "svc 1" are when already supervisor) goes
 * through x86_64_kernel_trap() below instead, an ordinary C function that
 * calls x86_64_trap_dispatch() directly with no privilege transition at
 * all. An earlier version of this file tried to let both kinds of caller
 * share x86_64_syscall_entry, telling them apart by testing whether the
 * interrupted RSP looked like a kernel address -- unsafe, since `syscall`
 * does not validate RSP at all: a real ring-3 caller can put any 64-bit
 * value there before executing it, including a higher-half one, and get
 * misclassified as kernel-mode, skipping `swapgs`/the stack switch and
 * pushing (and later reading back) attacker-influenced register state at
 * CPL0 on whatever address it chose. Splitting the two mechanisms instead
 * of trying to infer which one a `syscall` came from removes the
 * distinction this bug depended on entirely.
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

/*
 * Per-CPU state trapasm.S's entry/exit stub reaches via %gs, not by
 * symbol: `syscall` leaves RSP pointing at whatever the caller's own
 * stack was (never a kernel one, once real ring-3 callers exist), and
 * unlike an IDT gate's privilege-change path there is no TSS.rspN to
 * supply a safe one automatically (Intel SDM Vol 2B "SYSCALL" -- rsp is
 * "unmodified"). `swapgs` swaps the live GS base with IA32_KERNEL_GS_BASE
 * (x86_64_trap_init() points the latter at this struct), giving the
 * entry stub a way to reach known-good state before it dares touch the
 * stack at all: it saves the interrupted RSP into user_rsp, loads RSP
 * from kernel_rsp, does its normal push/dispatch/pop, then reverses both
 * (restore RSP from user_rsp, swapgs back) before `sysretq`. Only one
 * instance exists (no real multi-CPU support yet, see #329) -- offsets,
 * not the symbol's address, are what trapasm.S actually uses (a fixed
 * struct layout it must be kept in sync with by hand, there being no
 * shared header the assembler and compiler both read here).
 */
typedef struct {
    UQUAD kernel_rsp; /* offset 0: loaded into rsp on every syscall entry */
    UQUAD user_rsp;   /* offset 8: the interrupted (caller's) rsp, saved here across the round trip */
} x86_64_percpu_t;

/*
 * Called from trapasm.S's entry stub (from_ring3 = 1) and from
 * x86_64_kernel_trap() below (from_ring3 = 0). Reads frame->rax/rdi/rsi/
 * rdx/r10 per the calling convention above, dispatches directly to
 * osif()/bios_vecs[]/xbios_vecs[] (no vector-table indirection: this arch
 * does not need ARM's dynamic-hooking generality, since bios_init()'s
 * VEC_GEM/VEC_BIOS/VEC_XBIOS writes go nowhere this ever reads), and
 * leaves the result in frame->rax for the caller to pick up.
 *
 * from_ring3 tells this function whether frame->rdi/rsi/rdx/r10 are
 * untrusted register values a genuine ring-3 caller supplied (see
 * trap.c's x86_64_arg_hits_known_kernel_range() -- not general
 * user-pointer validation, read its own comment before assuming
 * otherwise) or trusted arguments a kernel-mode caller passed via
 * x86_64_kernel_trap() -- the latter legitimately includes real kernel
 * (higher-half) pointers, e.g. an internal buffer bdos/fsmain.c passes
 * to Cconws(), so the "reject a known-kernel-mapped address" check
 * below only ever applies to the former.
 */
void x86_64_trap_dispatch(x86_64_trap_frame_t *frame, int from_ring3);

/*
 * Kernel-mode entry point for GEMDOS/BIOS/XBIOS calls: builds a throwaway
 * x86_64_trap_frame_t on the caller's own stack and calls
 * x86_64_trap_dispatch() directly, exactly as if this were a genuine
 * `syscall` round trip, but as a plain, ordinary, recursion-safe C
 * function call -- no privilege transition, no swapgs, no dedicated
 * stack. This is what every current caller (util/arch/x86_64/miscasm.S's
 * trap1()/trap1_pexec(), and every x86_64 branch in include/biosbind.h/
 * xbiosbind.h) uses instead of x86_64_syscall_entry, since none of them
 * are a real ring-3 process (there is no such thing yet -- #334) and
 * some of them call each other (bdos/fsmain.c's xsetdrv() calling
 * Drvmap(), for one): an ordinary C call nests to any depth for free the
 * same way any other recursive call does, on whatever kernel stack is
 * already in use, which the shared syscall entry point cannot do without
 * either corrupting its own single fixed stack bustack or performing the
 * unsafe caller-classification the comment above rejects.
 *
 * Only rax/rdi/rsi/rdx/r10 are meaningful -- exactly the fields
 * x86_64_trap_dispatch()'s GEMDOS/BIOS/XBIOS cases read -- matching the
 * `syscall` calling convention above field for field so callers can be
 * written (and read) as if this were that same convention with the
 * privilege transition simply subtracted out.
 */
long x86_64_kernel_trap(long rax, long rdi, long rsi, long rdx, long r10);

/*
 * Called from x86_64_syscall_entry's exit path (trapasm.S) when the
 * about-to-be-restored return RIP (RCX -- entirely caller-controlled,
 * see trapasm.S's own comment) is not a canonical address. `sysretq`
 * does not validate this itself, and executing it anyway is the exact
 * hardware hazard CVE-2012-0217 (and its Xen/*BSD analogues) exploited:
 * on real silicon, a non-canonical target can fault after privilege/
 * segment state has already started changing, delivering the resulting
 * #GP with attacker-influenced state still in effect rather than
 * cleanly at CPL0 beforehand. There is no user-mode fault-delivery
 * mechanism yet (#334) to hand this back to the offending process
 * safely, so this panics instead -- loud and diagnosable beats silently
 * exploitable. Never returns.
 */
void x86_64_bad_sysret(UQUAD bad_rip) NORETURN;

/*
 * Enables the `syscall`/`sysret` extension (IA32_EFER.SCE), points
 * IA32_STAR/IA32_LSTAR/IA32_FMASK at this arch's entry stub
 * (x86_64_syscall_entry, trapasm.S) and the kernel/user selectors gdt.h
 * already builds (no dedicated segments needed beyond those), and points
 * IA32_KERNEL_GS_BASE at this CPU's x86_64_percpu_t so the entry stub's
 * `swapgs` has something to swap in. Must run after x86_64_gdt_init(),
 * whose selectors and ring-3 GDT layout this reuses.
 */
void x86_64_trap_init(void);

#endif /* X86_64_TRAP_H */
