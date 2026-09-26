/*
 * trap.c - x86-64 GEMDOS/BIOS/XBIOS trap dispatch
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "portab.h"
#include "biosext.h"
#include "gdt.h"
#include "gemerror.h"
#include "io.h"
#include "pgtable.h"
#include "trap.h"

/*
 * osif() (bdos/bdosmain.c) is GEMDOS's own C-level trap #1 handler,
 * declared there to take a uniform native-`long` pw[] on this arch (see
 * bdosmain.c's own comment on the #if defined(__arm__) ||
 * defined(__x86_64__) split) -- not in any shared header, so declared
 * here exactly as ARM's bdos/arch/arm/rwa.S implicitly relies on it too.
 * Deliberately `long`, not portab.h's always-32-bit LONG: several GEMDOS
 * calls (Pexec, Cconws, Fsetdta, Mfree, ...) pass a genuine pointer
 * through one of these slots, and bdosmain.c's own osif() reinterprets a
 * slot's address directly as a pointer of the real argument's width
 * (e.g. `*((char **)&pw[1])`) -- correct only if each slot is exactly
 * pointer-width. On ARM (ILP32) that is already 32 bits, same as LONG,
 * so this changes nothing there; on x86-64 (LP64) pointers are 64 bits,
 * and a LONG-sized slot would silently truncate every such pointer to
 * its low 32 bits before osif() ever saw it.
 *
 * bios_vecs[]/xbios_vecs[] (bios/bios.c, bios/xbios.c) are the BIOS/XBIOS
 * opcode-indexed function tables every other arch's own trap dispatcher
 * (bios/arch/m68k/vectors.S's biosxbios, ARM's arm_dispatch_svc via
 * VEC_BIOS/VEC_XBIOS) already calls into the same way: index by function
 * number, call through the generic PFLONG type with however many real
 * arguments that call actually needs -- safe and already this
 * codebase's own established convention, since a callee only ever reads
 * the argument registers its own real signature declares.
 */
extern long osif(long *pw);
extern const PFLONG bios_vecs[];
extern const UWORD bios_ent;
extern const PFLONG xbios_vecs[];
extern const UWORD xbios_ent;

/*
 * xbios_vecs[]'s filler for unimplemented function slots (bios/xbios.c).
 * On m68k/ARM it's an assembly trampoline that has the shared BIOS/XBIOS
 * trap entry's own dispatch loop leave the requested function number in
 * a register before jumping in, so xbios_do_unimpl() can read it back
 * out; this arch's dispatch (below) never goes through such a loop --
 * xbios_vecs[fn] is called directly, with fn only ever known here, not
 * inside whatever it points to. Special-cased below instead, the same
 * way an out-of-range fn already is.
 */
extern LONG xbios_unimpl(void);

/*
 * xbios_unimpl's real address, materialized once by x86_64_trap_init()
 * via the same forced RIP-relative `lea` x86_64_syscall_entry's own
 * comment explains (this PE link has no GOT for a plain C
 * `(PFLONG)xbios_unimpl` expression to safely go through), and compared
 * against here instead of taking xbios_unimpl's address directly at
 * every dispatch.
 */
static PFLONG xbios_unimpl_addr;

extern void x86_64_syscall_entry(void);

/* IA32_EFER/STAR/LSTAR/FMASK/KERNEL_GS_BASE (Intel SDM Vol 2B "SYSCALL"/
 * "SYSRET"/"SWAPGS"; Vol 4 2.1 for the MSR indices). */
#define MSR_EFER 0xC0000080UL
#define MSR_STAR 0xC0000081UL
#define MSR_LSTAR 0xC0000082UL
#define MSR_FMASK 0xC0000084UL
#define MSR_KERNEL_GS_BASE 0xC0000102UL

#define EFER_SCE 0x1ULL /* SYSCALL/SYSRET enable */

/*
 * This CPU's per-CPU state (trap.h). Only one instance: no real
 * multi-CPU support yet (#329). trapasm.S reaches it by fixed offset via
 * %gs after `swapgs`, never by this symbol's address -- see trap.h's own
 * comment.
 */
static x86_64_percpu_t percpu;

