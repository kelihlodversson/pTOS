/*
 * proc.c - process management routines
 *
 * Copyright (C) 2001 Lineo, Inc. and Authors:
 *               2002-2022 The EmuTOS development team
 *
 *  KTB     Karl T. Braun (kral)
 *  MAD     Martin Doering
 *  ACH     ???
 *  LVL     Laurent Vogel
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/* #define ENABLE_KDEBUG */

#include "emutos.h"
#include "bdosdefs.h"
#include "fs.h"
#include "mem.h"
#include "proc.h"
#include "kproc.h"
#include "gemerror.h"
#include "biosbind.h"
#include "string.h"
#include "biosext.h"
#include "asm.h"
#include "tosvars.h"
#include "has.h"
#include "../bios/vectors.h"
#if ARCH_ARM
#include "arch/arm/entry.h"
#endif
#if CONF_WITH_PLUGGABLE_FS
#include "pfs.h"
#endif


/*
 * defines
 */
#define TPASIZE_QUANTUM (128*1024L)     /* see alloc_tpa() */

#ifdef __x86_64__
#include "procmem.h"
#endif

/*
 * forward prototypes
 */

static void ixterm( PD *r );
static WORD envsize( char *env );
static void init_pd_fields(PD *p, char *tail, long max, char *envptr);
static void init_pd_files(PD *p);
static char *alloc_env(ULONG flags, char *v);
static UBYTE *alloc_tpa(ULONG flags,LONG needed,LONG *avail);
static void proc_go(PD *p);

/*
 * global variables
 */

PD      *run;           /* ptr to PD for current process */

#ifdef __x86_64__
/* Kernel code marks a freshly created ROM basepage here before Pexec(). */
static PD *x86_64_kernel_code_pd;

void x86_64_mark_kernel_code_pd(PD *p)
{
    x86_64_kernel_code_pd = p;
}

/*
 * Per-launch setup that can fail for lack of memory, done where Pexec()
 * can still return ENSMEM: a kernel-code process runs in ring 0 and needs
 * nothing; a real one gets its ring-3 address space here, owned by its
 * KPROC record from now on (see kproc_prepare_user()).
 */
/* Pexec() from ring 3 (see doc/x86_64-address-space.txt, "Basepages in
 * ring 3"): the caller owns what it creates, and may only launch what the
 * kernel made for it. */
static BOOL x86_64_ring3_caller(void)
{
    return kproc_user_aspace(run) != NULL;
}

static LONG x86_64_check_launch(PD *p)
{
    return x86_64_ring3_caller() ? kproc_check_launch(p, run) : E_OK;
}

static void x86_64_hand_over(PD *p)
{
    kproc_set_creator(p, run);
    if (x86_64_ring3_caller())
        kproc_hand_over(run, p);
}

static BOOL x86_64_prepare_launch(PD *p)
{
    if (x86_64_kernel_code_pd == p)
        return TRUE;
    return kproc_prepare_user(p, run);
}

static void release_pd_files(PD *r);

/* Drops the kernel-private record keyed by a freed process block.  A
 * basepage that was created (inheriting its parent's standard handles and
 * current directories in init_pd_files()) but never launched still holds
 * those references, and nothing else will ever release them: do it now,
 * exactly once -- the record is the proof it has not been done. */
static void x86_64_release_block(void *base)
{
    if (kproc_discard((PD *)base))
        release_pd_files((PD *)base);
    /* a block lent to a ring-3 launcher goes back, but only after its
     * basepage has been read: that launcher's page tables are loaded */
    kproc_unborrow(base);
}

/* Ptermres: an unlaunched child basepage the terminating process owns stays
 * allocated, but its record goes, like reserve_blocks() drops it.  The record
 * is also what says its inherited file and directory references are still
 * held, so they are released now and cleared from the basepage: whoever
 * launches or frees it later must not release them a second time. */
static void x86_64_drop_child_record(void *base)
{
    PD *child = (PD *)base;
    int i;

    if (kproc_discard(child)) {
        release_pd_files(child);
        for (i = 0; i < NUMSTD; i++)
            child->p_uft[i] = 0;
        for (i = 0; i < NUMCURDIR; i++)
            child->p_curdir[i] = 0;
    }
    kproc_unborrow(base);       /* lent to a ring-3 launcher while it was built */
}

/* Pending Ptermres: applied by xterm() once the process's address space is
 * gone, because only then can the unused tail of its block be released. */
static PD *x86_64_resident_pd;
static ULONG x86_64_resident_len;

void x86_64_make_resident(PD *p, ULONG keep_bytes)
{
    /* The process's own block is not passed to the callback below, and its
     * tail cannot be given back while the launcher still has it mapped: return
     * what a ring-3 launcher borrowed for it first (its environment goes
     * through the callback). */
    kproc_unborrow(p);
    x86_64_procmem_keep(p, keep_bytes, x86_64_drop_child_record);
}

