/* Pclone GEMDOS snapshot/validation boundary; trusted startup is separate. */
#include "emutos.h"
#include "gemerror.h"
#include "clone.h"
#include "biosext.h"
#if CONF_WITH_USER_ASPACE
#include "fs.h"
#include "bdosstub.h"
#include "kproc.h"
#include "clone_args.h"
#if CONF_WITH_NATIVE_SCHED
#include "proc_clone.h"
#include "ksched.h"
#if CONF_WITH_PRIVATE_CLONE_TEST
#include "kthread.h"
#include "kaspace.h"
#include "procmem.h"
#include "kheap.h"
#include "shared_pexec_test.h"
#include "proc.h"
#endif
#endif

static int clone_copy(void *context, uint64_t address, void *dst, uint32_t size)
{
    LONG *error = context;
    *error = kproc_copy_from_user_checked(dst, (VADDR)address, size);
    return *error == E_OK;
}

static int clone_access(void *context, uintptr_t address, uint32_t size, int kind)
{
    (void)context;
    if (kind == CLONE_ACCESS_EXEC) return kproc_validate_user_exec((VADDR)address);
    return kproc_user_access(run, (VADDR)address, size, TRUE);
}
#endif

static long clone_request(uint64_t address, uint64_t size)
{
#if CONF_WITH_USER_ASPACE
    CLONE_ARGS args;
    LONG copy_error = E_OK;
    enum clone_copy_result copied;
    enum clone_validate_result valid;
    static const CLONE_ABI abi = { CONF_PCLONE_STACK_ALIGNMENT,
        CONF_PCLONE_MIN_STACK, CONF_PCLONE_ENTRY_ALIGNMENT };
#if CONF_WITH_PRIVATE_CLONE_TEST
    BOOL private_test = FALSE;
#endif
#if CONF_WITH_SHARED_VM_PEXEC_TEST && CONF_WITH_PRIVATE_CLONE_TEST
    if(size==SHARED_PEXEC_PROTECTED_QUERY) {
        SHARED_PEXEC_PROTECTED probe;
        if(kproc_copy_from_user_checked(&probe,(VADDR)address,sizeof(probe)) ||
           !aspace_copy_to_user_ok(kproc_user_aspace(run),(VADDR)address,sizeof(probe))) return EIMBA;
        probe.tid_before=kthread_tid(ksched_exit_thread(run));
        probe.result=proc_shared_vm_exec((PD *)(uintptr_t)probe.image,FALSE);
        probe.pending=ksched_cancellation_pending();
        probe.tid_after=kthread_tid(ksched_exit_thread(run));
        /* xclone's enclosing cleanup scope owns this private experiment.
         * Its ordinary final checkpoint must deliver the latched request. */
        if(!kproc_copy_to_user_resident((VADDR)address,&probe,sizeof(probe)))
            panic("shared test lost resident result");
        return E_OK;
    }
    if(!address && size>=0xc8940900UL && size<=0xc89409ffUL) {
        kproc_shared_test_fail_after((UWORD)(size&255));return E_OK;
    }
    if(!address && size==0xc8940a00UL) return kproc_shared_test_steps();
    if(size==SHARED_PEXEC_SNAPSHOT_QUERY) {
        SHARED_PEXEC_SNAPSHOT out={ {0} };KHEAP_STATS heap;ULONG i;
        /* Preflight only resident output. The read itself cannot fault,
         * allocate, reap pending work, retain an owner, callback or yield. */
        if(!address || address>=USER_VA_LIMIT ||
           !aspace_copy_to_user_ok(kproc_user_aspace(run),(VADDR)address,sizeof(out))) return EIMBA;
        out.count[SP_PROCESSES]=kproc_count();
        out.count[SP_IDENTITIES]=kproc_test_identities(FALSE);
        out.count[SP_PROCESS_REFS]=kproc_test_identities(TRUE);
        out.count[SP_THREADS]=kthread_test_live();
        out.count[SP_CURRENT_PRIVATE]=aspace_private_pages(kproc_user_aspace(run));
        procmem_shared_snapshot(&out);kaspace_shared_snapshot(kproc_user_aspace(run),&out);
        out.count[SP_KSTACKS]=kstack_test_live();
        out.count[SP_SCHEDSTACKS]=(ULONG)ksched_test_nested(14);
        kheap_stats(&heap);out.count[SP_KHEAP_BLOCKS]=heap.live_blocks;
        out.count[SP_KHEAP_PAGES]=heap.pages;out.count[SP_KHEAP_BAD_FREES]=heap.bad_frees;
#if CONF_WITH_MMAP
        out.count[SP_FILE_PAGES]=aspace_file_pages();out.count[SP_FILE_REFS]=aspace_file_references();
        fileback_shared_snapshot(&out);
#endif
#if CONF_WITH_PRIVATE_CLONE
        out.count[SP_COW_FRAMES]=aspace_cow_frames();out.count[SP_COW_REFS]=aspace_cow_references();
#endif
        for(i=0;i<OPNFILES;i++) {
            out.count[SP_SFT_USES]+=sft[i].f_use;
            out.count[SP_SFT_STAGED]+=sft[i].f_staged_refs;
            out.count[SP_SFT_CLEANUP]+=sft[i].f_cleanup_refs;
        }
        for(i=0;i<NCURDIR;i++) out.count[SP_DIRECTORY_USES]+=dirtbl[i].use;
        return kproc_copy_to_user_resident((VADDR)address,&out,sizeof(out))?E_OK:EIMBA;
    }
#endif
#if CONF_WITH_PRIVATE_CLONE_TEST
    if ((size & 0xffffffffffff0000ULL) == 0xc8920000ULL) {
        if (!address && size >= 0xc8920100ULL) {
            KPROC *source = kproc_retain(run);
            LONG value = EINVFN;
            if (source) {
                if (size == 0xc8920100ULL) value = (LONG)kproc_user_root(run);
                if (size == 0xc8920101ULL) value = aspace_cow_frames();
                if (size == 0xc8920102ULL) value = aspace_cow_references();
                if (size == 0xc8920103ULL) {
                    KTHREAD *thread = ksched_exit_thread(run);
                    if (thread) value = kthread_tid(thread);
                }
                if (size >= 0xc8920200ULL && size <= 0xc89202ffULL) {
                    aspace_clone_test_fail_after((ULONG)size & 255); value = E_OK;
                }
                if (size == 0xc8920300ULL || size == 0xc8920301ULL) {
                    aspace_cow_test_fail_allocation(size == 0xc8920301ULL); value = E_OK;
                }
                if (size >= 0xc8920400ULL && size <= 0xc8920405ULL) {
                    proc_clone_test_fail_after((UWORD)(size & 255)); value = E_OK;
                }
                if (size >= 0xc8920500ULL && size <= 0xc8920505ULL) {
                    proc_clone_factory_test_fail_after((UWORD)(size & 255)); value = E_OK;
                }
                if (size == 0xc8920600ULL) {
                    proc_clone_test_cancel_publication(); value = E_OK;
                }
                /* Nested continuation ownership (#713): scheduler counters and
                 * one-shot injections, then global identity/resource counts. */
                if (size >= 0xc8920700ULL && size <= 0xc892077fULL)
                    value = ksched_test_nested((UWORD)(size & 255));
                if (size == 0xc8920780ULL) value = (LONG)kproc_count();
                if (size == 0xc8920781ULL) value = (LONG)kthread_test_live();
                if (size == 0xc8920782ULL) value = (LONG)kaspace_test_live();
                if (size == 0xc8920783ULL) value = (LONG)kstack_test_live();
                if (size == 0xc8920784ULL) value = (LONG)kproc_pid(run);
                if (size == 0xc8920785ULL) value = (LONG)kproc_test_identities(FALSE);
                if (size == 0xc8920786ULL) value = (LONG)kproc_test_identities(TRUE);
                if (size == 0xc8920787ULL) value = (LONG)kaspace_test_references();
                if (size == 0xc8920788ULL || size == 0xc8920789ULL) {
                    PROCMEM_STATS stats;
                    procmem_stats(&stats);
                    value = size == 0xc8920788ULL ? (LONG)stats.live_allocs : (LONG)stats.free_pages;
                }
                if (size == 0xc892078aULL) {
                    KHEAP_STATS stats;
                    kheap_stats(&stats); value = (LONG)stats.live_blocks;
                }
                kproc_release(source);
            }
            return value;
        }
        private_test = TRUE; size &= 0xffff;
    }
#endif
    copied = clone_copy_v1(&args, address, size, clone_copy, &copy_error);
    if (copied == CLONE_COPY_BAD_SIZE) return EINVAL;
    if (copied != CLONE_COPY_OK) return copy_error != E_OK ? copy_error : EIMBA;
    valid = clone_validate_v1(&args, &abi, clone_access, NULL);
    if (valid == CLONE_INVALID) return EINVAL;
    if (valid != CLONE_VALID) return EIMBA;
    if (!(args.flags & CLONE_VM)) return EINVFN;
#if CONF_WITH_PRIVATE_CLONE_TEST
    if (private_test && ((args.flags & CLONE_SAME_PROCESS) || args.child_pid || args.child_tid)) return EINVAL;
#endif
#if CONF_WITH_NATIVE_SCHED
    {
        PROC_CLONE_ARGS policy;
        THREAD_STARTUP_ARGS startup;
        LONG result;
        policy.source = kproc_retain(run);
        if (!policy.source) return EINVFN;
        policy.prepared_process = NULL;
        policy.process_policy = (args.flags & CLONE_SAME_PROCESS) ? PROC_CLONE_SAME_PROCESS : PROC_CLONE_NEW_PROCESS;
        policy.vm_policy =
#if CONF_WITH_PRIVATE_CLONE_TEST
            private_test ? PROC_CLONE_PRIVATE_VM :
#endif
            PROC_CLONE_SHARE_VM;
        policy.resources = ((args.flags & CLONE_COPY_STANDARD_HANDLES) ? PROC_CLONE_COPY_STANDARD_HANDLES : 0) |
                           ((args.flags & CLONE_COPY_CWD) ? PROC_CLONE_COPY_CWD : 0);
        startup.entry = (VADDR)(uintptr_t)args.entry;
        startup.argument = (VADDR)(uintptr_t)args.argument;
        startup.stack_base = (VADDR)(uintptr_t)args.stack_base;
        startup.stack_size = args.stack_size;
        result =
#if CONF_WITH_PRIVATE_CLONE_TEST
            private_test ? proc_clone_start_private_test(&policy, &startup) :
#endif
            proc_clone_start_public(&policy, &startup,
                    (VADDR)(uintptr_t)args.child_pid, (VADDR)(uintptr_t)args.child_tid);
        kproc_release(policy.source);
        return result;
    }
#endif
#else
    (void)address;
    (void)size;
#endif
    /* Configurations without the native execution backend remain unsupported. */
    return EINVFN;
}

long xclone(uint64_t address, uint64_t size)
{
#if CONF_WITH_USER_ASPACE && CONF_WITH_NATIVE_SCHED
    KSCHED_CLEANUP_SCOPE scope = { 0 };
    LONG result;
    /* Factory rollback may release files and yield. Keep local transactions
     * alive until cleanup ends; osif owns the later cancellation checkpoint. */
    ksched_cleanup_begin(&scope);
    result = clone_request(address, size);
    ksched_cleanup_end(&scope);
    return result;
#else
    return clone_request(address, size);
#endif
}