/*
 * The kernel stack syscall entry switches to (percpu.kernel_rsp below),
 * distinct from this image's own boot-time stack (startup.c's
 * boot_stack): a real ring-3 caller's `syscall` always arrives while the
 * kernel is between processes, never while boot_stack is itself in the
 * middle of being used, but keeping the two separate now avoids relying
 * on that not being true yet. 8 KiB is generous for a path that does not
 * recurse (trap.c's dispatch is a single switch, and osif()/bios_vecs[]/
 * xbios_vecs[] are shallow existing call trees on every other arch).
 */
#define SYSCALL_STACK_BYTES 8192
static UBYTE syscall_stack[SYSCALL_STACK_BYTES] __attribute__((aligned(16)));

/*
 * Generous over-approximations of this image's own load span and the
 * physical-memory direct map's actual size, used below rather than the
 * exact bounds (startup.c's IMAGE_SPAN_BYTES, x86_64_pmem_highest_addr())
 * -- pulling either in would mean this arch-generic file depending on a
 * bios/machine/pc-x86_64 header, the layering CLAUDE.md asks arch/ code
 * to avoid. Wildly generous is fine: these only need to safely contain
 * the real ranges, not match them tightly (see x86_64_is_kernel_mapped_addr()'s
 * own comment on why a loose bound here still cannot misfire against a
 * real GEMDOS argument).
 */
#define X86_64_KERNEL_IMAGE_SPAN_GENEROUS (32ULL * 1024 * 1024)  /* actual span ~4 MiB */
#define X86_64_PHYS_MAP_SPAN_GENEROUS     (1ULL << 40)           /* 1 TiB */

/*
 * True iff addr falls inside one of this kernel's own two known-mapped
 * address ranges: its own load image (X86_64_KERNEL_VIRT_BASE upward)
 * or the physical-memory direct map (X86_64_PHYS_MAP_BASE upward,
 * pgtable.h) -- the only ranges where a bad dereference could actually
 * read or corrupt live kernel state, as opposed to merely faulting
 * harmlessly into unmapped kernel-half address space. Used by
 * x86_64_trap_dispatch() to reject a genuine ring-3 caller's raw syscall
 * argument without ever dereferencing it at CPL0 on the caller's behalf.
 *
 * This replaced an earlier, broader "reject anything outside the low
 * canonical half" version (#350's review): that one also rejected
 * ordinary *signed* GEMDOS arguments sign-extended into the upper half --
 * Fseek()'s negative offset, Mxalloc()'s -1 size-query sentinel, and any
 * other call passing a small negative long -- exactly as if they were
 * pointers, breaking real functionality. There is no per-call, per-slot
 * argument-type metadata available here (bios_vecs[]/xbios_vecs[]/
 * bdosmain.c's funcs[] all carry an argument *count*, never which slots
 * are pointers) to do real type-aware validation with, so this still
 * isn't that; it is deliberately narrow instead. A sign-extended 32-bit
 * value's low 32 bits are always close to 0xFFFFFFFF (e.g. -9 is
 * 0xFFFFFFF7, -1 is 0xFFFFFFFF), far above the low-32-bit span of either
 * range checked here (X86_64_KERNEL_VIRT_BASE's low 32 bits are
 * 0x80000000, and X86_64_PHYS_MAP_BASE's top 32 bits are 0xFFFF8000, not
 * 0xFFFFFFFF, putting the whole range far below any sign-extended
 * negative's value) -- so no realistic signed integer argument can ever
 * land in either, no matter how negative, while a genuine kernel address
 * always does. This also means a call with fewer than 4 real arguments,
 * whose unused slots the caller left uncleared, cannot be misclassified
 * by accident either: the odds of unrelated garbage exactly landing
 * inside one of these two narrow ranges are negligible, unlike the old
 * version's roughly 50% of the address space.
 */
static int x86_64_is_kernel_mapped_addr(UQUAD addr)
{
    if (addr >= X86_64_KERNEL_VIRT_BASE
        && addr < X86_64_KERNEL_VIRT_BASE + X86_64_KERNEL_IMAGE_SPAN_GENEROUS)
        return 1;
    if (addr >= X86_64_PHYS_MAP_BASE
        && addr < X86_64_PHYS_MAP_BASE + X86_64_PHYS_MAP_SPAN_GENEROUS)
        return 1;
    return 0;
}

