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
#if CONF_WITH_USER_ASPACE
#include "proc_clone.h"
#include "process_observer.h"
#endif
#include "gemerror.h"
#include "biosbind.h"
#include "string.h"
#include "biosext.h"
#if CONF_WITH_PROCMEM && CONF_WITH_KCALL
#include "bdosstub.h"
#endif
#include "asm.h"
#include "tosvars.h"
#include "has.h"
#if ARCH_X86_64
#include "process.h"
#endif
#if CONF_WITH_NATIVE_SCHED
#include "ksched.h"
#include "ksched_lifetime.h"
#include "kthread.h"
#if CONF_WITH_CUIS
#include "bdosbind.h"
#endif
#endif
#include "../bios/vectors.h"
#if CONF_WITH_X86_64_MEMTEST
#include "../bios/machine/pc-x86_64/pc_x86_64_memtest.h"
#endif
#if CONF_WITH_PLUGGABLE_FS
#include "pfs.h"
#endif


/*
 * defines
 */
#define TPASIZE_QUANTUM (128*1024L)     /* see alloc_tpa() */

#if CONF_WITH_PROCMEM
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
static void proc_go(PD *p, WORD entry_type);

/*
 * global variables
 */

#if !CONF_WITH_NATIVE_SCHED
PD      *run;           /* classic single-execution ABI storage */
#endif /* native kernels export the CPU-owned slot from native_sched.c */

#if CONF_WITH_USER_ASPACE
/* Kernel code marks a freshly created ROM basepage here before Pexec(). */
static PD *kernel_code_pd;

void mark_kernel_code_pd(PD *p)
{
    kernel_code_pd = p;
}

/* Pexec() from a process with an address space of its own (see
 * doc/x86_64-address-space.txt, "Basepages in ring 3"): the caller owns what
 * it creates, and may only launch what the kernel made for it. */
static BOOL caller_has_aspace(void)
{
    return kproc_user_aspace(run) != NULL;
}

static LONG check_launch(PD *p)
{
    return caller_has_aspace() ? kproc_check_launch(p, run) : E_OK;
}

static void hand_over(PD *p)
{
    kproc_set_creator(p, run);
    if (caller_has_aspace())
        kproc_hand_over(run, p);
}

/* Preserve the legacy give-back path for kernel/ROM compatibility. User
 * address-space callers cannot request PE_GO until shared-VM mode 4 exists. */
static void give_back_image(PD *p, BOOL give_back)
{
    kproc_set_give_back(p, give_back);
}

/*
 * Per-launch setup that can fail for lack of memory, done where Pexec()
 * can still return ENSMEM: a kernel-code process runs in ring 0 and needs
 * nothing; a real one gets its address space here, owned by its KPROC record
 * from now on (see kproc_prepare_user()).
 */
static BOOL prepare_launch(PD *p)
{
    if (kernel_code_pd == p)
        return TRUE;
    return kproc_prepare_user(p, run);
}

BOOL take_kernel_code_pd(PD *p)
{
    if (kernel_code_pd != p)
        return FALSE;

    kernel_code_pd = NULL;
    return TRUE;
}
#endif /* CONF_WITH_USER_ASPACE */

#if CONF_WITH_NATIVE_SCHED
static BOOL schedule_launch(PD *p)
{
    /* ROM entry points and explicit accessory starts retain their existing
     * launch path. A nested Pexec already has a stable group controller. */
    return kernel_code_pd != p && !ksched_dispatching();
}

static LONG run_scheduled(PD *p, BOOL take_ownership)
{
    LONG result, rc = ksched_exec(p, take_ownership, &result);
    return rc < 0 ? rc : result;
}
#endif

#if CONF_WITH_PROCMEM
static void release_pd_files(PD *r);

/* Drops the kernel-private record keyed by a freed process block.  A
 * basepage that was created (inheriting its parent's standard handles and
 * current directories in init_pd_files()) but never launched still holds
 * those references, and nothing else will ever release them: do it now,
 * exactly once -- the record is the proof it has not been done. */
static void proc_release_block(void *base)
{
    /* the tables are in the record: release through it, then drop it */
    if (kproc_unlaunched((PD *)base))
        release_pd_files((PD *)base);
    kproc_discard((PD *)base);
#if CONF_WITH_USER_ASPACE
    /* a block lent to a launcher with an address space goes back, but only
     * after its basepage has been read: that launcher's page tables are loaded */
    kproc_unborrow(base);
#endif
}

/* Ptermres: an unlaunched child basepage the terminating process owns stays
 * allocated, but its record goes, like reserve_blocks() drops it.  The record
 * is also what says its inherited file and directory references are still
 * held, so they are released now and cleared from the basepage: whoever
 * launches or frees it later must not release them a second time. */
static void proc_drop_child_record(void *base)
{
    PD *child = (PD *)base;

    /* (the tables go with the record: later users of the basepage find the
     * basepage's own, zero, ones and release nothing a second time) */
    if (kproc_unlaunched(child))
        release_pd_files(child);
    kproc_discard(child);
#if CONF_WITH_USER_ASPACE
    kproc_unborrow(base);       /* lent to a launcher while it was built */
#endif
}

/* Pending Ptermres: applied by xterm() once the process's address space is
 * gone, because only then can the unused tail of its block be released. */
static PD *proc_resident_pd;
static ULONG proc_resident_len;

