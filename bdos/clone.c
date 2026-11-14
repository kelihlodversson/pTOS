/* Pclone GEMDOS snapshot/validation boundary; trusted startup is separate. */
#include "emutos.h"
#include "gemerror.h"
#include "clone.h"
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
#include "mapping_lifetime_test.h"
#include "fileback.h"
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

#if CONF_WITH_USER_ASPACE && CONF_WITH_NATIVE_SCHED && CONF_WITH_PRIVATE_CLONE_TEST
static LONG mapping_snapshot(VADDR output)
{
    MAPPING_LIFETIME_SNAPSHOT snapshot={ {0} };
    uint32_t *v=snapshot.value;
    ASPACE *as=kproc_user_aspace(run);
    PROCMEM_STATS memory;
    KHEAP_STATS heap;
    int i,j;
    if (!as || !aspace_copy_to_user_ok(as,output,sizeof(snapshot))) return EIMBA;
    kaspace_test_mapping_snapshot(as,v);
    v[MLT_PROCESSES]=kproc_count();v[MLT_IDENTITIES]=kproc_test_identities(FALSE);
    v[MLT_PROCESS_REFS]=kproc_test_identities(TRUE);v[MLT_THREADS]=kthread_test_live();
    v[MLT_ROOTS]=kaspace_test_live();v[MLT_SPACE_REFS]=kaspace_test_references();
    v[MLT_KSTACKS]=kstack_test_live();v[MLT_SCHEDSTACKS]=ksched_test_nested(0x0e);
    procmem_stats(&memory);kheap_stats(&heap);
    v[MLT_ALLOCS]=memory.live_allocs;v[MLT_FREE_PAGES]=memory.free_pages;v[MLT_BAD_FREES]=memory.bad_frees;
    v[MLT_ALLOC_HOLDS]=procmem_test_ownership(0);v[MLT_ALLOC_PINS]=procmem_test_ownership(1);
    v[MLT_ALLOC_PENDING]=procmem_test_ownership(2);
    v[MLT_KHEAP_BLOCKS]=heap.live_blocks;v[MLT_KHEAP_PAGES]=heap.pages;v[MLT_KHEAP_BAD_FREES]=heap.bad_frees;
    v[MLT_PRIVATE_PAGES]=aspace_private_pages(as);v[MLT_TABLES]=aspace_table_pages(as);
    v[MLT_PINS]=aspace_test_pin_count(as,FALSE);v[MLT_RETIRED_PINS]=aspace_test_pin_count(as,TRUE);
    v[MLT_COW_FRAMES]=aspace_cow_frames();v[MLT_COW_REFS]=aspace_cow_references();
#if CONF_WITH_MMAP
    v[MLT_FILE_PAGES]=aspace_file_pages();v[MLT_FILE_REFS]=aspace_file_references();
    v[MLT_BACKINGS]=fileback_test_count(FALSE);v[MLT_BACKING_REFS]=fileback_test_count(TRUE);
    v[MLT_OFD_REFS]=fileback_test_orphan_ofd_refs();
#endif
    for (i=0;i<OPNFILES;i++) if (sft[i].f_use) {
        v[MLT_SFT_SLOTS]++;v[MLT_SFT_REFS]+=sft[i].f_use;
        if (!sft[i].f_ofd || IS_BIOS_HANDLE(sft[i].f_ofd)) continue;
        for (j=0;j<i;j++) if (sft[j].f_use && sft[j].f_ofd==sft[i].f_ofd) break;
#if CONF_WITH_FILE_PINS
        if (j==i) v[MLT_OFD_REFS]+=sft[i].f_ofd->o_refs;
#endif
    }
    /* No fault, retain, reap or checkpoint: only resident checked copyout. */
    return aspace_copy_to_user(as,output,&snapshot,sizeof(snapshot))?E_OK:EIMBA;
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
    BOOL scoped_test = FALSE;
    if ((size&0xffffffffffff0000ULL)==MLT_SCOPED_CLONE) {
        if (address>=USER_VA_LIMIT) return EIMBA;
        if (size==MLT_SNAPSHOT) return mapping_snapshot((VADDR)address);
        if (size>=MLT_INFO && size<=MLT_DETACH) {
            MAPPING_LIFETIME_CLAIM claim;
            LONG error;
            ASPACE *as=kproc_user_aspace(run);
            if (!as || !aspace_copy_to_user_ok(as,(VADDR)address,sizeof(claim)) ||
                !aspace_copy_from_user(as,&claim,(VADDR)address,sizeof(claim))) return EIMBA;
            error=kproc_test_mapping_claim((ULONG)size,&claim);
            if (!error && !aspace_copy_to_user(as,(VADDR)address,&claim,sizeof(claim))) return EIMBA;
            return error;
        }
        if (!address && size>=0xc9670200ULL && size<=0xc96702ffULL) {
            kaspace_test_fail_mapping_after((UWORD)(size&255));return E_OK;
        }
        scoped_test=TRUE;size&=0xffff;
    }
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
    if ((private_test && ((args.flags & CLONE_SAME_PROCESS) || args.child_pid || args.child_tid)) ||
        (scoped_test && (args.flags&CLONE_SAME_PROCESS))) return EINVAL;
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
            scoped_test ? PROC_CLONE_SCOPED_VM :
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
            scoped_test ? proc_clone_start_scoped_test(&policy,&startup,
                    (VADDR)(uintptr_t)args.child_pid,(VADDR)(uintptr_t)args.child_tid) :
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