void x86_64_trap_dispatch(x86_64_trap_frame_t *frame, int from_ring3)
{
    UQUAD trap_class = frame->rax >> 32;
    ULONG fn = (ULONG)frame->rax;

    if (from_ring3 &&
        (x86_64_is_kernel_mapped_addr(frame->rdi) ||
         x86_64_is_kernel_mapped_addr(frame->rsi) ||
         x86_64_is_kernel_mapped_addr(frame->rdx) ||
         x86_64_is_kernel_mapped_addr(frame->r10))) {
        /* GEMDOS has a real "bad address" error code; BIOS/XBIOS calls
         * don't share one convention (return types vary per call), so
         * -1L (already this dispatcher's own "unhandled class" value
         * below) is the closest existing precedent. */
        frame->rax = (trap_class == X86_64_TRAP_GEMDOS) ? (UQUAD)EIMBA : (UQUAD)-1L;
        return;
    }

    switch (trap_class) {
    case X86_64_TRAP_GEMDOS: {
        /* 5 slots, not 4: bdosmain.c's own dispatch (the p4 case, e.g.
         * Pexec's mode/path/tail/env) reads up to pw[4] -- GEMDOS's own
         * widest call needs all 4 real argument registers this
         * convention has (trap.h), not just the first 3.
         *
         * `long`, not LONG: see this file's own top-of-file comment on
         * why a fixed-32-bit slot here would truncate any pointer
         * argument (Pexec's path/tail/env, Cconws's string, ...) before
         * osif() ever saw it. */
        long pw[5];

        pw[0] = (long)fn;
        pw[1] = (long)frame->rdi;
        pw[2] = (long)frame->rsi;
        pw[3] = (long)frame->rdx;
        pw[4] = (long)frame->r10;
        frame->rax = (UQUAD)osif(pw);
        break;
    }
    case X86_64_TRAP_BIOS:
        /* Out-of-range returns the function number itself as the result
         * -- matching bios/arch/m68k/vectors.S's biosxbios and ARM's
         * equivalent, both of which use the same convention. */
        if (fn >= bios_ent)
            frame->rax = fn;
        else
            frame->rax = (UQUAD)((LONG (*)(UQUAD, UQUAD, UQUAD, UQUAD))bios_vecs[fn])
                             (frame->rdi, frame->rsi, frame->rdx, frame->r10);
        break;
    case X86_64_TRAP_XBIOS:
        if (fn >= xbios_ent || xbios_vecs[fn] == xbios_unimpl_addr)
            frame->rax = fn;
        else
            frame->rax = (UQUAD)((LONG (*)(UQUAD, UQUAD, UQUAD, UQUAD))xbios_vecs[fn])
                             (frame->rdi, frame->rsi, frame->rdx, frame->r10);
        break;
    default:
        frame->rax = (UQUAD)-1L;
        break;
    }
}

long x86_64_kernel_trap(long rax, long rdi, long rsi, long rdx, long r10)
{
    x86_64_trap_frame_t frame;

    frame.rax = (UQUAD)rax;
    frame.rdi = (UQUAD)rdi;
    frame.rsi = (UQUAD)rsi;
    frame.rdx = (UQUAD)rdx;
    frame.r10 = (UQUAD)r10;
    x86_64_trap_dispatch(&frame, 0);
    return (long)frame.rax;
}

/* See trap.h's own comment. */
void x86_64_bad_sysret(UQUAD bad_rip)
{
    panic("x86-64: refusing sysretq to non-canonical RIP %016lx\n",
          (unsigned long)bad_rip);
}