void proc_make_resident(PD *p, ULONG keep_bytes)
{
#if CONF_WITH_USER_ASPACE
    /* The process's own block is not passed to the callback below, and its
     * tail cannot be given back while the launcher still has it mapped: return
     * what a launcher borrowed for it first (its environment goes through the
     * callback). */
    /* (a basepage the launcher owns, launched with PE_GO, stays its: the
     * grant it was given goes on) */
    if (procmem_owner(p) == p)
        kproc_unborrow(p);
#endif
    procmem_keep(p, keep_bytes, proc_drop_child_record);
}

/*
 * Frees every process allocation `p` owns (the counterpart of
 * free_all_owned() for the MPB lists), dropping KPROC records of any
 * basepages among them first.
 */
void proc_free_owned(PD *p)
{
    procmem_free_owned(p, proc_release_block);
}

/* Mfree() of a process allocation: same release, one block.  `trusted` is
 * Pexec()'s own rollback of what it just allocated for its caller. */
static long proc_mfree(void *addr, BOOL trusted)
{
    /* A block that a live process still has mapped (its own environment or
     * basepage, say) cannot be freed from under it, and tearing down the
     * address space the caller is running in would be worse: refuse, and
     * leave the KPROC record alone. */
#if CONF_WITH_USER_ASPACE
    /* A caller with an address space frees what it owns and nothing else: the
     * blocks of a process it launched are not its, nor those a peer retained. */
    if (!trusted && caller_has_aspace() && procmem_owner(addr) != run)
        return EACCDN;
    if (procmem_pins(addr) > kproc_borrow_count(run, addr))
        return EACCDN;
#else
    if (procmem_pins(addr) > 0)
        return EACCDN;
#endif
    proc_release_block(addr);     /* also returns it if it was borrowed */
    return procmem_free(addr) ? E_OK : EIMBA;
}

long procmem_mfree(void *addr)
{
    return proc_mfree(addr, FALSE);
}
#endif /* CONF_WITH_PROCMEM */

/* xexec()'s rollback: its blocks are not owned yet, or belong to the child
 * it failed to start, and a launcher with an address space must get back what
 * was lent to it. */
static void xexec_free(void *addr)
{
#if CONF_WITH_PROCMEM
    if (procmem_contains(addr)) {
        proc_mfree(addr, TRUE);
        return;
    }
#endif
    xmfree(addr);
}

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
static WORD    supstk[SUPSIZ] __attribute__((aligned(8))); /* common sup stack for all processes */


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
#if CONF_WITH_USER_ASPACE
/* SFT owners are compared for identity, never dereferenced. A live inherited
 * slot or an interrupted close may outlive its original owner's basepage. */