/*
 * Frees every process allocation `p` owns (the x86-64 counterpart of
 * free_all_owned() for the MPB lists), dropping KPROC records of any
 * basepages among them first.
 */
void x86_64_free_owned(PD *p)
{
    x86_64_procmem_free_owned(p, x86_64_release_block);
}

/* Mfree() of a process allocation: same release, one block. */
long x86_64_procmem_mfree(void *addr)
{
    /* A block that a live process still has mapped (its own environment or
     * basepage, say) cannot be freed from under it, and tearing down the
     * address space the caller is running in would be worse: refuse, and
     * leave the KPROC record alone. */
    /* A ring-3 caller frees what it owns and nothing else: the blocks of a
     * process it launched are not its, nor those a peer retained. */
    if (x86_64_ring3_caller() && x86_64_procmem_owner(addr) != run)
        return EACCDN;
    if (x86_64_procmem_pins(addr) > kproc_borrow_count(run, addr))
        return EACCDN;
    x86_64_release_block(addr);     /* also returns it if it was borrowed */
    return x86_64_procmem_free(addr) ? E_OK : EIMBA;
}

BOOL x86_64_take_kernel_code_pd(PD *p)
{
    if (x86_64_kernel_code_pd != p)
        return FALSE;

    x86_64_kernel_code_pd = NULL;
    return TRUE;
}
#endif

#ifndef __x86_64__
#define x86_64_ring3_caller() FALSE
#define x86_64_check_launch(p) E_OK
#define x86_64_hand_over(p) do { } while (0)
#define x86_64_prepare_launch(p) TRUE
#endif

/*
 * internal variables
 */

/*
 * common supervisor stack for all processes: every trap #1 call from every
 * process runs its BDOS-side C code on this stack (see other_sp in
 * proc_go() and its use in gouser(), bdos/arch/arm/rwa.S). On ARM, AAPCS
 * requires sp to be 8-byte aligned at every public interface, which gcc's
 * auto-vectorizer (-mfpu=neon-vfpv4) relies on, but a plain WORD array
 * carries no alignment guarantee beyond the compiler's incidental default.
 */
#if ARCH_ARM
static WORD    supstk[SUPSIZ] __attribute__((aligned(8)));
#else
static WORD    supstk[SUPSIZ]; /* common sup stack for all processes */
#endif
static jmp_buf bakbuf;         /* longjmp buffer */


/*
 * memory internal routines
 *
 * These violate the encapsulation of the memory internal structure.
 * Could perhaps better go into a memory module; however, moving them to
 * e.g. iumem.c would cost about 40 bytes of ROM space in the 192K ROMs.
 */
static void free_all_owned(PD *p, MPB *mpb);
static void reserve_blocks(PD *pd, MPB *mpb);

/* reserve blocks, i.e. remove them from the allocated list
 *
 * the memory associated with these blocks will remain permanently
 * allocated - this is used by Ptermres()
 */
static void reserve_blocks(PD *p, MPB *mpb)
{
    MD *m, **q;

    for (m = *(q = &mpb->mp_mal); m; m = *q) {
        if (m->m_own == p) {
            /* the block is kept allocated rather than freed, so freeit()
             * will not run; drop child process records here. xterm()
             * removes p's record after calling its termination handler. */
            if ((PD *)m->m_start != p)
                kproc_destroy((PD *)m->m_start);
            *q = m->m_link; /* pouf ! like magic */
            xmfremd(m);
        } else {
            q = &m->m_link;
        }
    }
}

/* free each item in the allocated list, that is owned by 'p' */
static void free_all_owned(PD *p, MPB *mpb)
{
    MD *m, *next;

    for (m = mpb->mp_mal; m; m = next) {
        next = m->m_link;
        if (m->m_own == p)
            freeit(m,mpb);
    }
}

/*
 * ixterm - terminate a process
 *
 * terminate process with PD 'r'.
 *
 * @r: PD of process to terminate
 */
/*
 * release_pd_files - drop the file handles and current-directory references
 * a process holds.  Called when the process terminates and, on x86-64, for
 * a basepage discarded before it ever ran.
 */
