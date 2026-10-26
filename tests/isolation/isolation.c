/*
 * isolation.c - regression test for the isolation of processes (#572)
 *
 * What the per-process address spaces (CONF_WITH_USER_ASPACE on ARM and
 * m68k) are for, shown from user mode, as x32's ring-3 probe does at boot:
 *
 *   - a program runs in user mode and its exit code comes back;
 *   - it cannot read or write the kernel (the kernel half, the low memory),
 *     jump into it, or execute a privileged instruction: it gets a fault,
 *     which ends that process and no other, and leaves nothing behind;
 *   - it cannot see what another process has in its private pages (image,
 *     heap, stack): the same address is its own memory, or none;
 *   - the system calls refuse a pointer it has no right to, with the
 *     documented error: GEMDOS (EIMBA), BIOS/XBIOS (EIMBA, EACCDN).
 *
 * The forbidden accesses are made by ISOPROBE.TOS (isoprobe.c), one per
 * launch.  The file passes without testing anything where the processes
 * share one address space.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <mint/basepage.h>
#include <stddef.h>

void test_isolation(void);

#ifdef TEST_WITH_USER_ASPACE

#define EACCDN  (-36L)
#define EIMBA   (-40L)
#define FAULT   0xffffL         /* what the kernel makes of a process it ends for a fault: Pterm(-1) as a word */
#define SURVIVED 7L             /* what ISOPROBE.TOS returns when nothing stopped it */
#define BROKEN   99L

#define KERNEL_HALF     0x80000000UL    /* from here up the kernel, supervisor-only */
#define KERNEL_IMAGE_PI 0x80008000UL    /* where the Raspberry Pi kernel is */
#define LOW_SYSVAR      0x000004baUL    /* _hz_200: m68k's low memory, nothing on ARM */
#define LOW_NULL        0x00000004UL
#define LOW_HOLE        0x00100000UL    /* inside the low 2 MiB, which no process maps */

static volatile unsigned long image_marker = 0x11111111UL;

/* FALSE if the query did not give the whole structure (the counters are then 0) */
static int meminfo(unsigned long *free_pages, unsigned long *processes)
{
    struct {
        unsigned long page_size, phys_pages, free_pages, processes;
        unsigned long shared_pages, shared_refs, shared_cross;
    } mi;

    *free_pages = *processes = 0;
    if (Ssystem(-3, (long)&mi, (long)sizeof(mi)) != (long)sizeof(mi))
        return 0;
    *free_pages = mi.free_pages;
    *processes = mi.processes;
    return 1;
}

/* "<what> (<rc>)" as the message of a failed check, for a result that is not what it should be */
static void expect(int ok, const char *what, long rc)
{
    static char text[160];
    unsigned long v = rc < 0 ? 0UL - (unsigned long)rc : (unsigned long)rc;
    char digits[24];
    int n = 0, m = 0;

    if (ok)
    {
        ptest_assert(1);
        return;
    }
    while (what[n] && n < (int)sizeof(text) - 32)
    {
        text[n] = what[n];
        n++;
    }
    text[n++] = ' ';
    text[n++] = '(';
    if (rc < 0)
        text[n++] = '-';
    do
        digits[m++] = (char)('0' + v % 10);
    while (v /= 10);
    while (m)
        text[n++] = digits[--m];
    text[n++] = ')';
    text[n] = 0;
    ptest_assert_msg(0, text);
}

static void put_hex(char **p, unsigned long v)
{
    int i;

    for (i = 28; i >= 0; i -= 4)
        *(*p)++ = "0123456789abcdef"[(v >> i) & 15];
}

/* runs ISOPROBE.TOS with <op> <a> <v> and returns the exit code */
static long probe(char op, unsigned long a, unsigned long v)
{
    char tail[40];
    char *p = tail + 1;

    *p++ = op;
    *p++ = ' ';
    put_hex(&p, a);
    *p++ = ' ';
    put_hex(&p, v);
    tail[0] = (char)(p - tail - 1);
    *p = 0;
    return Pexec(PE_LOADGO, "C:\\ISOPROBE.TOS", tail, 0L);
}

