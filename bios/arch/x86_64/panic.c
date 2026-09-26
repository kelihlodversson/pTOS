/*
 * panic.c - x86-64 exception panic dump
 *
 * The receiving end of every CPU exception isr.S's stubs catch: decodes
 * the vector, prints a full register dump (plus CR2/access-type decoding
 * for #PF) and a raw dump of the interrupted code's own stack, then
 * hangs. This milestone (#331) has no recoverable fault handling -- every
 * exception is fatal -- so there is no path back to whatever faulted.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "portab.h"
#include "io.h"
#include "earlycon.h"
#include "panic.h"
#include "pgtable.h"

/* Intel SDM Vol 3A Table 6-1, indexed by vector number. Vectors without a
 * dedicated exception keep their reserved status, since nothing in this
 * milestone can ever raise them (see idt.h). Exactly 32 entries, one per
 * installed vector (0-31): a short initializer here leaves the missing
 * tail entries as null pointers, which x86_64_exception_dispatch() would
 * then dereference.
 *
 * Each entry is compile-time-initialized data: it was a low address the
 * PE loader's relocations fixed up once at load time, but startup.c's
 * x86_64_apply_higher_half_relocations() (#343) has since re-applied the
 * same relocation table a second time, for the higher-half bias, so this
 * already holds each string's higher-half virtual address by the time a
 * panic can happen -- no translation needed at the point of use below. */
static const char *const vector_names[32] = {
    "#DE Divide Error",
    "#DB Debug",
    "NMI",
    "#BP Breakpoint",
    "#OF Overflow",
    "#BR BOUND Range Exceeded",
    "#UD Invalid Opcode",
    "#NM Device Not Available",
    "#DF Double Fault",
    "Reserved (Coprocessor Segment Overrun)",
    "#TS Invalid TSS",
    "#NP Segment Not Present",
    "#SS Stack-Segment Fault",
    "#GP General Protection Fault",
    "#PF Page Fault",
    "Reserved",
    "#MF x87 FPU Floating-Point Error",
    "#AC Alignment Check",
    "#MC Machine Check",
    "#XM SIMD Floating-Point Exception",
    "#VE Virtualization Exception",
    "#CP Control Protection Exception",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved"
};

static void print_val(const char *label, UQUAD value)
{
    earlycon_puts(label);
    earlycon_puthex(value);
    earlycon_puts("\n");
}

/* #PF's error code (Intel SDM Vol 3A 4.7) decodes the access that
 * faulted, which is often more useful for diagnosing it than the raw
 * hex value alone. */
static void print_page_fault_detail(UQUAD error_code)
{
    print_val("cr2 (faulting address)=", x86_64_read_cr2());
    earlycon_puts("  cause: ");
    earlycon_puts((error_code & 0x1) ? "protection violation" : "not present");
    earlycon_puts(", ");
    earlycon_puts((error_code & 0x2) ? "write" : "read");
    earlycon_puts(", ");
    earlycon_puts((error_code & 0x4) ? "user" : "supervisor");
    if (error_code & 0x10)
        earlycon_puts(", instruction fetch");
    if (error_code & 0x8)
        earlycon_puts(", reserved bit set in page table");
    earlycon_puts("\n");
}

/* A raw dump of the interrupted code's own stack, not a frame-pointer
 * walk: this whole codebase (not just this arch) builds with
 * -fomit-frame-pointer (see the top level Makefile's OTHERFLAGS), so
 * %rbp at fault time is not reliably a frame-chain link. Dumping words
 * from the top of the faulted stack is the honest substitute -- return
 * addresses typically do show up in the first few slots, just not
 * unambiguously labelled as such. frame->rsp is the CPU-saved value of
 * the interrupted code's own stack pointer -- saved as part of every
 * vector's frame in long mode, not just ones that switch stacks (see
 * panic.h) -- so this is accurate uniformly, #DF (IST) included: IST
 * only changes which physical stack the frame itself lands on, not
 * what value gets saved in the frame's own RSP field.
 *
 * Guarded by x86_64_addr_mapped_readable() before touching it: an
 * earlier version dereferenced frame->rsp unconditionally, reasoned as
 * "acceptable, since there is no recovery path either way" -- true when
 * every fault was necessarily a kernel-mode invariant already broken so
 * badly that nothing could be trusted, but #350 makes a genuine ring-3
 * fault with an attacker- or bug-controlled (unmapped or non-canonical)
 * user RSP an ordinary, caller-triggerable event instead. Faulting again
 * partway through this dump -- while still inside the original
 * exception handler -- risks a second, unhandled fault escalating to
 * #DF and then a triple fault, erasing the whole panic message already
 * printed above, rather than just this one section of it. */
static void dump_stack(const x86_64_exception_frame_t *frame)
{
    const UQUAD *sp = (const UQUAD *)(uintptr_t)frame->rsp;
    int i;

    earlycon_puts("raw stack dump (not an unwound trace -- see comment above):\n");

    /* Both ends, not just frame->rsp itself: the 16-qword span below
     * could still cross into an unmapped page if rsp happened to sit
     * within the last 120 bytes of a mapped region (astronomically
     * unlikely given this file's 2 MiB/1 GiB page granularity, but cheap
     * to rule out rather than assume). */
    if (!x86_64_addr_mapped_readable(frame->rsp)
        || !x86_64_addr_mapped_readable(frame->rsp + 15 * sizeof(UQUAD))) {
        earlycon_puts("  (rsp is not a confirmed-mapped address -- skipping to avoid a recursive fault)\n");
        return;
    }

    for (i = 0; i < 16; i++) {
        earlycon_puts("  ");
        earlycon_puthex((UQUAD)(uintptr_t)&sp[i]);
        earlycon_puts(": ");
        earlycon_puthex(sp[i]);
        earlycon_puts("\n");
    }
}

static NORETURN void hang(void)
{
    for (;;) {
        x86_64_cli();
        x86_64_halt();
    }
}

void x86_64_exception_dispatch(x86_64_exception_frame_t *frame)
{
    earlycon_puts("\npanic: exception ");
    earlycon_puthex(frame->vector);
    earlycon_puts(" ");
    earlycon_puts(vector_names[frame->vector]);
    earlycon_puts("\n");

    print_val("error_code=", frame->error_code);
    if (frame->vector == 14)
        print_page_fault_detail(frame->error_code);

    print_val("rip=", frame->rip);
    print_val("cs=", frame->cs);
    print_val("rflags=", frame->rflags);
    print_val("rsp=", frame->rsp);
    print_val("ss=", frame->ss);
    print_val("rax=", frame->rax);
    print_val("rbx=", frame->rbx);
    print_val("rcx=", frame->rcx);
    print_val("rdx=", frame->rdx);
    print_val("rsi=", frame->rsi);
    print_val("rdi=", frame->rdi);
    print_val("rbp=", frame->rbp);
    print_val("r8=", frame->r8);
    print_val("r9=", frame->r9);
    print_val("r10=", frame->r10);
    print_val("r11=", frame->r11);
    print_val("r12=", frame->r12);
    print_val("r13=", frame->r13);
    print_val("r14=", frame->r14);
    print_val("r15=", frame->r15);

    dump_stack(frame);

    hang();
}