static void release_pd_files(PD *r)
{
    WORD h;
    WORD i;

    /* check the standard devices in both file tables  */

    for (i = 0; i < NUMSTD; i++)
        if ((h = r->p_uft[i]) > 0)
            xclose(h);

    for (i = 0; i < OPNFILES; i++)
        if (r == sft[i].f_own)
            xclose(i+NUMSTD);


    /* decrement usage counts for current directories */

    for (i = 0; i < NUMCURDIR; i++)
    {
        if ((h = r->p_curdir[i]) != 0)
            decr_curdir_usage(h);
    }

#if CONF_WITH_PLUGGABLE_FS
    /*
     * p_curdir[] above indexes fs/pfs.c's own directory table instead of
     * dirtbl[] for a pluggable drive - decr_curdir_usage() is harmless
     * either way (it no-ops below NCURDIR's dirtbl[] range and dirtbl[]
     * stays empty for the lifetime of a pluggable-fs build, since
     * nothing calls incr_curdir_usage() once this option is on), but the
     * pluggable table still needs its own matching cleanup so its slots
     * (and any open Fsfirst/Fsnext searches this process owned) don't
     * leak.
     */
    pfs_proc_exit(r);
#endif
}

static void ixterm(PD *r)
{
    release_pd_files(r);

    /* free each item in the allocated list that is owned by 'r' */

    free_all_owned(r, &pmd);
#if CONF_WITH_ALT_RAM
    if (has_alt_ram)
        free_all_owned(r, &pmdalt);
#endif
#ifdef __x86_64__
    x86_64_free_owned(r);
#endif
}


/*
 * envsize - determine size of env area
 *
 * counts bytes starting at 'env' up to and including the terminating
 * double null.
 */
static WORD envsize(char *env)
{
    char *e;
    WORD cnt;

    for (e = env, cnt = 0; !(*e == '\0' && *(e+1) == '\0'); ++e, ++cnt)
        ;

    return cnt + 2;         /*  count terminating double null  */
}


/*
 * xexec - (Pexec - 0x4b) execute a new process
 *
 * load&go(cmdlin,cmdtail), load/nogo(cmdlin,cmdtail), justgo(psp)
 * create psp - user receives a memory partition
 *
 * @flg: 0: load&go, 3: load/nogo, 4: justgo, 5: create psp, 6: ???
 * @s:   command
 * @t:   tail
 * @v:   environment
 */

/* these variables are used to avoid the following warning:
 * variable `foo' might be clobbered by `longjmp' or `vfork'
 */
static PD *cur_p;