static void check_user_mode(void)
{
    long rc;

    rc = probe('0', 0, 0);
    expect(rc == 42L, "a program did not run and return its exit code", rc);
    /* the privileged instruction is a fault of the process, not of the kernel */
    rc = probe('p', 0, 0);
    expect(rc == FAULT, "a privileged instruction was executed", rc);
    rc = Super(0L);
    expect(rc == EACCDN, "Super() put the process in supervisor mode", rc);
}

static void check_kernel_memory(void)
{
    static const unsigned long addrs[] = { LOW_NULL, LOW_SYSVAR, LOW_HOLE, KERNEL_HALF, KERNEL_IMAGE_PI };
    unsigned i;
    long rc;

    for (i = 0; i < sizeof addrs / sizeof addrs[0]; i++)
    {
        rc = probe('r', addrs[i], 0);
        expect(rc == FAULT, "a read of kernel memory or an unmapped address was not stopped", rc);
        rc = probe('x', addrs[i], 0);
        expect(rc == FAULT, "a jump to kernel memory or an unmapped address was not stopped", rc);
    }
    rc = probe('w', LOW_SYSVAR, 0);
    expect(rc == FAULT, "a write to the low memory was not stopped", rc);
    rc = probe('w', LOW_HOLE, 0);
    expect(rc == FAULT, "a write to an unmapped address was not stopped", rc);
    rc = probe('w', KERNEL_HALF, 0);
    expect(rc == FAULT, "a write to the kernel half was not stopped", rc);
}

/*
 * The private pages of this process are at the same addresses in the next
 * one, with other pages behind them (the image, the stack) or nothing (the
 * Malloc() block, the basepage): the child either faults or has memory of
 * its own there, and never sees or changes what is here.
 */
static void check_private_pages(void)
{
    unsigned long on_stack = 0x33333333UL;
    unsigned long *heap = (unsigned long *)Malloc(64L);
    BASEPAGE *bp = _base;
    unsigned long where[4];
    unsigned long magic[4];
    unsigned i;
    long rc;

    ptest_assert_msg(heap != NULL, "Malloc() failed");
    if (!heap)
        return;
    *heap = 0x44444444UL;
    where[0] = (unsigned long)&image_marker;  magic[0] = 0x11111111UL;
    where[1] = (unsigned long)&on_stack;      magic[1] = 0x33333333UL;
    where[2] = (unsigned long)heap;           magic[2] = 0x44444444UL;
    where[3] = (unsigned long)bp;             magic[3] = *(volatile unsigned long *)bp;

    for (i = 0; i < 4; i++)
    {
        rc = probe('c', where[i], magic[i]);
        expect(rc != BROKEN, "a process read the private memory of another", rc);
        rc = probe('s', where[i], 0x22222222UL);
        expect(rc == SURVIVED || rc == FAULT, "a store to an address of another process ended in a strange way", rc);
    }
    ptest_assert_msg(image_marker == 0x11111111UL, "a process changed the image of another");
    ptest_assert_msg(on_stack == 0x33333333UL, "a process changed the stack of another");
    ptest_assert_msg(*heap == 0x44444444UL, "a process changed the heap of another");
    Mfree(heap);
}

