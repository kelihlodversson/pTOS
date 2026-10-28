/*
 * mmuprobe.c - the supervisor-side fixture of the MMU tests (#737)
 *
 * What the MMU is, and what a virtual address translates to, from the CPU
 * itself: PTEST on a 68030 (the PSR, and the descriptor it ends at, for the
 * physical address), PTEST on a 68040 (the MMUSR); a 68060 has neither PTEST
 * nor an MMUSR, and the tables are walked in software.  The
 * instructions are spelled as words: the file is assembled for the -m68020
 * (or -m68000) the image is built for.  See include/mmuprobe.h.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "emutos.h"
#include "processor.h"
#include "gemerror.h"   /* E_OK */
#include "mmuprobe.h"

#if CONF_WITH_MMU_PROBE

#define WINDOW      0x80000000UL    /* the physical memory in the kernel half (every higher-half m68k image) */
#define WINDOW_SIZE 0x40000000UL

#define FC_USER_DATA    1UL
#define FC_SUPER_DATA   5UL

/* PSR of the 68030 */
#define PSR_B       0x8000          /* bus error during the walk */
#define PSR_L       0x4000          /* limit violation */
#define PSR_S       0x2000          /* supervisor violation */
#define PSR_W       0x0800          /* write protected */
#define PSR_I       0x0400          /* invalid */
#define PSR_T       0x0040          /* transparent */
#define PSR_N       0x0007          /* levels walked */

/* a long format descriptor of the 68030 (bios/arch/m68k/atari_mmu.S) */
#define D_DT        0x3
#define D_PAGE      0x1
#define D_WP        0x04
#define D_CI        0x40
#define D_SUPER     0x100

/* MMUSR of the 68040 and the 68060 */
#define MMUSR_R     0x001           /* resident */
#define MMUSR_T     0x002           /* transparent */
#define MMUSR_W     0x004
#define MMUSR_S     0x080
#define MMUSR_CM(m) (((m) >> 5) & 3)

/* PTEST of the 68030: the PSR, and the address of the last descriptor */
static UWORD ptest030(ULONG va, ULONG fc, BOOL write, ULONG *desc)
{
    UWORD psr;
    ULONG d;

    if (write)
        __asm__ volatile (
            "move.l %2,%%d1\n\t"
            "move.l %3,%%a1\n\t"
            ".long  0xf0119d49\n\t"         /* ptestw d1,(a1),#7,a2 */
            "move.l %%a2,%1\n\t"
            "lea    %0,%%a0\n\t"
            ".long  0xf0106200"             /* pmove psr,(a0) */
            : "=m" (psr), "=m" (d) : "g" (fc), "g" (va) : "d1", "a0", "a1", "a2", "memory");
    else
        __asm__ volatile (
            "move.l %2,%%d1\n\t"
            "move.l %3,%%a1\n\t"
            ".long  0xf0119f49\n\t"         /* ptestr d1,(a1),#7,a2 */
            "move.l %%a2,%1\n\t"
            "lea    %0,%%a0\n\t"
            ".long  0xf0106200"             /* pmove psr,(a0) */
            : "=m" (psr), "=m" (d) : "g" (fc), "g" (va) : "d1", "a0", "a1", "a2", "memory");
    *desc = d;
    return psr;
}

/* PTEST of the 68040 and the 68060: the MMUSR (through DFC, saved and restored) */
static ULONG ptest040(ULONG va, ULONG fc, BOOL write)
{
    ULONG mmusr;

    if (write)
        __asm__ volatile (
            ".long  0x4e7a1001\n\t"         /* movec dfc,d1 */
            "move.l %2,%%d2\n\t"
            ".long  0x4e7b2001\n\t"         /* movec d2,dfc */
            "move.l %1,%%a1\n\t"
            ".word  0xf549\n\t"             /* ptestw (a1) */
            ".long  0x4e7a0805\n\t"         /* movec mmusr,d0 */
            "move.l %%d0,%0\n\t"
            ".long  0x4e7b1001"             /* movec d1,dfc */
            : "=m" (mmusr) : "g" (va), "g" (fc) : "d0", "d1", "d2", "a1", "memory");
    else
        __asm__ volatile (
            ".long  0x4e7a1001\n\t"
            "move.l %2,%%d2\n\t"
            ".long  0x4e7b2001\n\t"
            "move.l %1,%%a1\n\t"
            ".word  0xf569\n\t"             /* ptestr (a1) */
            ".long  0x4e7a0805\n\t"
            "move.l %%d0,%0\n\t"
            ".long  0x4e7b1001"
            : "=m" (mmusr) : "g" (va), "g" (fc) : "d0", "d1", "d2", "a1", "memory");
    return mmusr;
}