long xexec(WORD flag, char *path, char *tail, char *env)
{
    PD *p, *owner;
    PGMHDR01 hdr;
    char *env_ptr;
    ULONG hdrflags;
    LONG rc;
    long max, needed;
    FH fh;

    KDEBUG(("BDOS xexec: flag or mode = %d\n",flag));


    /* first branch - actions that do not require loading files */
    switch(flag) {
#if DETECT_NATIVE_FEATURES
    case PE_RELOCATE:   /* internal use only, see bootstrap() in bios/bios.c */
        p = (PD *) tail;
        rc = kpgm_relocate(p, (long)path);
        if (rc) {
            KDEBUG(("BDOS xexec: kpgm_reloc returned %ld (0x%lx)\n",rc,rc));
            return rc;
        }

        /* invalidate instruction cache for the TEXT segment only
         * programs that jump into their DATA, BSS or HEAP are kindly invited
         * to do their cache management themselves.
         */
        invalidate_instruction_cache( p+1, p->p_tlen);

        return (long)p;
#endif
    case PE_BASEPAGE:           /* just create a basepage */
        path = (char *) 0L;     /* (same as basepage+flags with flags set to zero) */
        FALLTHROUGH;
    case PE_BASEPAGEFLAGS:      /* create a basepage, respecting the flags */
        hdrflags = (ULONG)path;
        env_ptr = alloc_env(hdrflags, env);
        if (env_ptr == NULL) {
            KDEBUG(("BDOS xexec: no memory for environment\n"));
            return ENSMEM;
        }
        p = (PD *)alloc_tpa((ULONG)path,sizeof(PD),&max);

        if (p == NULL) {    /* not even enough memory for basepage */
            xmfree(env_ptr);
            KDEBUG(("BDOS xexec: No memory for basepage\n"));
            return ENSMEM;
        }

        /* memory ownership */
        set_owner(p, run);
        set_owner(env_ptr, run);

        /* initialize the PD */
        init_pd_fields(p, tail, max, env_ptr);
        if (!kproc_create(p)) {
            xmfree(env_ptr);
            xmfree(p);
            return ENSMEM;
        }
        p->p_flags = (ULONG)path;   /* set the flags */
        init_pd_files(p);
        x86_64_hand_over(p);

        return (long)p;
    case PE_GOTHENFREE:
        p = (PD *) tail;
        rc = x86_64_check_launch(p);
        if (rc)
            return rc;
        /* The allocation can fail; retain the parent's ownership until it
         * succeeds so an ENSMEM return leaves the retained basepage freeable. */
        if (!kproc_create(p) || !x86_64_prepare_launch(p))
            return ENSMEM;
        /* set the owner of the memory to be this process */
        set_owner(p, p);
        set_owner(USERPTR_TO_PTR(p->p_env), p);
        FALLTHROUGH;
    case PE_GO:
        p = (PD *) tail;
        if (flag == PE_GO && (rc = x86_64_check_launch(p)) != E_OK)
            return rc;
        if (flag == PE_GO && (!kproc_create(p) || !x86_64_prepare_launch(p)))
            return ENSMEM;
        proc_go(p);
        /*
         * "should not return ?": on m68k/ARM, proc_go()/gouser() (rwa.S)
         * never actually reach this line for a reentrant launch (e.g.
         * aes/gemshlib.c's aes_run_rom_program()) -- gouser()'s own trap-
         * return mechanism (a raw asm jump, invisible to this C code)
         * delivers control straight back to whichever trap #1 call site
         * originally invoked Pexec(), with D0 already holding the exit
         * code xterm()'s own `run->p_dreg[0] = rc;` supplied, bypassing
         * this function's own C-level return entirely.
         *
         * On x86-64, though, proc_go()/gouser() (rwa.c) are ordinary
         * nested C calls with no trap involved (#334's own "no re-trap
         * needed for kernel-internal callers" simplification) -- so this
         * line IS genuinely reached there, once a reentrant launch's own
         * Pterm()/Pterm0() unwinds back via gouser()'s setjmp()/
         * longjmp() pair. By then, xterm() has already reassigned `run`
         * to the parent (this same call's own caller) and stashed the
         * exit code in its p_dreg[0], so returning that instead of
         * (long)p propagates the exit code exactly like the trap-based
         * archs' D0 does -- harmless on m68k/ARM themselves, since they
         * never execute this statement in the first place.
         */
        return run->p_dreg[0];
    case PE_LOADGO:
    case PE_LOAD:
        break;
    default:
        return EINVFN;
    }

    /* we now need to load a file */
    KDEBUG(("BDOS xexec: trying to find %s\n",path));
    if (ixsfirst(path,0,0L)) {
        KDEBUG(("BDOS xexec: command %s not found!!!\n",path));
        return EFILNF;      /*  file not found      */
    }

    /* load the header - if I/O error occurs now, the longjmp in rwabs will
     * jump directly back to bdosmain.c, which is not a problem because
     * we haven't allocated anything yet.
     */
    rc = kpgmhdrld(path, &hdr, &fh);
    if (rc) {
        KDEBUG(("BDOS xexec: kpgmhdrld returned %ld (0x%lx)\n",rc,rc));
        return rc;
    }

    /* allocate the environment first, depending on memory policy */
    env_ptr = alloc_env(hdr.h01_flags, env);
    if (env_ptr == NULL) {
        KDEBUG(("BDOS xexec: no memory for environment\n"));
        xclose(fh);
        return ENSMEM;
    }

    /* allocate the basepage depending on memory policy */
    needed = hdr.h01_tlen + hdr.h01_dlen + hdr.h01_blen + sizeof(PD);
    p = (PD *)alloc_tpa(hdr.h01_flags,needed,&max);

    /* if failed, free env_ptr and return */
    if (p == NULL) {
        KDEBUG(("BDOS xexec: no memory for TPA\n"));
        xmfree(env_ptr);
        xclose(fh);
        return ENSMEM;
    }

    /* memory ownership - the owner is either the new process being created,
     * or the parent
     */
    owner = (flag == PE_LOADGO) ? p : run;
    set_owner(p, owner);
    set_owner(env_ptr, owner);

    /* initialize the fields in the PD structure */
    init_pd_fields(p, tail, max, env_ptr);
    if (!kproc_create(p)) {
        xmfree(env_ptr);
        xmfree(p);
        xclose(fh);
        return ENSMEM;
    }

    /* set the flags (must be done after init_pd) */
    p->p_flags = hdr.h01_flags;

    /* use static variable to avoid the obscure longjmp warning */
    cur_p = p;

    /* we have now allocated memory, so we need to intercept longjmp. */
    memcpy(bakbuf, errbuf, sizeof(errbuf));
    if (setjmp(errbuf)) {

        KDEBUG(("Error and longjmp in xexec()!\n"));

        /* free any memory allocated so far & close the file */
        kproc_destroy(cur_p);
        xmfree(USERPTR_TO_PTR(cur_p->p_env));
        xmfree(cur_p);
        xclose(fh);

        /* we still have to jump back to bdosmain.c so that the proper error
         * handling can occur.
         */
        longjmp(bakbuf, 1);
    }

    /* now, load the rest of the program, perform relocation, close the file */
    rc = kpgmld(cur_p, fh, &hdr);
    if (rc) {
        KDEBUG(("BDOS xexec: kpgmld returned %ld (0x%lx)\n",rc,rc));
        /* free any memory allocated yet */
        kproc_destroy(cur_p);
        xmfree(USERPTR_TO_PTR(cur_p->p_env));
        xmfree(cur_p);

        return rc;
    }

    /* at this point the program has been correctly loaded in memory, and
     * more I/O errors cannot occur, so it is safe now to finish initializing
     * the new process.
     */
    /* Anything that can still fail for lack of memory is done before
     * init_pd_files(): that takes references on inherited files and
     * directories, which an ENSMEM return here would otherwise leak. */
    if (flag != PE_LOAD && !x86_64_prepare_launch(cur_p)) {
        kproc_destroy(cur_p);
        xmfree(USERPTR_TO_PTR(cur_p->p_env));
        xmfree(cur_p);
        return ENSMEM;
    }
    init_pd_files(cur_p);
    if (flag == PE_LOAD)
        x86_64_hand_over(cur_p);

    /* invalidate instruction cache for the TEXT segment only
     * programs that jump into their DATA, BSS or HEAP are kindly invited
     * to do their cache management themselves.
     */
    invalidate_instruction_cache(((UBYTE *)cur_p) + sizeof(PD), hdr.h01_tlen);

    if (flag != PE_LOAD) {
        proc_go(cur_p);
#ifdef __x86_64__
        /* Returns here, unlike on m68k/ARM (see the PE_GO case above), once
         * the child has exited: its exit code is in the launcher's D0. */
        return run->p_dreg[0];
#endif
    }
    return (long)cur_p;
}

