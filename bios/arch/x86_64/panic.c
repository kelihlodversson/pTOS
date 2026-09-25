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
 * Each entry is compile-time-initialized data, so it is a low address the
 * PE loader's relocations fixed up once at load time, regardless of when
 * or from where it is later read (see x86_64_low_to_high()'s own
 * comment) -- x86_64_exception_dispatch() below translates it before
 * dereferencing, since by the time a panic can happen,
 * x86_64_drop_identity_map() has already removed the low mapping this
 * would otherwise silently still depend on. */
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
 * what value gets saved in the frame's own RSP field. This can itself
 * fault (e.g. a stack overflow, or #DF's own RSP being corrupt) --
 * acceptable here, since there is no recovery path either way. */
static void dump_stack(const x86_64_exception_frame_t *frame)
{
    const UQUAD *sp = (const UQUAD *)(uintptr_t)frame->rsp;
    int i;

    earlycon_puts("raw stack dump (not an unwound trace -- see comment above):\n");
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
    earlycon_puts((const char *)(uintptr_t)
                  x86_64_low_to_high((UQUAD)(uintptr_t)vector_names[frame->vector]));
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