/*
 * The 68060 has no PTEST and no MMUSR (the instruction is a line F exception
 * there): the walk of the tables is made in software, with the descriptors of
 * the 68040 (the same format), read through the window of the kernel, and the
 * result is given as the MMUSR of a 68040 would have it.  The transparent
 * translation registers are looked at first, as the hardware does.
 */
static ULONG creg(int which)
{
    ULONG v;

    switch (which)
    {
    case 0:  __asm__ volatile (".long 0x4e7a0006\n\tmove.l %%d0,%0" : "=m" (v) : : "d0", "memory"); break;   /* movec dtt0,d0 */
    case 1:  __asm__ volatile (".long 0x4e7a0007\n\tmove.l %%d0,%0" : "=m" (v) : : "d0", "memory"); break;   /* movec dtt1,d0 */
    case 2:  __asm__ volatile (".long 0x4e7a0806\n\tmove.l %%d0,%0" : "=m" (v) : : "d0", "memory"); break;   /* movec urp,d0 */
    default: __asm__ volatile (".long 0x4e7a0807\n\tmove.l %%d0,%0" : "=m" (v) : : "d0", "memory"); break;   /* movec srp,d0 */
    }
    return v;
}

static ULONG window_long(ULONG phys)
{
    return *(volatile ULONG *)(WINDOW + phys);
}

static ULONG walk060(ULONG va, ULONG fc)
{
    ULONG d, table, w = 0;
    int i;

    for (i = 0; i < 2; i++)
    {
        ULONG t = creg(i);
        ULONG s = (t >> 13) & 3;        /* 0 user, 1 supervisor, 2 and 3 both */

        if ((t & 0x8000UL) && ((((va >> 24) ^ (t >> 24)) & ~(t >> 16) & 0xff) == 0)
            && (s >= 2 || (s == 0 && fc == FC_USER_DATA) || (s == 1 && fc == FC_SUPER_DATA)))
            return MMUSR_T | (t & 0x60UL) | (t & MMUSR_W);
    }

    table = creg(fc == FC_USER_DATA ? 2 : 3) & ~0x1ffUL;
    d = window_long(table + ((va >> 25) & 0x7f) * 4);               /* root: 7 bits, 32 MiB */
    if ((d & 3) < 2)
        return 0;
    w |= d & MMUSR_W;
    table = d & ~0x1ffUL;
    d = window_long(table + ((va >> 18) & 0x7f) * 4);               /* pointer: 7 bits, 256 KiB */
    if ((d & 3) < 2)
        return 0;
    w |= d & MMUSR_W;
    table = d & ~0xffUL;
    d = window_long(table + ((va >> 12) & 0x3f) * 4);               /* page: 6 bits, 4 KiB */
    if ((d & 3) == 2)                                               /* indirect */
        d = window_long(d & ~3UL);
    if ((d & 3) == 0)
        return 0;
    return MMUSR_R | (d & 0xfffff000UL) | (d & (MMUSR_S | 0x60UL | 0x10UL)) | w | (d & MMUSR_W);
}

/* the MMUSR for an address, by PTEST or by the walk of the 68060 */
static ULONG ptest_any(ULONG va, ULONG fc, BOOL write)
{
    return mcpu == 60 ? walk060(va, fc) : ptest040(va, fc, write);
}

static void read_030(struct mmu_probe *p)
{
    ULONG tc, crp[2], srp[2];

    __asm__ volatile ("lea %0,%%a0\n\t.long 0xf0104200" : "=m" (tc) : : "a0", "memory");        /* pmove tc,(a0) */
    __asm__ volatile ("lea %0,%%a0\n\t.long 0xf0104e00" : "=m" (crp) : : "a0", "memory");       /* pmove crp,(a0) */
    __asm__ volatile ("lea %0,%%a0\n\t.long 0xf0104a00" : "=m" (srp) : : "a0", "memory");       /* pmove srp,(a0) */
    p->tc = tc;
    p->enabled = (tc & 0x80000000UL) != 0;
    p->root[0] = crp[0];
    p->root[1] = crp[1];
    p->root[2] = srp[0];
    p->root[3] = srp[1];
}

static void read_040(struct mmu_probe *p)
{
    ULONG tc, urp, srp;

    __asm__ volatile (".long 0x4e7a0003\n\tmove.l %%d0,%0" : "=m" (tc) : : "d0", "memory");     /* movec tc,d0 */
    __asm__ volatile (".long 0x4e7a0806\n\tmove.l %%d0,%0" : "=m" (urp) : : "d0", "memory");    /* movec urp,d0 */
    __asm__ volatile (".long 0x4e7a0807\n\tmove.l %%d0,%0" : "=m" (srp) : : "d0", "memory");    /* movec srp,d0 */
    p->tc = tc;
    p->enabled = (tc & 0x8000UL) != 0;
    p->root[0] = urp;
    p->root[1] = srp;
    p->root[2] = p->root[3] = 0;
}