/* initialize the structure fields */
static void init_pd_fields(PD *p, char *tail, long max, char *envptr)
{
    int i;
    char *b;

    /* first, zero it out */
    bzero(p, sizeof(PD)) ;

    /* memory values
     *
     * PTR_TO_USERPTR(), not the unchecked cast: p itself comes from
     * alloc_tpa(), whose __x86_64__ branch (bdos/proc.c) already draws
     * from procmem's low, sub-4GiB window (bios/machine/
     * pc-x86_64/memory.c), so this can never truncate on that arch. */
    p->p_lowtpa = PTR_TO_USERPTR((UBYTE *)p);              /*  M01.01.06   */
    p->p_hitpa  = PTR_TO_USERPTR((UBYTE *)p  +  max);      /*  M01.01.06   */
#if ARCH_ARM
    /*
     * p_hitpa becomes the actual initial user-mode sp of any process
     * launched from this PD: proc_go()'s gouser_stack is unwound entirely
     * by gouser() (bdos/arch/arm/rwa.S), landing sp at exactly p_hitpa, not
     * at a gouser_stack-relative offset. AAPCS requires sp to be 8-byte
     * aligned at every public interface, which gcc's auto-vectorizer
     * (-mfpu=neon-vfpv4) relies on, but the free-memory allocator this
     * pointer comes from (alloc_tpa()/ffit()/getmpb()) only guarantees
     * 4-byte alignment. Round down; losing at most 7 bytes of TPA is
     * harmless.
     */
    p->p_hitpa = (UBYTE *)((ULONG)p->p_hitpa & ~7UL);
#elif defined(__x86_64__)
    /*
     * p_hitpa becomes gouser()'s own initial ring-3 RSP directly
     * (bdos/arch/x86_64/rwa.c), landing sp at exactly p_hitpa before the
     * process's first instruction ever runs. The x86-64 SysV/x32 ABI's
     * process-entry convention requires (RSP + 8) to be a multiple of
     * 16 at that point (equivalently, RSP itself is 8 mod 16) -- p itself
     * comes from a page-aligned allocator (x86_64_procmem_alloc()),
     * but max (the process's own TPA size, ultimately from arbitrary
     * ELF segment sizes) is not, so p+max need not satisfy this (#356's
     * own review caught it: the first `call` in a real process can run
     * with the wrong alignment, breaking anything that assumes the
     * standard entry convention). Round down to the largest value
     * satisfying it; losing at most 15 bytes of TPA is harmless. */
    p->p_hitpa = ((p->p_hitpa + 8UL) & ~15UL) - 8UL;
#endif
    /* Same reasoning as p_lowtpa/p_hitpa above: p_cmdlin is a field
     * within p itself, and envptr comes from alloc_env(), whose own
     * __x86_64__ branch (bdos/proc.c) likewise draws from
     * x86_64_procmem_alloc() rather than xmxalloc()'s higher-half pool
     * (see #360). */
    p->p_xdta = PTR_TO_USERPTR((DTA *) p->p_cmdlin);       /* default p_xdta is p_cmdlin */
    p->p_env = PTR_TO_USERPTR(envptr);

    /* copy tail */
    b = &p->p_cmdlin[0];
    for (i = 0; (i < PDCLSIZE) && (*tail); i++)
        *b++ = *tail++;

    *b++ = 0;
}