void x86_64_trap_init(void)
{
    UQUAD efer = x86_64_rdmsr(MSR_EFER);

    x86_64_wrmsr(MSR_EFER, efer | EFER_SCE);

    /*
     * STAR[47:32] is both the kernel CS `syscall` loads directly and the
     * base of the kernel SS it loads as that value + 8 -- exactly
     * X86_64_KERNEL_CODE_SEL immediately followed by X86_64_KERNEL_DATA_SEL
     * in this GDT already (gdt.c), so no dedicated syscall segments are
     * needed. STAR[63:48] is X86_64_USER32_CS_SEL_BASE: `sysretq` adds 8
     * for SS and 16 for CS to that same base (see gdt.h's own comment on
     * why those two land on USER_DATA_SEL/USER_CODE_SEL), forcing RPL=3
     * regardless of the low bits stored here.
     */
    x86_64_wrmsr(MSR_STAR, ((UQUAD)X86_64_KERNEL_CODE_SEL << 32)
                          | ((UQUAD)X86_64_USER32_CS_SEL_BASE << 48));

    /*
     * Not a plain C `(UQUAD)(uintptr_t)x86_64_syscall_entry`: taking the
     * address of an extern symbol that way lets the compiler pick
     * GOT-indirected addressing (`mov x86_64_syscall_entry@GOTPCREL(%rip),
     * %reg`, R_X86_64_REX_GOTPCRELX) for a symbol it can't prove is local
     * at compile time -- normally harmless (GNU ld's ELF backend relaxes
     * it back to a direct `lea` when linking a static executable), but
     * this image's objects are ELF while the final link is PE
     * (`ld -m i386pep`, see the X86_64_LD comment in the top level
     * Makefile), whose backend does not perform that relaxation or
     * otherwise populate a GOT: the load reads whatever unrelated bytes
     * happen to sit at that spot instead of a real address, and `syscall`
     * later jumps straight into that garbage. Forcing `lea` here (which
     * `-fpie` never routes through the GOT) sidesteps the whole class,
     * and -- being RIP-relative -- self-adjusts to wherever this code is
     * actually executing, so unlike idt.c's exception_stub[] (compile-time
     * data fixed to the pre-relocation load address) this needs no
     * x86_64_low_to_high() translation even though it also runs
     * post-relocation.
     */
    {
        UQUAD entry_addr;

        __asm__("lea x86_64_syscall_entry(%%rip), %0" : "=r"(entry_addr));
        x86_64_wrmsr(MSR_LSTAR, entry_addr);
    }

    /* Same GOT hazard as x86_64_syscall_entry above, for the same reason
     * (see xbios_unimpl_addr's own comment): xbios_unimpl is an external
     * symbol, so a plain C `(PFLONG)xbios_unimpl` risks a GOT-indirected
     * load this PE link never populates. */
    {
        UQUAD unimpl_addr;

        __asm__("lea xbios_unimpl(%%rip), %0" : "=r"(unimpl_addr));
        xbios_unimpl_addr = (PFLONG)(uintptr_t)unimpl_addr;
    }

    /* Cleared in RFLAGS on syscall entry: IF (bit 9), so a trap handler
     * is never itself interrupted -- consistent with interrupts already
     * being off for the whole of this milestone (startup.c/panic.c). */
    x86_64_wrmsr(MSR_FMASK, 0x200);

    /*
     * percpu.kernel_rsp is initialized to (top of stack) - 8, not the top
     * itself: trapasm.S's entry stub pushes registers immediately after
     * loading this value, on the assumption that %rsp%16==8 already (as
     * if a return address had just been pushed) -- the same convention
     * the stub documents for a normally-`call`ed caller, so that its own
     * `call x86_64_trap_dispatch` lands %rsp%16==0 right before the call,
     * matching the SysV ABI.
     */
    percpu.kernel_rsp = (UQUAD)(uintptr_t)&syscall_stack[SYSCALL_STACK_BYTES] - 8;

    /*
     * IA32_KERNEL_GS_BASE, not IA32_GS_BASE: `swapgs` (trapasm.S) swaps
     * the *live* GS base with whatever is in this MSR, so this is where
     * the value GS should hold *while running kernel code* belongs.  GS
     * itself is left alone here (already 0 from gdt_init()'s
     * reload_segments(), the right starting value for "no user context
     * has run yet"): the first `swapgs`, on the first syscall entry,
     * exchanges the two, bringing this address into GS and leaving 0 in
     * the MSR for that entry's matching exit to swap back out again.
     * Same GOT hazard as x86_64_syscall_entry above in principle, but
     * `percpu` is file-static (internal linkage): the compiler can prove
     * no other translation unit could interpose it, so it already gets a
     * direct `lea` -- confirmed by disassembly, not just assumed, given
     * how expensive assuming wrongly about this exact class of bug just
     * turned out to be.
     */
    x86_64_wrmsr(MSR_KERNEL_GS_BASE, (UQUAD)(uintptr_t)&percpu);
}
