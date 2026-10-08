/*
 * trap.c - x86-64 GEMDOS/BIOS/XBIOS trap dispatch
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "biosext.h"
#include "biosargs.h"
#include "blkdev.h"
#include "gdt.h"
#include "gemerror.h"
#include "io.h"
#include "pgtable.h"
#include "trap.h"

#include "../../../bdos/mem.h"
#include "../../../include/biosdefs.h"
#include "../../../bios/disk.h"

extern BOOL kproc_validate_user_range(UQUAD address, ULONG size);
extern BOOL kproc_validate_user_write(UQUAD address, ULONG size);
extern BOOL kproc_copy_from_user(void *dst, UQUAD address, ULONG size);
extern BOOL kproc_copy_to_user(UQUAD address, const void *src, ULONG size);

static void *x86_64_copy_user_buffer(UQUAD address, ULONG size)
{
    void *buffer;

    buffer = xmxalloc((long)size, MX_STRAM);
    if (!buffer)
        return NULL;
    if (!kproc_copy_from_user(buffer, address, size)) {
        xmfree(buffer);
        return NULL;
    }
    return buffer;
}

static ULONG x86_64_flopfmt_buffer_size(LONG spt)
{
    if (spt >= 1L && spt <= 10L)
        return 6250UL;  /* DD: TRACK_SIZE_DD in bios/floppy.c */
    if (spt >= 13L && spt <= 20L)
        return 12500UL; /* HD: TRACK_SIZE_HD in bios/floppy.c */
    return 0;
}

/*
 * GSX_ENTRY()/VDIPB (vdi_entry.o) are unconditional: bios/build.mk's own
 * VDI obj-y list has no CONF_WITH_* guard, matching every other arch (an
 * m68k/ARM trap#2 always reaches a VDI call unless it's one of AES's own
 * two meta-opcodes below). super()/AESPB (aes/gemsuper.c) exist only
 * #if CONF_WITH_AES, so gemsuper.h is included unconditionally (it is a
 * tiny standalone struct definition, safe to see either way) but super()
 * itself is only called under that same guard below -- the same relative-
 * include depth aes/arch/arm/gemdosifc.c already uses for its own
 * bios/*.h includes from three levels down.
 */
#include "../../../vdi/vdi_defs.h"
#include "../../../aes/gemsuper.h"

#if CONF_WITH_AES
/* Not declared in gemsuper.h itself (aes/gemsuper.c's own comment says
 * "called only from gemdosif.S" -- true on every other arch; this file is
 * x86-64's equivalent of that call site). */