/* duplicate files */
static void init_pd_files(PD *p)
{
    int i;

    /* inherit standard files from me */
    for (i = 0; i < NUMSTD; i++) {
        WORD h = run->p_uft[i];
        if (h > 0)
            ixforce(i, h, p);
        else
            p->p_uft[i] = h;
    }

    /* and current directory set */
    for (i = 0; i < NUMCURDIR; i++) {
        int dn = run->p_curdir[i];
        p->p_curdir[i] = dn;
        if (dn)
            dirtbl[dn].use++;
#if CONF_WITH_PLUGGABLE_FS
        /* p_curdir[] indexes fs/pfs.c's own table instead when this
         * option is on (see the comment above PFS_MAX_CWD in fs/pfs.c);
         * the dirtbl[] bump above is harmless but meaningless in that
         * case, so also bump the table actually in use. */
        if (dn)
            pfs_cwd_addref((WORD)dn);
#endif
    }

    /* and current drive */
    p->p_curdrv = run->p_curdrv;
}

/* allocate the environment, in ST RAM or alternate RAM, according to the header flags */
static char *alloc_env(ULONG flags, char *env)
{
    char *new_env;
    int size;

    /* determine the env size */
    if (env == NULL)
        env = (char *)USERPTR_TO_PTR(run->p_env);
    size = (envsize(env) + 1) & ~1;  /* must be even */

    /* allocate it */
#ifdef __x86_64__
    /*
     * Same reasoning as alloc_tpa()'s own #ifdef __x86_64__ branch
     * above: xmxalloc()'s ffit()/pmd free list is ultimately built from
     * membot/memtop, which point into _end_os_stram -- an ordinary
     * higher-half kernel symbol, so memory it hands out cannot be
     * stored in a PD's USERPTR_T-typed p_env field without truncation
     * (#360: this is exactly the bug that field's own corruption turned
     * out to be, before this fix). Route through the same dedicated low
     * pool alloc_tpa() already uses instead: an environment string is
     * conceptually just as much "this process's own low memory" as its
     * TPA is, and bdos/umem.c's set_owner()/xmfree() already handle an
     * address outside every known MPB gracefully (see alloc_tpa()'s own
     * comment), exactly what happens for memory from this same pool.
     */
    new_env = (char *)x86_64_procmem_alloc(size, PROCMEM_ZERO);
    /* built below under the launcher's page tables: lend it to a ring-3 one */
    if (new_env && !kproc_borrow(run, new_env)) {
        x86_64_procmem_free(new_env);
        new_env = NULL;
    }
#else
    new_env = xmxalloc(size, (flags&PF_TTRAMLOAD) ? MX_PREFTTRAM : MX_STRAM);
#endif
    if (new_env)
    {
        memcpy(new_env, env, size);     /* copy it */
    }

    return new_env;
}

/*
 * allocate the TPA
 *
 * we first determine if ST RAM and/or alternate RAM is available for
 * allocation, based on the flags, the amount of RAM required and
 * the presence or absence of TT RAM.
 *
 * if no types are available (the requested amount is too large), we
 * return NULL.
 *
 * if only one type of RAM is available, we allocate it & return a
 * pointer to it.
 *
 * if both types are available, we normally allocate in alternate RAM
 * *except* if the amount of ST RAM is greater than the amount of
 * alternate RAM.  In this case, we use a tiebreaker: bits 31-27
 * of the flags field plus 1 is taken as a 4-bit number, which is
 * multiplied by 128K and added to the base amount needed to get a
 * "would like to have" amount.  If this amount is larger than the
 * amount of alternate RAM, then we allocate in ST RAM.
 *
 * Reference: TT030 TOS Release Notes, Third Edition, 6 September 1991,
 * pages 29-30.
 *
 * returns: ptr to allocated memory (NULL => failed)
 *          updates 'avail' with the size of allocated memory
 */