/* is `va` of the kernel half mapped for a supervisor read? (before it is read) */
static BOOL kernel_readable(ULONG va)
{
    if (mcpu == 30)
    {
        ULONG d;
        UWORD psr = ptest030(va, FC_SUPER_DATA, FALSE, &d);

        return (psr & (PSR_B | PSR_L | PSR_I)) == 0;
    }
    else
    {
        ULONG m = ptest_any(va, FC_SUPER_DATA, FALSE);

        return (m & (MMUSR_R | MMUSR_T)) != 0;
    }
}

LONG mmu_probe(struct mmu_probe *p)
{
    ULONG va = p->va;
    BOOL write = (p->flags & MMUPROBE_WRITE) != 0;
    ULONG fc = (p->flags & MMUPROBE_SUPER) ? FC_SUPER_DATA : FC_USER_DATA;

    p->model = 0;
    p->enabled = 0;
    p->tc = p->status = 0;
    p->root[0] = p->root[1] = p->root[2] = p->root[3] = 0;
    p->mapped = p->access = 0;
    p->phys = p->super_only = p->write_protected = p->cache_mode = 0xffffffffUL;
    p->window_ok = 0;
    p->window_word = 0;

    if (mcpu == 30)
    {
        ULONG desc, d0, d1, mask;
        UWORD psr;

        p->model = 30;
        read_030(p);
        if (!p->enabled)
            return E_OK;

        psr = ptest030(va, fc, write, &desc);
        p->status = psr;
        p->mapped = (psr & (PSR_B | PSR_L | PSR_I)) == 0;
        p->write_protected = (psr & PSR_W) != 0;
        p->super_only = (psr & PSR_S) != 0;
        p->access = p->mapped && !(psr & PSR_S) && !(write && (psr & PSR_W));
        if (p->mapped && (psr & PSR_T))
        {
            p->phys = va;               /* transparent: the address itself */
            p->cache_mode = 0xffffffffUL;
        }
        else if (p->mapped && desc >= 0x1000 && desc < WINDOW_SIZE - 8
                 && kernel_readable(WINDOW + desc))
        {
            /* the descriptor the walk ended at, read through the window: a page
             * descriptor, of the level that says how many bits the page has */
            d0 = *(volatile ULONG *)(WINDOW + desc);
            d1 = *(volatile ULONG *)(WINDOW + desc + 4);
            switch (psr & PSR_N)
            {
            case 1:  mask = 0x01ffffffUL; break;    /* TIA = 7: 32 MiB */
            case 2:  mask = 0x0003ffffUL; break;    /* TIB = 7: 256 KiB */
            default: mask = 0x00000fffUL; break;    /* TIC = 6: 4 KiB */
            }
            if ((d0 & D_DT) == D_PAGE)
            {
                p->phys = (d1 & ~mask) | (va & mask);
                p->super_only = (d0 & D_SUPER) != 0;
                p->write_protected = (d0 & D_WP) != 0;
                p->cache_mode = (d0 & D_CI) ? 2 : 0;
            }
        }
    }
    else if (mcpu == 40 || mcpu == 60)
    {
        ULONG m;

        p->model = (ULONG)mcpu;
        read_040(p);
        if (!p->enabled)
            return E_OK;

        m = ptest_any(va, fc, write);
        p->status = m;
        p->mapped = (m & (MMUSR_R | MMUSR_T)) != 0;
        p->write_protected = (m & MMUSR_W) != 0;
        p->super_only = (m & MMUSR_S) != 0;
        p->access = p->mapped && !((fc == FC_USER_DATA) && (m & MMUSR_S))
                    && !(write && (m & MMUSR_W));
        if (p->mapped)
        {
            p->phys = (m & MMUSR_T) ? va : ((m & 0xfffff000UL) | (va & 0xfffUL));
            p->cache_mode = (m & MMUSR_T) ? 0xffffffffUL : (ULONG)MMUSR_CM(m);
        }
    }
    else
        return E_OK;

    /* the same bytes by the window of the kernel, a second mapping of them (the
     * four bytes at phys, which may not be aligned, or across a page) */
    if (p->mapped && p->phys != 0xffffffffUL && p->phys < WINDOW_SIZE - 4
        && kernel_readable(WINDOW + p->phys) && kernel_readable(WINDOW + p->phys + 3))
    {
        const volatile UBYTE *w = (const volatile UBYTE *)(WINDOW + p->phys);

        p->window_word = ((ULONG)w[0] << 24) | ((ULONG)w[1] << 16) | ((ULONG)w[2] << 8) | w[3];
        p->window_ok = 1;
    }
    return E_OK;
}

#endif /* CONF_WITH_MMU_PROBE */