/* the error each call gives for a pointer it may not use */
static void check_pointer_refusals(void)
{
    long fh;
    long rc;

    /* GEMDOS: a path, a buffer to read into, a buffer to write from, and one that runs over the edge */
    rc = Fopen((const char *)KERNEL_HALF, 0);
    expect(rc == EIMBA, "Fopen() took a path in the kernel", rc);
    rc = Dgetpath((char *)KERNEL_HALF, 0);
    expect(rc == EIMBA, "Dgetpath() wrote into the kernel", rc);
    rc = Dgetpath((char *)LOW_HOLE, 0);
    expect(rc == EIMBA, "Dgetpath() wrote to an unmapped address", rc);
    rc = Fwrite(1, 16L, (const void *)KERNEL_HALF);
    expect(rc == EIMBA, "Fwrite() sent out kernel memory", rc);
    rc = Fwrite(1, 0x200L, (const void *)(KERNEL_HALF - 0x100UL));
    expect(rc == EIMBA, "Fwrite() ran over the end of the user half", rc);
    rc = Pexec(PE_LOADGO, (const char *)KERNEL_HALF, "", 0L);
    expect(rc == EIMBA, "Pexec() took a path in the kernel", rc);
    fh = Fopen("C:\\RUNTESTS.TOS", 0);
    ptest_assert_msg(fh >= 0, "C:\\RUNTESTS.TOS cannot be opened");
    if (fh >= 0)
    {
        rc = Fread((int)fh, 16L, (void *)KERNEL_HALF);
        expect(rc == EIMBA, "Fread() read into the kernel", rc);
        rc = Fread((int)fh, 16L, (void *)LOW_HOLE);
        expect(rc == EIMBA, "Fread() read into an unmapped address", rc);
        Fclose((int)fh);
    }

    /* BIOS and XBIOS: a pointer into the kernel, and the calls that hand out kernel pointers */
    rc = trap_14_wl(0x06, (long)KERNEL_HALF);        /* (Setpalette() the binding drops the result) */
    expect(rc == EACCDN, "Setpalette() kept the address of a palette", rc);   /* read later, under other tables */
    rc = trap_14_wwl(0x0C, 3, (long)KERNEL_HALF);      /* Midiws() */
    expect(rc == EIMBA, "Midiws() took a string in the kernel", rc);
    rc = trap_14_wllww(0x12, (long)LOW_HOLE, 0L, -1, 0);   /* Protobt() */
    expect(rc == EIMBA, "Protobt() wrote a boot sector to an unmapped address", rc);
    rc = (long)Setexc(2, (void (*)(void))KERNEL_HALF);
    expect(rc == EACCDN, "Setexc() set a vector from user mode", rc);
    rc = trap_13_wwlwww(0x04, 0, (long)LOW_HOLE, 1, 0, 2);    /* Rwabs(read, buf, 1, 0, C:) into an unmapped address */
    expect(rc == EIMBA, "Rwabs() read a sector into an unmapped address", rc);
    rc = trap_13_wl(0x00, (long)LOW_HOLE);        /* Getmpb() */
    expect(rc == EACCDN, "Getmpb() gave out the kernel's memory descriptors", rc);
    rc = (long)Iorec(0);
    expect(rc == EACCDN, "Iorec() gave out a kernel buffer", rc);
    rc = (long)Kbdvbase();
    expect(rc == EACCDN, "Kbdvbase() gave out the kernel's vectors", rc);
    rc = trap_14_wl(0x20, (long)KERNEL_HALF);        /* Dosound() */
    expect(rc == EACCDN, "Dosound() took a table in the kernel", rc);
}

/* a process that is stopped leaves no process record and no memory, and the others go on */
static void check_faults_leave_nothing(void)
{
    unsigned long free_before, free_after, procs_before, procs_after;
    int i;
    long rc;

    /* the first round may fill caches that stay (the directory, the file buffers) */
    probe('r', KERNEL_HALF, 0);
    ptest_assert_msg(meminfo(&free_before, &procs_before), "Ssystem(S_MEMINFO) failed");
    for (i = 0; i < 8; i++)
    {
        rc = probe('r', KERNEL_HALF, 0);
        expect(rc == FAULT, "a read of kernel memory was not stopped", rc);
        rc = probe('p', 0, 0);
        expect(rc == FAULT, "a privileged instruction was not stopped", rc);
    }
    ptest_assert_msg(meminfo(&free_after, &procs_after), "Ssystem(S_MEMINFO) failed");
    ptest_assert_msg(procs_after == procs_before, "a process record is left from a process that faulted");
    ptest_assert_msg(free_after + 4 >= free_before, "memory is left from a process that faulted");
    rc = probe('0', 0, 0);
    expect(rc == 42L, "a program did not run after others faulted", rc);
}

#endif /* TEST_WITH_USER_ASPACE */

void test_isolation(void)
{
    ptest_begin("isolation (#572)");
#ifdef TEST_WITH_USER_ASPACE
    check_user_mode();
    check_kernel_memory();
    check_private_pages();
    check_pointer_refusals();
    check_faults_leave_nothing();
#endif
    ptest_pass();
}