static UBYTE *alloc_tpa(ULONG flags,LONG needed,LONG *avail)
{
    MD *md;
    LONG st_ram_size;
    BOOL st_ram_available = FALSE;

#ifdef __x86_64__
    /*
     * This arch has no ST/alternate-RAM distinction to route through
     * ffit()/pmd/pmdalt at all -- and, more fundamentally, membot/
     * memtop (what pmd's free list is ultimately built from) point into
     * _end_os_stram, an ordinary higher-half kernel symbol that cannot
     * be forced low without an unrelated relocation overflow (see
     * procmem.c's own comment on x86_64_low_tpa_init() for why). Route
     * through that dedicated low pool instead.
     *
     * needed+15, not needed: init_pd_fields() (below) rounds p_hitpa
     * (p+max) down to the nearest address satisfying the SysV/x32 ABI's
     * process-entry stack alignment, losing up to 15 bytes -- allocate
     * that much extra slack up front and report the padded size as
     * *avail, so an ELF whose own size exactly equals `needed` still
     * gets a tpalen at least that large after rounding (elf_pgmld()
     * would otherwise wrongly reject an exact-fit image with ENSMEM;
     * Copilot's review of #356 caught this).
     */
    {
        UBYTE *low = x86_64_procmem_alloc(needed + 15, PROCMEM_ZERO);

        if (low && !kproc_borrow(run, low)) {   /* see alloc_env() */
            x86_64_procmem_free(low);
            low = NULL;
        }
        if (low)
            *avail = needed + 15;
        return low;
    }
#endif

    st_ram_size = (LONG) ffit(-1L, &pmd);
    if (st_ram_size >= needed)
        st_ram_available = TRUE;

#if CONF_WITH_ALT_RAM
    {
        LONG alt_ram_size = 0L, tpasize;
        BOOL alt_ram_available = FALSE;

        if (has_alt_ram && (flags & PF_TTRAMLOAD)) {
            alt_ram_size = (LONG) ffit(-1L, &pmdalt);
            if (alt_ram_size >= needed)
                alt_ram_available = TRUE;
        }

        if (st_ram_available && alt_ram_available && (st_ram_size > alt_ram_size)) {
            tpasize = (((flags >> 28) & 0x0f) + 1) * TPASIZE_QUANTUM;
            if (needed+tpasize > alt_ram_size)
                alt_ram_available = FALSE;  /* force allocation in ST RAM */
        }

        if (alt_ram_available) {
            *avail = alt_ram_size;
            md = ffit(alt_ram_size, &pmdalt);
            return md->m_start;
        }
    }
#endif

    if (st_ram_available) {
        *avail = st_ram_size;
        md = ffit(st_ram_size, &pmd);
        return md->m_start;
    }

    return NULL;
}

/* proc_go launches the new process by creating the right data
 * structure in memory, then pretending resuming from an ordinary
 * BDOS call by calling gouser().
 *
 * Here is an excerpt of gouser() from rwa.S:
 * _gouser:
 *   move.l  _run,a5
 *   move.l  d0,0x68(a5)
 *   move.l  0x7c(a5),a6     // stack pointer (maybe usp, maybe ssp)
 *   move.l  (a6)+,a4        // other stack pointer
 *   move.w  (a6)+,d0
 *   move.l  (a6)+,a3        // retadd
 *   movem.l (a6)+,d1-d7/a0-a2
 *   btst    #13,d0
 *   jne     retsys          // a6 is (user-supplied) system stack
 *   move.l  a4,sp
 *   move.l  a6,usp
 * gousr:
 *   move.l  a3,-(sp)
 *   move    d0,-(sp)
 *   movem.l 0x68(a5),d0/a3-a6
 *
 */

#ifdef __arm__
struct gouser_stack {
    LONG regs[8];    /* argument registers from previous call (r7, r0-r6) */
    LONG other_sp;   /* the other stack pointer */
    LONG retaddr;
    LONG spsr;       /* note the basepage is passed in r0 and not on the stack */
};
#elif defined(__x86_64__)
/*
 * Not yet a real coroutine stack layout (unlike the ARM/m68k structs
 * above): a genuine x86-64 gouser()/termuser() needs a dedicated
 * per-process kernel stack (bdos/arch/x86_64/rwa.S's gouser()/termuser()
 * panic rather than attempting anything with this, see that file's own
 * comment) plus a real ring0->ring3 transition (iretq, reusing the
 * GDT/TSS this arch's trap.c already sets up) -- more than this one
 * struct can express. Kept as an empty placeholder so proc_go() below
 * has something of the right *kind* to size/reference without pretending
 * the m68k/ARM field layouts mean anything here.
 */
struct gouser_stack {
    LONG unused;
};
#else
struct gouser_stack {
  LONG other_sp;   /* a4, the other stack pointer */
  WORD sr;         /* d0, the status register */
  LONG retaddr;    /* a3, the return address */
  LONG fill[11];   /* 10 registers d1-d7/a0-a2 and one dummy so that ... */
  PD * basepage;   /* ... upon startup the basepage is in 4(sp) */
};
#endif