static char orphaned_sft_owner;
#endif
static void release_pd_files(PD *r)
{
    WORD h;
    WORD i;
#if CONF_WITH_CUIS
    ULONG explicit_refs[OPNFILES];

    /* An SFT slot retains f_own after its explicit handle is closed while
     * standard aliases remain. Snapshot before dropping this process's
     * aliases: only a reference beyond all live aliases is an explicit
     * handle the owner sweep may close. Fdup uses a separate SFT slot. */
    kproc_standard_file_refs(explicit_refs);
    for (i = 0; i < OPNFILES; i++)
        explicit_refs[i] = r == sft[i].f_own && sft[i].f_use > 0 &&
                          (ULONG)sft[i].f_use > explicit_refs[i];
#endif

    /* check the standard devices in both file tables  */

    for (i = 0; i < NUMSTD; i++)
        if ((h = PD_UFT(r)[i]) > 0) {
#if CONF_WITH_PROCMEM && CONF_WITH_KCALL
            kdos_fclose(h); /* Catch disk errors and finish the interrupted close. */
#else
            xclose(h);
#endif
        }

    for (i = 0; i < OPNFILES; i++)
        if (r == sft[i].f_own
#if CONF_WITH_CUIS
            && explicit_refs[i]
#endif
        ) {
#if CONF_WITH_PROCMEM && CONF_WITH_KCALL
            kdos_fclose(i+NUMSTD);
#else
            xclose(i+NUMSTD);
#endif
        }


    /* decrement usage counts for current directories */

    for (i = 0; i < NUMCURDIR; i++)
    {
        if ((h = PD_CURDIR(r)[i]) != 0)
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
#if CONF_WITH_USER_ASPACE
    /* Preserve the allocated SFT slot without leaving a reusable PD address
     * in f_own. A failed bounded close can still leak its handle reference. */
    for (i = 0; i < OPNFILES; i++)
        if (sft[i].f_own == r)
            sft[i].f_own = (PD *)&orphaned_sft_owner;
#endif
}

static void ixterm(PD *r)
{
#if !CONF_WITH_KPROC
    release_pd_files(r);    /* (with records: xterm() did, while the record was there) */
#endif

    /* free each item in the allocated list that is owned by 'r' */

    free_all_owned(r, &pmd);
#if CONF_WITH_ALT_RAM
    if (has_alt_ram)
        free_all_owned(r, &pmdalt);
#endif
#if CONF_WITH_PROCMEM
    proc_free_owned(r);
#endif
}

/* Final process-wide teardown. User-ASPACE exits normally finalize on the
 * resumed launcher, after reclaiming the child's transferred syscall stack.
 * The unassociated legacy fallback still runs before returning to its launcher. */
static void proc_release_resources(PD *p, BOOL resident_exit)
{
#if CONF_WITH_KPROC
    release_pd_files(p);
#endif
    kproc_destroy(p);
#if CONF_WITH_PROCMEM
    if (resident_exit && proc_resident_pd == p) {
        proc_make_resident(p, proc_resident_len);
        proc_resident_pd = NULL;
    }
#else
    UNUSED(resident_exit);
#endif
    ixterm(p);
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

struct kproc;
static long exec_common(WORD flag, char *path, char *tail, char *env,
    ULONG session, ULONG token, BOOL deferred, char *directory, struct kproc **observe);
long xexec(WORD flag, char *path, char *tail, char *env)
{
    return exec_common(flag,path,tail,env,0,0,FALSE,NULL,NULL);
}
#if CONF_WITH_USER_ASPACE && CONF_WITH_NATIVE_SCHED
long proc_exec_observe(char *path, char *tail, char *env, char *directory,
                       PROCESS_OBSERVER **out)
{
    if (!out || *out) return EINVAL;
    return DISK_CALL(exec_common(PE_ASYNC_LOADGO,path,tail,env,0,0,FALSE,directory,out));
}
#endif
#if CONF_WITH_CUIS
long xexec_observed(char *path, char *tail, char *env, ULONG session, ULONG token)
{
    return DISK_CALL(exec_common(PE_ASYNC_LOADGO,path,tail,env,session,token,FALSE,NULL,NULL));
}
long xexec_deferred(char *path, char *tail, char *env, ULONG session)
{
    return DISK_CALL(exec_common(PE_ASYNC_LOADGO,path,tail,env,session,0,TRUE,NULL,NULL));
}
long xexec_directory(char *path, char *tail, char *env, ULONG session, ULONG token, BOOL deferred, char *directory)
{
    return DISK_CALL(exec_common(PE_ASYNC_LOADGO,path,tail,env,session,token,deferred,directory,NULL));
}
#endif
static long exec_common(WORD flag, char *path, char *tail, char *env,
    ULONG session, ULONG token, BOOL deferred, char *directory, struct kproc **observe)
{
    PD *p, *owner;
    PGMHDR01 hdr;
    char *env_ptr;
    ULONG hdrflags;
    LONG rc;
    LONG max;
#if CONF_WITH_NATIVE_SCHED
    LONG scheduled_result;
#endif
    long needed;
    FH fh;
    PD *cur_p; /* assigned before setjmp, never changed across its return */
    FS_ERROR_STATE loader_error;
#if CONF_WITH_USER_ASPACE && CONF_WITH_NATIVE_SCHED
    PROCESS_OBSERVER *observed = NULL;
#else
    UNUSED(observe);
#endif
#if !CONF_WITH_CUIS
    UNUSED(session); UNUSED(token); UNUSED(deferred);
#endif
#if !CONF_WITH_NATIVE_SCHED
    UNUSED(directory);
#endif

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
        hdrflags = (ULONG)(uintptr_t)path;
        env_ptr = alloc_env(hdrflags, env);
        if (env_ptr == NULL) {
            KDEBUG(("BDOS xexec: no memory for environment\n"));
            return ENSMEM;
        }
        p = (PD *)alloc_tpa((ULONG)(uintptr_t)path,sizeof(PD),&max);

        if (p == NULL) {    /* not even enough memory for basepage */
            xexec_free(env_ptr);
            KDEBUG(("BDOS xexec: No memory for basepage\n"));
            return ENSMEM;
        }

        /* memory ownership */
        set_owner(p, run);
        set_owner(env_ptr, run);

        /* initialize the PD */
        init_pd_fields(p, tail, max, env_ptr);
        if (!kproc_create(p)) {
            xexec_free(env_ptr);
            xexec_free(p);
            return ENSMEM;
        }
        p->p_flags = (ULONG)(uintptr_t)path;   /* set the flags */
        init_pd_files(p);
#if CONF_WITH_USER_ASPACE
        hand_over(p);
#endif

        return (long)p;
#if CONF_WITH_NATIVE_SCHED
    case PE_ASYNC_GO_FREE:
        if (!ksched_async_allowed()) return EINVFN;
        p = (PD *)tail;
        rc = check_launch(p);
        if (rc) return rc;
        if (!kproc_create(p)) return ENSMEM;
        give_back_image(p, FALSE);
        return ksched_spawn(p);
    case PE_ASYNC_LOADGO:
        if (!ksched_async_allowed()) return EINVFN;
        break;
#endif
    case PE_GOTHENFREE:
        p = (PD *) tail;
#if CONF_WITH_USER_ASPACE
        rc = check_launch(p);
        if (rc)
            return rc;
#endif
        /* The allocation can fail; retain the parent's ownership until it
         * succeeds so an ENSMEM return leaves the retained basepage freeable. */
        if (!kproc_create(p))
            return ENSMEM;
#if CONF_WITH_USER_ASPACE
        give_back_image(p, FALSE);      /* (a PE_GO that failed before may have asked) */
#if CONF_WITH_NATIVE_SCHED
        if (schedule_launch(p)) return run_scheduled(p, TRUE);
#endif
        if (!prepare_launch(p))
            return ENSMEM;
#endif
        /* set the owner of the memory to be this process */
        set_owner(p, p);
        set_owner(USERPTR_TO_PTR(p->p_env), p);
        FALLTHROUGH;
    case PE_GO:
#if CONF_WITH_USER_ASPACE
        /* Ring-3 mode 4 will return as shared-VM Pexec in #894. Refuse it
         * before touching the staged image or changing any ownership. */
        if (flag == PE_GO && caller_has_aspace())
            return EINVFN;
#endif
        p = (PD *) tail;
#if CONF_WITH_USER_ASPACE
        if (flag == PE_GO && (rc = check_launch(p)) != E_OK)
            return rc;
#endif
        if (flag == PE_GO && !kproc_create(p))
            return ENSMEM;
#if CONF_WITH_USER_ASPACE
        give_back_image(p, flag == PE_GO);
#if CONF_WITH_NATIVE_SCHED
        if (schedule_launch(p)) return run_scheduled(p, FALSE);
#endif
        if (flag == PE_GO && !prepare_launch(p))
            return ENSMEM;
#endif
        proc_go(p, 0);
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
#if CONF_WITH_USER_ASPACE && CONF_WITH_ELF_LOADER
    elf_set_load_into_caller(flag == PE_LOAD);
#endif
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
    p = NULL;
#if KPROC_LEGACY_PRG
    /*
     * A classic PRG gets what TOS gave it: a TPA of the free memory (here at
     * most half of the window, so that the rest stays for its children), which
     * it shrinks with Mshrink() once it has set its stack.
     */
    if (kpgm_is_prg()) {
        LONG all = (LONG)procmem_largest_free();

        if (all > (LONG)KPROC_LEGACY_TPA_MAX)
            all = KPROC_LEGACY_TPA_MAX;
        all -= 16;                  /* (alloc_tpa() adds some) */
        if (all > needed)
            p = (PD *)alloc_tpa(hdr.h01_flags, all, &max);
    }
#endif
    if (p == NULL)
        p = (PD *)alloc_tpa(hdr.h01_flags,needed,&max);

    /* if failed, free env_ptr and return */
    if (p == NULL) {
        KDEBUG(("BDOS xexec: no memory for TPA\n"));
        xexec_free(env_ptr);
        xclose(fh);
        return ENSMEM;
    }

    /* memory ownership - the owner is either the new process being created,
     * or the parent
     */
    owner = (flag == PE_LOADGO
#if CONF_WITH_NATIVE_SCHED
             || flag == PE_ASYNC_LOADGO
#endif
            ) ? p : run;
    set_owner(p, owner);
    set_owner(env_ptr, owner);

    /* initialize the fields in the PD structure */
    init_pd_fields(p, tail, max, env_ptr);
    if (!kproc_create(p)) {
        xexec_free(env_ptr);
        xexec_free(p);
        xclose(fh);
        return ENSMEM;
    }

    /* set the flags (must be done after init_pd) */
    p->p_flags = hdr.h01_flags;
#if KPROC_LEGACY_PRG
    if (kpgm_is_prg())
        kproc_mark_legacy(p);
#endif

    /* The loaded image and enclosing target belong to this invocation. */
    cur_p = p;

    /* we have now allocated memory, so we need to intercept longjmp. */
    loader_error = *fs_error_current();
    if (setjmp(errbuf)) {

        KDEBUG(("Error and longjmp in xexec()!\n"));

        /* free any memory allocated so far & close the file */
        kproc_destroy(cur_p);
        xexec_free(USERPTR_TO_PTR(cur_p->p_env));
        xexec_free(cur_p);
        xclose(fh);

        /* we still have to jump back to bdosmain.c so that the proper error
         * handling can occur.
         */
        memcpy(errbuf, loader_error.jump, sizeof(errbuf));
        longjmp(errbuf, 1);
    }

    /* now, load the rest of the program, perform relocation, close the file */
    rc = kpgmld(cur_p, fh, &hdr, path);
    /* No later launch/admission/cleanup may retain this loader frame as its
     * disk-error target, especially across nested synchronous Pexec. */
    *fs_error_current() = loader_error;
    if (rc) {
        KDEBUG(("BDOS xexec: kpgmld returned %ld (0x%lx)\n",rc,rc));
        /* free any memory allocated yet */
        kproc_destroy(cur_p);
        xexec_free(USERPTR_TO_PTR(cur_p->p_env));
        xexec_free(cur_p);

        return rc;
    }

#if CONF_WITH_KPROC
    kproc_set_executable_name(cur_p, path);
#endif
    /* at this point the program has been correctly loaded in memory, and
     * more I/O errors cannot occur, so it is safe now to finish initializing
     * the new process.
     */
    /* Direct launches prepare before taking inherited file/directory
     * references. Managed launches allocate the group before preparation;
     * their later failure path disposes these references explicitly. */
#if CONF_WITH_USER_ASPACE
    if (flag != PE_LOAD
#if CONF_WITH_NATIVE_SCHED
        && flag != PE_ASYNC_LOADGO
        && !schedule_launch(cur_p)
#endif
        && !prepare_launch(cur_p)) {
        kproc_destroy(cur_p);
        xexec_free(USERPTR_TO_PTR(cur_p->p_env));
        xexec_free(cur_p);
        return ENSMEM;
    }
#endif
    init_pd_files(cur_p);
#if CONF_WITH_USER_ASPACE
    if (flag == PE_LOAD)
        hand_over(cur_p);
#endif

    /* invalidate instruction cache for the TEXT segment only
     * programs that jump into their DATA, BSS or HEAP are kindly invited
     * to do their cache management themselves.
     */
    invalidate_instruction_cache(((UBYTE *)cur_p) + sizeof(PD), hdr.h01_tlen);

#if CONF_WITH_NATIVE_SCHED
    if (flag == PE_ASYNC_LOADGO) {
        rc = directory ? fs_set_process_directory(directory,cur_p) : E_OK;
#if CONF_WITH_USER_ASPACE
        if (rc == E_OK && observe) {
            observed = kproc_retain(cur_p); /* before any task can be published */
            if (!observed) rc = ENSMEM;
        }
#endif
        if (rc == E_OK) {
#if CONF_WITH_CUIS
            rc = deferred ? ksched_defer(cur_p,session) : ksched_spawn_observed(cur_p,session,token);
#else
            rc = ksched_spawn(cur_p);
#endif
        }
        if (rc < 0) {
#if CONF_WITH_CUIS
            if (deferred) proc_dispose_deferred(cur_p);
            else
#endif
                proc_discard_scheduled(cur_p);
#if CONF_WITH_USER_ASPACE
            kproc_release(observed);
#endif
        }
#if CONF_WITH_USER_ASPACE
        else if (observe) *observe = observed; /* kernel output, no fallible work follows */
#endif
        return rc;
    }
#endif
    if (flag != PE_LOAD) {
#if CONF_WITH_NATIVE_SCHED
        if (schedule_launch(cur_p)) {
            rc = ksched_exec(cur_p, TRUE, &scheduled_result);
            if (rc < 0) proc_discard_scheduled(cur_p);
            return rc < 0 ? rc : scheduled_result;
        }
#endif
        proc_go(cur_p, 0);
#if CONF_WITH_USER_ASPACE
        /* Returns here, unlike without CONF_WITH_USER_ASPACE (see the PE_GO case above), once
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
     * alloc_tpa(), whose CONF_WITH_PROCMEM branch (bdos/proc.c) already draws
     * from procmem's low, sub-4GiB window (bios/machine/
     * pc-x86_64/memory.c), so this can never truncate on that arch. */
    p->p_lowtpa = PTR_TO_USERPTR((UBYTE *)p);              /*  M01.01.06   */
    p->p_hitpa  = PTR_TO_USERPTR((UBYTE *)p  +  max);      /*  M01.01.06   */
    proc_align_stack(p);
    /* Same reasoning as p_lowtpa/p_hitpa above: p_cmdlin is a field
     * within p itself, and envptr comes from alloc_env(), whose own
     * CONF_WITH_PROCMEM branch (bdos/proc.c) likewise draws from
     * procmem_alloc() rather than xmxalloc()'s higher-half pool
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
static LONG init_pd_standard_files_from(PD *p, PD *source)
{
    int i;
    LONG error = E_OK, rc;

    /* Preserve existing Pexec inheritance while allowing checked factory use. */
    for (i = 0; i < NUMSTD; i++) {
        WORD h = PD_UFT(source)[i];
        if (h > 0) {
            rc = ixforce(i, h, p);
            if (rc < 0 && error == E_OK) error = rc;
        }
        else
            PD_UFT(p)[i] = h;
    }

    return error;
}

static void init_pd_cwd_from(PD *p, PD *source)
{
    int i;
    /* The same native/PFS CWD reference operations used by Pexec. */
    for (i = 0; i < NUMCURDIR; i++) {
        int dn = PD_CURDIR(source)[i];
        PD_CURDIR(p)[i] = dn;
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
    p->p_curdrv = source->p_curdrv;
}

static void init_pd_files(PD *p)
{
    (void)init_pd_standard_files_from(p, run);
    init_pd_cwd_from(p, run);
}

#if CONF_WITH_USER_ASPACE
#if CONF_WITH_X86_64_MEMTEST
static UWORD clone_factory_fail_stage;
void proc_clone_factory_test_fail_after(UWORD stage) { clone_factory_fail_stage = stage; }
static BOOL clone_factory_injected(UWORD stage)
{
    if (clone_factory_fail_stage != stage) return FALSE;
    clone_factory_fail_stage = 0;
    return TRUE;
}
#else
#define clone_factory_injected(stage) FALSE
#endif

LONG proc_create_clone_process(KPROC *source, UWORD resources, KPROC **result)
{
    static const char empty_environment[2] = { 0, 0 };
    PD *parent, *p = NULL;
    char *env;
    LONG size, error = ENSMEM;
    KPROC *process;

    if (!result || *result || (resources & ~PROC_CLONE_RESOURCE_MASK)) return EINVAL;
    parent = kproc_live_pd(source);
    if (!parent) return EACCDN;
    /* Existing allocation helpers map their temporary construction buffers in
     * run. Restrict this first factory to its actual calling PROCESS rather
     * than switching global execution identity or borrowing a foreign PD. */
    if (parent != run) return EINVFN;
    env = alloc_env(PF_STANDARD, (char *)empty_environment);
    if (!env) return ENSMEM;
    if (clone_factory_injected(1)) goto raw_failed;
    p = (PD *)alloc_tpa(PF_STANDARD, sizeof(PD), &size);
    if (!p || clone_factory_injected(2)) goto raw_failed;
    init_pd_fields(p, "", size, env);
    p->p_flags = PF_STANDARD;
    set_owner(p, p); set_owner(env, p);
    if (!kproc_create(p)) goto raw_failed;
    process = kproc_retain(p); /* freshly created lookup: exactly one ref */
    if (!process) panic("clone factory: missing new identity");
    if (clone_factory_injected(3)) goto record_failed;
    if (!kproc_clone_inherit_identity(process, source)) { error = EACCDN; goto record_failed; }
    if (resources & PROC_CLONE_COPY_STANDARD_HANDLES) {
        error = init_pd_standard_files_from(p, parent);
        if (error != E_OK) goto record_failed;
    }
    error = ENSMEM;
    if (clone_factory_injected(4)) goto record_failed;
    if (resources & PROC_CLONE_COPY_CWD) init_pd_cwd_from(p, parent);
    if (clone_factory_injected(5)) goto record_failed;
    /* No image staging and no loader grant survives resource construction. */
    kproc_unborrow(env); kproc_unborrow(p);
    *result = process;
    return E_OK;
record_failed:
    proc_dispose_process(process);
    kproc_release(process);
    return error;
raw_failed:
    xexec_free(env);
    if (p) xexec_free(p);
    return ENSMEM;
}
#endif

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
#if CONF_WITH_PROCMEM
    /*
     * Same reasoning as alloc_tpa()'s own CONF_WITH_PROCMEM branch
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
    new_env = (char *)procmem_alloc(size, PROCMEM_ZERO);
#if CONF_WITH_USER_ASPACE
    /* built below under the launcher's page tables: lend it to a launcher
     * with an address space of its own */
    if (new_env && !kproc_borrow(run, new_env)) {
        procmem_free(new_env);
        new_env = NULL;
    }
#endif
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

#if CONF_WITH_PROCMEM
    /*
     * Process memory comes from the procmem window: there is no ST/alternate-RAM distinction to route through
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
        UBYTE *low = procmem_alloc(needed + 15, PROCMEM_ZERO);

#if CONF_WITH_USER_ASPACE
        if (low && !kproc_borrow(run, low)) {   /* see alloc_env() */
            procmem_free(low);
            low = NULL;
        }
#endif
        if (low)
            *avail = needed + 15;
        return low;
    }
#endif

    st_ram_size = (LONG)(uintptr_t)ffit(-1L, &pmd);
    if (st_ram_size >= needed)
        st_ram_available = TRUE;

#if CONF_WITH_ALT_RAM
    {
        LONG alt_ram_size = 0L, tpasize;
        BOOL alt_ram_available = FALSE;

        if (has_alt_ram && (flags & PF_TTRAMLOAD)) {
            alt_ram_size = (LONG)(uintptr_t)ffit(-1L, &pmdalt);
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

/* Shared launcher entry for a new PROCESS or an already prepared THREAD. */
static void proc_enter_user(WORD entry_type)
{
#if ARCH_X86_64
    gouser_entry(entry_type);
#else
    (void)entry_type;
#if ARCH_ARM && !CONF_WITH_USER_ASPACE
    /* gouser() would store whatever r0 happens to hold over the basepage
     * proc_launch_prepare() put there for the new process to find in r0 */
    termuser();
#else
    gouser();
#endif
#endif
}

static void proc_go(PD *p, WORD entry_type)
{
#if CONF_WITH_NATIVE_SCHED
    PD *controller=ksched_launch_controller(p);
#endif
    KDEBUG(("BDOS xexec: trying to load (and execute) a process on %p ...\n",p->p_tbase));
    /* PTR_TO_USERPTR(), not the unchecked cast: `run` here is always
     * either a real, alloc_tpa()'d PD or the first process's parent,
     * initial_basepage (bdosmain.c) -- also low on x86-64 since #360's
     * review (bdosmain.c's osinit_after_xmaddalt() now allocates it from
     * the same pool). Checked because xterm()/ixterm() widen p_parent
     * back and dereference it on every Pterm(), so a truncated value
     * here would fault there instead of trapping at the point of
     * corruption. */
#if CONF_WITH_NATIVE_SCHED
    p->p_parent=PTR_TO_USERPTR(controller ? controller : run);
#else
    p->p_parent = PTR_TO_USERPTR(run);
#endif
#if CONF_WITH_KPROC
#if CONF_WITH_NATIVE_SCHED
    if (!controller && !kproc_clone_startup(p))
#endif
        kproc_set_parent(p, run);   /* clone parent retain was prepared before commit */
#endif
#if CONF_WITH_USER_ASPACE
    /* A ring-3 process cannot see its launcher's basepage, nor the
     * launcher's own: it gets read-only copies at a fixed address instead. */
    if (kproc_ancestors_va(p))
        p->p_parent = PTR_TO_USERPTR_UNCHECKED((PD *)(uintptr_t)kproc_ancestors_va(p));
#endif

    proc_launch_prepare(p, &supstk[SUPSIZ]);   /* arch: launch.c */

    /* the new process is the one to run */
#if CONF_WITH_KPROC
    kproc_mark_started(p);
#endif
    run = (PD *)p;

    proc_enter_user(entry_type);
}

#if CONF_WITH_NATIVE_SCHED
LONG proc_prepare_scheduled(PD *p)
{
    if (!kproc_unlaunched(p)) return EIMBA;
    give_back_image(p, FALSE);
    return kproc_prepare_async(p,run,ksched_controller_pd()) ? E_OK : ENSMEM;
}
LONG proc_start_scheduled(PD *p, BOOL take_ownership)
{
    if (ksched_sibling_launch(p)) {
        /* Existing PROCESS: do not reset parent, loader state or start time.
         * The existing launcher consumes this task's independent startup. */
        run = p;
        proc_enter_user(0);
        return run->p_dreg[0];
    }
    if (!kproc_unlaunched(p) || !kproc_user_root(p)) return EIMBA;
    if (take_ownership) {
        set_owner(p, p);
        set_owner(USERPTR_TO_PTR(p->p_env), p);
    }
    proc_go(p, 0);
    return run->p_dreg[0];
}
#endif

#if ARCH_X86_64
LONG proc_prepare_accessory(PD *p)
{
    if (!kproc_unlaunched(p))
        return EIMBA;
    give_back_image(p, FALSE);
    return kproc_prepare_user(p, run) ? E_OK : ENSMEM;
}

LONG proc_start_accessory(PD *p)
{
    if (!kproc_unlaunched(p) || !kproc_user_root(p))
        return EIMBA;
    set_owner(p, p);
    set_owner(USERPTR_TO_PTR(p->p_env), p);
    proc_go(p, ENTRY_ACCESSORY);
    return run->p_dreg[0];
}

#endif

#if ARCH_X86_64 || CONF_WITH_NATIVE_SCHED
void proc_discard_scheduled(PD *p)
{
    if (!kproc_unlaunched(p))
        return;
    set_owner(p, p);
    set_owner(USERPTR_TO_PTR(p->p_env), p);
#if CONF_WITH_USER_ASPACE
    proc_dispose_retained(p);
#else
    proc_dispose_detached(p);
#endif
}
#endif

#if ARCH_X86_64
void proc_discard_accessory(PD *p)
{
    proc_discard_scheduled(p);
}

#if CONF_WITH_CUIS
/* Pending successors were loaded with child-owned allocations. Their public
 * basepage is not mapped in the requesting process after async preparation. */
void proc_dispose_deferred(PD *p)
{
#if CONF_WITH_USER_ASPACE
    proc_dispose_retained(p);
#else
    proc_release_resources(p, FALSE);
#endif
}
#endif

void proc_dispose_detached(PD *p)
{
#if CONF_WITH_USER_ASPACE
    proc_dispose_retained(p);
#else
    proc_release_resources(p, FALSE);
#endif
}
#endif

#if CONF_WITH_USER_ASPACE
void proc_dispose_retained(PD *p)
{
    KPROC *process = kproc_retain(p);
    if (!process)
        return;
    proc_dispose_process(process);
    kproc_release(process);
}

void proc_dispose_process(KPROC *process)
{
    PD *p;
    UWORD status = 0;
    BOOL complete;
    if (!kproc_begin_disposal(process, &p))
        return;
    complete = kproc_pending_completion(process, &status);
#if CONF_WITH_X86_64_MEMTEST
    x86_64_memtest_disposal_committed(p);
#endif
    if (complete) {
        kproc_complete(p, status);
#if CONF_WITH_NATIVE_SCHED
        ksched_exiting(p, status);
#endif
    }
    proc_release_resources(p, FALSE);
#if CONF_WITH_CUIS
    if (complete)
        ksched_exited(p);
#endif
    kproc_publish_observers(process);
}

/* The launcher calls this only after leaving the child's syscall stack and
 * restoring its own address space. The retained identity owns no stack. */
void proc_finalize_exit(KPROC *process, UWORD status, BOOL resident, ULONG resident_len)
{
    PD *p;
    FS_ERROR_STATE saved_error;
#if CONF_WITH_NATIVE_SCHED
    BOOL managed;
#endif

    /* Only the disk-error unwinder is supported by cleanup callbacks.
     * kdos_fclose catches recoverable errors locally. An escaping disk error
     * violates the no-unwind contract of the remaining teardown callbacks:
     * do not retry a partially committed cleanup or return to a stale trap.
     * On this fatal path consume the caller's handoff reference ourselves;
     * panic must not return. Arbitrary private longjmp targets are forbidden.
     */
    saved_error = *fs_error_current();
    if (setjmp(errbuf)) {
        *fs_error_current() = saved_error;
#if CONF_WITH_PROCMEM
        proc_resident_pd = NULL;
        proc_resident_len = 0;
#endif
        kproc_release(process);
        panic("exit finalization: cleanup callback escaped");
    }
    if (!kproc_commit_exit_handoff(process, &p))
        panic("invalid exit handoff");
    kproc_complete(p, status);
#if CONF_WITH_NATIVE_SCHED
    managed = ksched_finalizing_process(process);
    ksched_exiting(p, status);
#endif
#if CONF_WITH_PROCMEM
    if (resident) {
        proc_resident_pd = p;
        proc_resident_len = resident_len;
    }
#else
    UNUSED(resident);
    UNUSED(resident_len);
#endif
    proc_release_resources(p, TRUE);
#if CONF_WITH_CUIS
    ksched_exited_identity(process, p);
#endif
#if CONF_WITH_NATIVE_SCHED
    if (!managed)
#endif
        kproc_publish_observers(process);
    *fs_error_current() = saved_error;
}

BOOL proc_prepare_completion(KPROC *process, struct kthread *thread, UWORD status)
{
#if CONF_WITH_NATIVE_SCHED
    /* Runnable scheduler integration must account for task completion,
     * orphaning and its separate stack handoff before using this path. */
    if (ksched_active())
        return FALSE;
#endif
    if (!kproc_request_completion(process, thread, status))
        return FALSE;
    return TRUE;
}
#endif


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
#if CONF_WITH_USER_ASPACE
    KPROC *fallback_observer = NULL;
#endif
#if CONF_WITH_NATIVE_SCHED
    KTHREAD *thread = ksched_exit_thread(p);
#if CONF_WITH_PTERMGRP
    if (thread) {
        ksched_enter_exit(p);
        (void)kproc_group_status(kthread_process(thread), &rc);
    }
#endif
#endif

#if CONF_WITH_USER_ASPACE
#if CONF_WITH_NATIVE_SCHED
    kproc_require_exit_thread(p, thread);
#else
    kproc_require_no_threads(p);
#endif
#endif
#if CONF_WITH_X86_64_MEMTEST && CONF_WITH_USER_CALLBACKS
    x86_64_memtest_callback_exit(p, rc);
#endif

#if CONF_WITH_NATIVE_SCHED
    if (!thread || kproc_thread_is_sole(kthread_process(thread), thread))
#endif
    {
        userterm = (PFVOID)Setexc(0x102, (long)-1L);
        protect_v((PFLONG)userterm); /* process-wide hook, preserving d2/a2 */
    }
#if CONF_WITH_USER_ASPACE
    {
        BOOL resident = FALSE;
        ULONG resident_len = 0;
#if CONF_WITH_PROCMEM
        if (proc_resident_pd == p) {
            resident = TRUE;
            resident_len = proc_resident_len;
        }
#endif
        if (proc_defer_exit(p, rc, resident, resident_len)) {
#if CONF_WITH_PROCMEM
            if (resident) proc_resident_pd = NULL;
#endif
            run = kproc_get_parent(p);
            run->p_dreg[0] = rc;
            termuser();
        }
    }
#endif
#if CONF_WITH_USER_ASPACE
    /* A backend-associated execution must leave through its verified launcher
     * handoff. Never fall back to in-context teardown of an attached thread. */
    kproc_require_no_threads(p);
    if (!kproc_begin_exit(p))
        panic("xterm: process disposal already committed");
    fallback_observer = kproc_retain(p);
    if (!fallback_observer) panic("fallback observer retention failed");
    if (proc_defer_observer(p, fallback_observer)) fallback_observer = NULL;
#endif
#if CONF_WITH_KPROC
    kproc_complete(p, rc);
#endif
#if CONF_WITH_NATIVE_SCHED
    ksched_exiting(p, rc);
#endif
#if CONF_WITH_KPROC
    /* not run->p_parent: that field is in the process's own writable
     * memory, and run->p_dreg[0] is written through the result below */
    run = kproc_get_parent(p);
#else
    run = (PD *)USERPTR_TO_PTR(run->p_parent);
#endif
    proc_release_resources(p, TRUE);
#if CONF_WITH_CUIS
    /* All process resources are gone. No cleanup can return to the caller
     * through the disk-error handler after making its successor runnable. */
    ksched_exited(p);
#endif
    /* gouser() will store the current value of D0 in the active PD
     * so it cannot be used here. See proc_go() above.
     * termuser() will enter the gouser() code at the proper place.
     * sep 2005 RCL
     */
#if CONF_WITH_USER_ASPACE
    /* A kernel-code child has no separate syscall stack. User children defer
     * this publication to their retained launcher, after stack reclamation. */
    if (fallback_observer) {
        kproc_publish_observers(fallback_observer);
        kproc_release(fallback_observer);
    }
#endif
    run->p_dreg[0] = rc;
    termuser();
}


#if CONF_WITH_USER_ASPACE
void proc_fault(const char *cause, UQUAD code, UQUAD address, UQUAD pc, UQUAD sp)
{
    ULONG word[4];

    kcprintf("process fault: %s code=0x%lx addr=0x%lx pc=0x%lx sp=0x%lx\n",
             cause, (long)code, (long)address, (long)pc, (long)sp);
#if CONF_WITH_NATIVE_SCHED
    ksched_fault(run, 11); /* Keep a specific CPU/paging signal if already recorded. */
#else
    kproc_note_fault(run, 11);
#endif
#if CONF_WITH_USER_CALLBACKS
    user_callback_fault();
#endif
    if (kproc_copy_from_user(word, sp, sizeof(word)))
        kcprintf("  stack: 0x%lx 0x%lx 0x%lx 0x%lx\n",
                 (long)word[0], (long)word[1], (long)word[2], (long)word[3]);
    xterm((UWORD)-1);
}
#endif

/*
 * xtermres - Function 0x31 (Ptermres)
 */
long xtermres(long blkln, WORD rc)
{
#if CONF_WITH_NATIVE_SCHED
    KTHREAD *thread = ksched_exit_thread(run);
    if (thread && !kproc_thread_is_sole(kthread_process(thread), thread))
        return EACCDN;
#endif
#if ARCH_X86_64
    /* An accessory is owned by the AES session and must drain on restart. */
    if (x86_64_process_is_accessory())
        return EACCDN;
#endif
    xsetblk(0,run,blkln);

    reserve_blocks(run, &pmd);
#if CONF_WITH_ALT_RAM
    if (has_alt_ram)
        reserve_blocks(run, &pmdalt);
#endif
#if CONF_WITH_PROCMEM
    /* The window's blocks are not MPB descriptors, so xsetblk() above cannot
     * shrink them: xterm() does it, and makes the rest resident, once the
     * address space that maps them is destroyed. */
    proc_resident_pd = run;
    proc_resident_len = (ULONG)blkln;
#endif
    xterm(rc);
}
