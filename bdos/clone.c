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
#if CONF_WITH_PRIVATE_CLONE_TEST
#include "kthread.h"
#include "ksched.h"
#include "procmem.h"
#endif
#endif

static int clone_copy(void *context, uint64_t address, void *dst, uint32_t size)
{
    (void)context;
    return kproc_copy_from_user(dst, (VADDR)address, size);
}

static int clone_access(void *context, uintptr_t address, uint32_t size, int kind)
{
    (void)context;
    if (kind == CLONE_ACCESS_EXEC) return kproc_validate_user_exec((VADDR)address);
    return kproc_user_access(run, (VADDR)address, size, TRUE);
}
#endif

long xclone(uint64_t address, uint64_t size)
{
#if CONF_WITH_USER_ASPACE
    CLONE_ARGS args;
    enum clone_copy_result copied;
    enum clone_validate_result valid;
    static const CLONE_ABI abi = { CONF_PCLONE_STACK_ALIGNMENT,
        CONF_PCLONE_MIN_STACK, CONF_PCLONE_ENTRY_ALIGNMENT };
#if CONF_WITH_PRIVATE_CLONE_TEST
    BOOL private_test = FALSE;
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
                kproc_release(source);
            }
            return value;
        }
        private_test = TRUE; size &= 0xffff;
    }
#endif
    copied = clone_copy_v1(&args, address, size, clone_copy, NULL);
    if (copied == CLONE_COPY_BAD_SIZE) return EINVAL;
    if (copied != CLONE_COPY_OK) return EIMBA;
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