static void proc_go(PD *p)
{
    struct gouser_stack *sp;

    KDEBUG(("BDOS xexec: trying to load (and execute) a process on %p ...\n",p->p_tbase));
    /* PTR_TO_USERPTR(), not the unchecked cast: `run` here is always
     * either a real, alloc_tpa()'d PD or the first process's parent,
     * initial_basepage (bdosmain.c) -- also low on x86-64 since #360's
     * review (bdosmain.c's osinit_after_xmaddalt() now allocates it from
     * the same pool). Checked because xterm()/ixterm() widen p_parent
     * back and dereference it on every Pterm(), so a truncated value
     * here would fault there instead of trapping at the point of
     * corruption. */
    p->p_parent = PTR_TO_USERPTR(run);
#ifdef __x86_64__
    kproc_set_parent(p, run);   /* the copy xterm() trusts */
    /* A ring-3 process cannot see its launcher's basepage, nor the
     * launcher's own: it gets read-only copies at a fixed address instead. */
    if (kproc_ancestors_va(p))
        p->p_parent = (ULONG)kproc_ancestors_va(p);
#endif

    /* create a stack at the end of the TPA */
    sp = (struct gouser_stack *) (p->p_hitpa - sizeof(struct gouser_stack));

#ifdef __arm__
    p->p_dreg[0] = (LONG)p;  /* base page is passed in r0 */
    sp->regs[1] = ARM_ENTRY_PROGRAM;
    sp->spsr = ((get_cpsr() & ~0x1f) | 0x10); /* the process will start in user mode, same interrupts */
    sp->retaddr = (long)p->p_tbase; /* return address is text start */
    /* the other stack is the supervisor stack */
    sp->other_sp = (long) &supstk[SUPSIZ];
    /* store this new stack in the saved sp field of the PD */
    p->p_areg[7-3] = (long) sp;
#elif defined(__x86_64__)
    /* Not implemented yet -- see gouser_stack's own comment above and
     * bdos/arch/x86_64/rwa.S. gouser() below panics with a clear message
     * rather than silently running with none of this set up. */
    (void)sp;
#else
    sp->basepage = p;      /* the stack contains the basepage */

    sp->retaddr = (long)p->p_tbase; /* return address a3 is text start */
    sp->sr = get_sr() & 0x0700;  /* the process will start in user mode, same IPL */

    /* the other stack is the supervisor stack */
    sp->other_sp = (long) &supstk[SUPSIZ];

    /* store this new stack in the saved a7 field of the PD */
    p->p_areg[7-3] = (long) sp;

#if 1
    /* the following settings are not documented, and hence theoretically
     * the assignments below are not necessary.
     * However, many programs test if A0 = 0 to check if they are running
     * as a normal program or as an accessory, so we need to clear at least
     * this register!
     */
    {   /* d1-d7/a0-a2 and dummy return address set to zero */
        int i;
        for(i = 0; i < 11 ; i++)
            sp->fill[i] = 0;
    }
    p->p_areg[6-3] = (long) sp;    /* a6 to hold a copy of the stack */
    p->p_areg[5-3] = (long)p->p_dbase;  /* a5 to point to the DATA segt */
    p->p_areg[4-3] = (long)p->p_bbase;  /* a4 to point to the BSS segt */
#endif
#endif

    /* the new process is the one to run */
#ifdef __x86_64__
    kproc_mark_started(p);
#endif
    run = (PD *)p;

    gouser();
}



/*
 * x0term - (Pterm0) terminate current process
 *
 * terminates the calling process and returns to the parent process
 * with a return code of zero
 */
void x0term(void)
{
    xterm(0);
}

/*
 * xterm - (Pterm) terminate current process
 *
 * terminate the current process and transfer control to the calling
 * process.  All files opened by the terminating process are closed.
 *
 * Function 0x4C        p_term
 */
void xterm(UWORD rc)
{
    PFVOID userterm;
    PD *p = run;

    userterm = (PFVOID)Setexc(0x102, (long)-1L);  /* get user term handler address */
    protect_v((PFLONG)userterm);    /* call it, protecting d2/a2 from modification */

#ifdef __x86_64__
    /* not run->p_parent: that field is in the process's own writable
     * memory, and run->p_dreg[0] is written through the result below */
    run = kproc_get_parent(p);
#else
    run = (PD *)USERPTR_TO_PTR(run->p_parent);
#endif
    kproc_destroy(p);
#ifdef __x86_64__
    if (x86_64_resident_pd == p) {      /* Ptermres, now that nothing maps p */
        x86_64_make_resident(p, x86_64_resident_len);
        x86_64_resident_pd = NULL;
    }
#endif
    ixterm(p);
    /* gouser() will store the current value of D0 in the active PD
     * so it cannot be used here. See proc_go() above.
     * termuser() will enter the gouser() code at the proper place.
     * sep 2005 RCL
     */
    run->p_dreg[0] = rc;
    termuser();
}


/*
 * xtermres - Function 0x31 (Ptermres)
 */
WORD xtermres(long blkln, WORD rc)
{
    xsetblk(0,run,blkln);

    reserve_blocks(run, &pmd);
#if CONF_WITH_ALT_RAM
    if (has_alt_ram)
        reserve_blocks(run, &pmdalt);
#endif
#ifdef __x86_64__
    /* The window's blocks are not MPB descriptors, so xsetblk() above cannot
     * shrink them: xterm() does it, and makes the rest resident, once the
     * address space that maps them is destroyed. */
    x86_64_resident_pd = run;
    x86_64_resident_len = (ULONG)blkln;
#endif
    xterm(rc);
}
