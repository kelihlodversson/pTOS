/*
 * trap.c - x86-64 GEMDOS/BIOS/XBIOS trap dispatch
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "portab.h"
#include "gdt.h"
#include "io.h"
#include "trap.h"

/*
 * osif() (bdos/bdosmain.c) is GEMDOS's own C-level trap #1 handler,
 * declared there to take a uniform LONG pw[] on this arch (see
 * bdosmain.c's own comment on the #if defined(__arm__) ||
 * defined(__x86_64__) split) -- not in any shared header, so declared
 * here exactly as ARM's bdos/arch/arm/rwa.S implicitly relies on it too.
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
extern long osif(LONG *pw);
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

void x86_64_trap_dispatch(x86_64_trap_frame_t *frame)
{
    UQUAD trap_class = frame->rax >> 32;
    ULONG fn = (ULONG)frame->rax;

    switch (trap_class) {
    case X86_64_TRAP_GEMDOS: {
        /* 5 slots, not 4: bdosmain.c's own dispatch (the p4 case, e.g.
         * Pexec's mode/path/tail/env) reads up to pw[4] -- GEMDOS's own
         * widest call needs all 4 real argument registers this
         * convention has (trap.h), not just the first 3. */
        LONG pw[5];

        pw[0] = (LONG)fn;
        pw[1] = (LONG)frame->rdi;
        pw[2] = (LONG)frame->rsi;
        pw[3] = (LONG)frame->rdx;
        pw[4] = (LONG)frame->r10;
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
        if (fn >= xbios_ent || xbios_vecs[fn] == (PFLONG)xbios_unimpl)
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
    x86_64_trap_dispatch(&frame);
    return (long)frame.rax;
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