extern LONG super(WORD cx, AESPB *pcrys_blk);
#endif

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
extern long osif(long *pw);          /* the trap entry: checks its arguments */
extern long osif_trusted(long *pw);  /* the same, for kernel callers */
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
 * via a plain C address-of (safe since #358 -- see the top level
 * Makefile's ARCH_X86_64 MULTILIBFLAGS comment), and compared against
 * here instead of taking xbios_unimpl's address directly at every
 * dispatch.
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
#define EFER_NXE 0x800ULL /* No-Execute page-protection enable (bit 11) */

/*
 * This CPU's per-CPU state (trap.h). Only one instance: no real
 * multi-CPU support yet (#329). trapasm.S reaches it by fixed offset via
 * %gs after `swapgs`, never by this symbol's address -- see trap.h's own
 * comment.
 */
static x86_64_percpu_t percpu;

/*
 * The initial kernel stack for syscall entry (percpu.kernel_rsp below),
 * distinct from this image's own boot-time stack (startup.c's boot_stack).
 * It serves only until the first ring-3 process is launched: every process
 * has a kernel stack of its own (include/procmem.h), which gouser()
 * (bdos/arch/x86_64/rwa.c) installs with x86_64_set_kernel_stack() before
 * entering it, and the launcher's value is restored when the process is gone.
 * 8 KiB is generous for a path that does not
 * recurse (trap.c's dispatch is a single switch, and osif()/bios_vecs[]/
 * xbios_vecs[] are shallow existing call trees on every other arch).
 */
#define SYSCALL_STACK_BYTES 8192
static UBYTE syscall_stack[SYSCALL_STACK_BYTES] __attribute__((aligned(16)));

static void trap_dispatch(x86_64_trap_frame_t *frame, int from_ring3);

/*
 * Set while a real ring-3 `syscall` is being serviced: its entry stub did
 * `swapgs`, and its exit stub undoes it.  A call that never reaches the exit
 * stub -- Pterm(), which unwinds straight back to the launching kernel
 * context -- leaves the swapped state behind; x86_64_syscall_abandoned()
 * restores it.
 */
static volatile int syscall_gs_swapped;

void x86_64_trap_dispatch(x86_64_trap_frame_t *frame, int from_ring3)
{
    if (from_ring3)
        syscall_gs_swapped = 1;
    trap_dispatch(frame, from_ring3);
    if (from_ring3)
        syscall_gs_swapped = 0;
}

/*
 * The stack the next system call switches to (percpu.kernel_rsp).  Each
 * ring-3 process has one of its own; gouser() points this at the process it
 * enters and back at the launcher's when the process exits.
 */
UQUAD x86_64_get_kernel_stack(void)
{
    return percpu.kernel_rsp;
}

void x86_64_set_kernel_stack(UQUAD rsp)
{
    percpu.kernel_rsp = rsp;
}

/*
 * The interrupted user RSP the entry stub parks in the per-CPU area for the
 * exit stub to restore: one slot, so a process suspended in a system call
 * (Pexec) loses it to every call its child makes.  gouser() saves it when it
 * launches and puts it back when the child is gone.
 */
UQUAD x86_64_get_saved_user_rsp(void)
{
    return percpu.user_rsp;
}

void x86_64_set_saved_user_rsp(UQUAD rsp)
{
    percpu.user_rsp = rsp;
}

/*
 * A process launching another from inside its own system call is in the
 * "swapped" GS state (entry did swapgs); the child must be entered in the
 * user state, and the launcher put back into the swapped one when the child
 * is gone.  gs_to_user() undoes the entry's swap if there was one and says
 * whether it did; gs_back_to_syscall() redoes it.
 */
int x86_64_gs_to_user(void)
{
    int was = syscall_gs_swapped;

    if (was) {
        __asm__ volatile ("swapgs" ::: "memory");
        syscall_gs_swapped = 0;
    }
    return was;
}

void x86_64_gs_back_to_syscall(void)
{
    __asm__ volatile ("swapgs" ::: "memory");
    syscall_gs_swapped = 1;
}

void x86_64_syscall_abandoned(void)
{
    if (syscall_gs_swapped) {
        __asm__ volatile ("swapgs" ::: "memory");
        syscall_gs_swapped = 0;
    }
}

/*
 * The argument array osif() takes, from the trap frame.  5 slots, not 4:
 * bdosmain.c's own dispatch (the p4 case, e.g. Pexec's mode/path/tail/env)
 * reads up to pw[4] -- GEMDOS's own widest call needs all 4 real argument
 * registers this convention has (trap.h), not just the first 3.
 *
 * `long`, not LONG: see this file's own top-of-file comment on why a
 * fixed-32-bit slot here would truncate any pointer argument (Pexec's
 * path/tail/env, Cconws's string, ...) before osif() ever saw it.
 */
static void gemdos_args(long *pw, const x86_64_trap_frame_t *frame)
{
    pw[0] = (long)(ULONG)frame->rax;
    pw[1] = (long)frame->rdi;
    pw[2] = (long)frame->rsi;
    pw[3] = (long)frame->rdx;
    pw[4] = (long)frame->r10;
}

static void trap_dispatch(x86_64_trap_frame_t *frame, int from_ring3)
{
    UQUAD trap_class = frame->rax >> 32;
    ULONG fn = (ULONG)frame->rax;

    /*
     * The raw argument registers are not inspected here: which of them a
     * call uses, and which of those are pointers, is only known per
     * function, and a register the call does not use is none of this
     * function's business.  Checking that a buffer or string pointer lies
     * in the calling process's own address space is done per call, for the
     * pointers that call uses (GEMDOS: bdos/uaccess.c, below); BIOS and
     * XBIOS calls still trust theirs unless they check it themselves (#352).
     */

    /* Trap class 2 (GEM) is not safe from ring 3 yet -- it dereferences
     * AESPB/VDIPB pointers (rdi) directly without validation or
     * translation, which would fault the kernel if rdi points at
     * unmapped/kernel memory from user mode. Reject the entire class
     * from ring 3 until the #352 copy/validation path exists. */
    if (from_ring3 && trap_class == X86_64_TRAP_GEM) {
        frame->rax = (UQUAD)-1L;
        return;
    }

    switch (trap_class) {
    case X86_64_TRAP_GEMDOS: {
        long pw[5];

        /* Only a ring-3 `syscall` gets here: kernel code calls GEMDOS through
         * x86_64_kernel_trap() (below), which does not use this function for
         * it.  The caller's pointers are checked by osif() itself. */
        gemdos_args(pw, frame);
        frame->rax = (UQUAD)osif(pw);
        break;
    }
    case X86_64_TRAP_BIOS:
        /* Out-of-range returns the function number itself as the result
         * -- matching bios/arch/m68k/vectors.S's biosxbios and ARM's
         * equivalent, both of which use the same convention. */
        if (fn >= bios_ent)
            frame->rax = fn;
        else if (from_ring3 && fn == 5 && (long)frame->rsi != -1L) {
            /* Setexc() with a new vector: the kernel would later call that
             * address in ring 0 (etv_term from xterm(), the critical-error
             * and timer vectors, the exception table).  Ring 3 may only
             * query a vector, never install one. */
            frame->rax = (UQUAD)-1L;
        } else if (from_ring3 && fn == 4) {
            struct x32_bios_lrwabs_args wire;
            struct bios_lrwabs_args native;
            ULONG bytes;
            void *buffer;
            LONG result;

            if (!kproc_copy_from_user(&wire, frame->rdi, sizeof(wire))) {
                frame->rax = (UQUAD)-1L;
                break;
            }
            if (!wire.adr && wire.drive >= 0 && wire.drive < NUMFLOPPIES) {
                /* TOS uses a null buffer to update floppy media-change state. */
                native.r_w = wire.r_w;
                native.adr = NULL;
                native.numb = wire.numb;
                native.first = wire.first;
                native.drive = wire.drive;
                native.lfirst = wire.lfirst;
                frame->rax = (UQUAD)((LONG (*)(struct bios_lrwabs_args *))bios_vecs[fn])(&native);
                break;
            }
            if (wire.numb <= 0 || wire.numb > 0x7fffL
                || !blkdev_rwabs_buffer_size(wire.r_w, (WORD)wire.numb,
                                              (WORD)wire.drive, &bytes)) {
                frame->rax = (UQUAD)-1L;
                break;
            }
            /* A buffer the device fills is also written back to the caller:
             * it must be writable up front, not discovered not to be after
             * the device operation has already happened. */
            if (!(((wire.r_w & RW_RW) == RW_READ)
                  ? kproc_validate_user_write(wire.adr, bytes)
                  : kproc_validate_user_range(wire.adr, bytes))) {
                frame->rax = (UQUAD)-1L;
                break;
            }
            buffer = x86_64_copy_user_buffer(wire.adr, bytes);
            if (!buffer) {
                frame->rax = (UQUAD)ENSMEM;
                break;
            }
            native.r_w = wire.r_w;
            native.adr = buffer;
            native.numb = wire.numb;
            native.first = wire.first;
            native.drive = wire.drive;
            native.lfirst = wire.lfirst;
            result = ((LONG (*)(struct bios_lrwabs_args *))bios_vecs[fn])(&native);
            if ((wire.r_w & RW_RW) == RW_READ
                && !kproc_copy_to_user(wire.adr, buffer, bytes))
                result = ERR;
            xmfree(buffer);
            frame->rax = (UQUAD)result;
        } else if (fn == 5)
            /* BIOS function 5 is Setexc(). bios.c's setexc() was widened
             * to return a native `long` (not the fixed-32-bit LONG every
             * other BIOS call still uses) specifically so that its
             * etv_timer/etv_critic/etv_term path -- vecnum 0x100-0x102 --
             * can hand back a real x86-64 function pointer (see setexc()'s
             * own comment in bios.c). Calling it through the same
             * LONG-returning function-pointer type the other entries below
             * use would truncate that pointer straight back to 32 bits at
             * this call boundary, undoing the widening before it ever
             * reaches frame->rax: Setexc(0x100, -1)/Setexc(0x102, -1) would
             * hand timer_init()/xterm() a garbage callback address. The
             * historical 32-bit-per-slot low vector-table path (every other
             * vecnum) is unaffected either way, since that table can't hold
             * a genuine 64-bit pointer regardless of this cast (#351). */
            frame->rax = (UQUAD)((long (*)(UQUAD, UQUAD, UQUAD, UQUAD))bios_vecs[fn])
                             (frame->rdi, frame->rsi, frame->rdx, frame->r10);
        else
            frame->rax = (UQUAD)((LONG (*)(UQUAD, UQUAD, UQUAD, UQUAD))bios_vecs[fn])
                             (frame->rdi, frame->rsi, frame->rdx, frame->r10);
        break;
    case X86_64_TRAP_XBIOS:
        if (fn >= xbios_ent || xbios_vecs[fn] == xbios_unimpl_addr)
            frame->rax = fn;
        else if (from_ring3 && (fn == 8 || fn == 9 || fn == 19)) {
            struct x32_xbios_flop_io_args wire;
            struct xbios_flop_io_args native;
            ULONG bytes;
            void *buffer;
            LONG result;

            if (!kproc_copy_from_user(&wire, frame->rdi, sizeof(wire))
                || wire.count < -32768L
                || wire.count > 0x7fffL
                || (fn != 19 && wire.count <= 0)) {
                frame->rax = (UQUAD)-1L;
                break;
            }
            if (fn == 19) {
                /* flopver() reserves the second sector for DMA and writes
                 * one WORD per bad sector plus a terminator in the first.
                 * Its fixed layout cannot safely represent more than the
                 * first sector's worth of bad-sector entries. */
                if (wire.count > SECTOR_SIZE / sizeof(WORD) - 1)
                    bytes = 0;
                else
                    bytes = 2UL * SECTOR_SIZE;
            } else {
                bytes = (ULONG)wire.count * SECTOR_SIZE;
            }
            if (!bytes) {
                frame->rax = (UQUAD)-1L;
                break;
            }
            /* Floprd/Flopver fill the buffer and it is copied back: it must be
             * writable before the device operation; Flopwr only reads it. */
            if (!(fn != 9 ? kproc_validate_user_write(wire.buf, bytes)
                          : kproc_validate_user_range(wire.buf, bytes))) {
                frame->rax = (UQUAD)-1L;
                break;
            }
            buffer = x86_64_copy_user_buffer(wire.buf, bytes);
            if (!buffer) {
                frame->rax = (UQUAD)ENSMEM;
                break;
            }
            native.buf = buffer;
            native.filler = wire.filler;
            native.dev = wire.dev;
            native.sect = wire.sect;
            native.track = wire.track;
            native.side = wire.side;
            native.count = wire.count;
            result = ((LONG (*)(struct xbios_flop_io_args *))xbios_vecs[fn])(&native);
            if (fn != 9 && !kproc_copy_to_user(wire.buf, buffer, bytes))
                result = ERR;
            xmfree(buffer);
            frame->rax = (UQUAD)result;
        } else if (from_ring3 && fn == 10) {
            struct x32_xbios_flopfmt_args wire;
            struct xbios_flopfmt_args native;
            void *buffer;
            void *skew;
            ULONG skew_bytes;
            ULONG bytes;
            WORD interlv;
            LONG result;

            if (!kproc_copy_from_user(&wire, frame->rdi, sizeof(wire))
                || !(bytes = x86_64_flopfmt_buffer_size(wire.spt))
                /* the buffer is copied back after the format: writable now */
                || !kproc_validate_user_write(wire.buf, bytes)) {
                frame->rax = (UQUAD)-1L;
                break;
            }
            interlv = (WORD)wire.interlv;
            if (interlv < 0
                && !kproc_validate_user_range(wire.skew, (ULONG)wire.spt * sizeof(WORD))) {
                frame->rax = (UQUAD)-1L;
                break;
            }
            buffer = x86_64_copy_user_buffer(wire.buf, bytes);
            if (!buffer) {
                frame->rax = (UQUAD)ENSMEM;
                break;
            }
            skew = NULL;
            skew_bytes = (ULONG)wire.spt * sizeof(WORD);
            if (interlv < 0) {
                skew = x86_64_copy_user_buffer(wire.skew, skew_bytes);
                if (!skew) {
                    xmfree(buffer);
                    frame->rax = (UQUAD)ENSMEM;
                    break;
                }
            }
            native.buf = buffer;
            native.skew = skew;
            native.dev = wire.dev;
            native.spt = wire.spt;
            native.track = wire.track;
            native.side = wire.side;
            native.interlv = interlv;
            native.magic = wire.magic;
            native.virgin = wire.virgin;
            result = ((LONG (*)(struct xbios_flopfmt_args *))xbios_vecs[fn])(&native);
            if (!kproc_copy_to_user(wire.buf, buffer, bytes))
                result = ERR;
            if (skew)
                xmfree(skew);
            xmfree(buffer);
            frame->rax = (UQUAD)result;
        } else if (from_ring3 && fn == 15) {
            struct x32_xbios_rsconf_args wire;
            struct xbios_rsconf_args native;

            if (!kproc_copy_from_user(&wire, frame->rdi, sizeof(wire))) {
                frame->rax = (UQUAD)-1L;
                break;
            }
            native.baud = wire.baud;
            native.ctrl = wire.ctrl;
            native.ucr = wire.ucr;
            native.rsr = wire.rsr;
            native.tsr = wire.tsr;
            native.scr = wire.scr;
            frame->rax = (UQUAD)((LONG (*)(struct xbios_rsconf_args *))xbios_vecs[fn])(&native);
        } else
            frame->rax = (UQUAD)((LONG (*)(UQUAD, UQUAD, UQUAD, UQUAD))xbios_vecs[fn])
                             (frame->rdi, frame->rsi, frame->rdx, frame->r10);
        break;
    case X86_64_TRAP_GEM:
        /*
         * Real trap#2, AES/VDI's shared entry: fn is the opcode
         * (desk/gembind.c's gem() puts 0xC8 there, aes/gsx2.c's gsx2()
         * puts 0x73, ...) and rdi is the pointer the caller built (an
         * AESPB for AES's own opcodes, a VDIPB for everything else) --
         * exactly the ARM/m68k convention, just without a separate
         * assembly trap-entry stub: this dispatcher already is that
         * entry point for every trap class.
         *
         * ARM's own aestrap (aes/arch/arm/gemdosif.S) treats 0xC8/0xC9 as
         * AES's own two meta-opcodes and sends everything else straight
         * to the previously-installed VDI trap-2 handler (savetrap2);
         * there is no equivalent chaining needed here since this arch has
         * no earlier VDI-only trap-2 handler to chain to in the first
         * place -- GSX_ENTRY() below is simply the unconditional "not one
         * of AES's two meta-opcodes" fallback, same as ARM's chain target
         * ultimately resolves to.
         */
        if (fn == 0xC8 || fn == 0xC9)
#if CONF_WITH_AES
            frame->rax = (UQUAD)super((WORD)fn, (AESPB *)(uintptr_t)frame->rdi);
#else
            /* AES isn't built: these two opcodes can't occur legitimately
             * (nothing on this image ever issues them), but a stray call
             * with the right opcode should still get a harmless answer
             * rather than being misrouted into GSX_ENTRY() as if it were
             * ordinary VDI opcode 0xC8/0xC9. */
            frame->rax = (UQUAD)-1L;
#endif
        else
            frame->rax = (UQUAD)GSX_ENTRY((int)fn, (VDIPB *)(uintptr_t)frame->rdi);
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
    if ((UQUAD)rax >> 32 == X86_64_TRAP_GEMDOS) {
        /* kernel code calling GEMDOS as an internal API passes its own,
         * kernel, pointers: no user-pointer checks (osif() has them) */
        long pw[5];

        gemdos_args(pw, &frame);
        return osif_trusted(pw);
    }
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

    /*
     * One combined read-modify-write, not two separate MSR writes: a
     * second wrmsr() using a stale `efer` read before the first one
     * landed would clobber whichever bit it did not itself set. NXE
     * readies the PTE_NX (bit 63) page-table protection bit #334's
     * per-process page tables will need for non-executable data/stack
     * segments; SCE is the pre-existing syscall/sysret enable.
     */
    x86_64_wrmsr(MSR_EFER, efer | EFER_SCE | EFER_NXE);

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
     * Plain C address-of, safe since #358: this file (like the rest of
     * the kernel proper, everything that only ever runs post-relocation)
     * is compiled -fno-pic -mcmodel=large, not -fpie, specifically so
     * that taking the address of an extern symbol like
     * x86_64_syscall_entry is an ordinary absolute 64-bit load/immediate,
     * not GOT-indirected addressing that this image's PE link
     * (`ld -m i386pep`) cannot correctly populate for. See the top level
     * Makefile's ARCH_X86_64 MULTILIBFLAGS comment and #358 for the full
     * story; this file used to work around it here with a forced
     * `lea sym(%rip)` -- not needed anymore now that nothing on this arch
     * is GOT-indirected in the first place. Unrelated to this: some
     * other x86-64 address computations still need explicit translation
     * via x86_64_low_to_high() (startup.c's own entry_high/stack_top_high,
     * computed from a RIP-relative `&function` while still running at
     * the low, pre-jump address) or need none at all (idt.c's
     * exception_stub[], compile-time data whose relocation entries
     * x86_64_apply_higher_half_relocations() already retargets straight
     * to the higher half) -- neither of those is a GOT concern either way.
     */
    x86_64_wrmsr(MSR_LSTAR, (UQUAD)(uintptr_t)x86_64_syscall_entry);

    /* Same reasoning as x86_64_syscall_entry above: xbios_unimpl is an
     * external symbol, and a plain C `(PFLONG)xbios_unimpl` is safe now
     * that this file is no longer GOT-indirected (#358). */
    xbios_unimpl_addr = (PFLONG)(uintptr_t)xbios_unimpl;

    /* Clear IF (bit 9) on syscall entry. trapasm.S re-enables it only after
     * switching off the user stack and saving a complete register frame. */
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
     * `percpu` is file-static (internal linkage), so even before #358's
     * fix this was never at risk of the GOT-indirection hazard
     * x86_64_syscall_entry above used to need a workaround for: the
     * compiler could already prove no other translation unit could
     * interpose it, so it always got a direct `lea` here -- confirmed by
     * disassembly at the time, not just assumed, given how expensive
     * assuming wrongly about this exact class of bug turned out to be.
     */
    x86_64_wrmsr(MSR_KERNEL_GS_BASE, (UQUAD)(uintptr_t)&percpu);
}

/* See trap.h's own comment. Selectors are embedded as asm-immediate
 * literals via XSTR, same technique gdt.c's reload_segments() already
 * uses, rather than passed as operands: doing so needs no register to
 * hold them across the pushes leading up to iretq. */
#define STR(x) #x
#define XSTR(x) STR(x)

void x86_64_enter_user(UQUAD pml4_phys, UQUAD entry_rip, UQUAD user_rsp,
                       UQUAD basepage, UQUAD entry_type)
{
    __asm__ volatile (
        "mov %0, %%cr3\n\t"
        "mov %3, %%rdi\n\t"
        "mov %4, %%rsi\n\t"
        "pushq $" XSTR(X86_64_USER_DATA_SEL) "\n\t" /* SS */
        "pushq %1\n\t"                              /* RSP */
        "pushq $0x202\n\t"                          /* RFLAGS: IF enabled */
        "pushq $" XSTR(X86_64_USER_CODE_SEL) "\n\t" /* CS */
        "pushq %2\n\t"                              /* RIP */
        "iretq"
        :
        : "r"(pml4_phys), "r"(user_rsp), "r"(entry_rip),
          "r"(basepage), "r"(entry_type)
        : "rdi", "rsi", "memory"
    );
    __builtin_unreachable();
}
