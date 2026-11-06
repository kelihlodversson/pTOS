#include "emutos.h"
#include "gemerror.h"
#include "kheap.h"
#include "kaspace.h"
#include "procmem.h"
#include "thread_startup.h"
#if CONF_WITH_NATIVE_SCHED
#include "thread_startup_backend.h"
#include "ksched.h"
#include "biosext.h"
#include "setjmp.h"
#include "string.h"
extern jmp_buf errbuf;
struct thread_startup_draft {
    KASPACE *space;
    THREAD_STARTUP_ARGS args;
    void *image;
    void *bridge;
    KASPACE_LEASE *leases[5]; /* entry, bridge, stack, PID output, TID output */
    THREAD_STARTUP_STATE state;
    BOOL outputs_prepared;
    VADDR outputs[2]; /* exact destinations retained; never written yet */
    THREAD_STARTUP_OBSERVER observer;
    void *observer_context;
    void *frame_alias, *output_aliases[2];
};
#endif

static LONG startup_prepare(KASPACE *space, const THREAD_STARTUP_ARGS *args,
                            THREAD_STARTUP_DRAFT **result, BOOL public_return)
{
    if (!result || *result || !space || !args) return EINVAL;
#if CONF_WITH_NATIVE_SCHED
    {
        THREAD_STARTUP_DRAFT *draft;
        LONG error;
        UWORD i;
        error = thread_startup_arch_validate(kaspace_backend(space), args);
        if (error) return error;
        draft = kalloc(sizeof(*draft));
        if (!draft) return ENSMEM;
        draft->space = kaspace_share(space);
        if (!draft->space) { kfree(draft); return ENSMEM; }
        for (i = 0; i < 5; i++) draft->leases[i] = NULL;
        draft->state = THREAD_STARTUP_PREPARED;
        draft->outputs_prepared = FALSE;
        draft->outputs[0] = draft->outputs[1] = 0;
        draft->observer.acquire = NULL;
        draft->observer.release = NULL;
        draft->observer_context = NULL;
        draft->frame_alias = draft->output_aliases[0] = draft->output_aliases[1] = NULL;
        draft->args = *args;
        draft->image = NULL;
        draft->bridge = NULL;
        error = thread_startup_arch_bridge_acquire(space, &draft->bridge);
        if (!error) error = thread_startup_arch_prepare(kaspace_backend(space), &draft->args, draft->bridge, &draft->image);
        if (!error && public_return) error = thread_startup_arch_public_return(draft->image, draft->bridge);
        if (!error) error = kaspace_lease_acquire(space, args->entry, 1,
            ASPACE_PROT_USER | ASPACE_PROT_EXEC, &draft->leases[0]);
        if (!error) error = kaspace_lease_acquire(space, thread_startup_arch_bridge_address(draft->bridge), 1,
            ASPACE_PROT_USER | ASPACE_PROT_EXEC, &draft->leases[1]);
        /* Entire supplied stack, including alignment discard and red zone.
         * This intentionally protects more than just the minimal 136 bytes. */
        if (!error) error = kaspace_lease_acquire(space, args->stack_base, args->stack_size,
            ASPACE_PROT_USER | ASPACE_PROT_WRITE, &draft->leases[2]);
        if (error) { thread_startup_abort(&draft); return error; }
        *result = draft;
        return E_OK;
    }
#else
    (void)public_return; return EINVFN;
#endif
}
LONG thread_startup_prepare(KASPACE *space, const THREAD_STARTUP_ARGS *args, THREAD_STARTUP_DRAFT **out)
{
    return startup_prepare(space, args, out, FALSE);
}
LONG thread_startup_prepare_public(KASPACE *space, const THREAD_STARTUP_ARGS *args, THREAD_STARTUP_DRAFT **out)
{
    return startup_prepare(space, args, out, TRUE);
}

LONG thread_startup_validate(const THREAD_STARTUP_DRAFT *draft)
{
    if (!draft) return EINVAL;
#if CONF_WITH_NATIVE_SCHED
    {
        UWORD i;
        for (i = 0; i < 5; i++)
            if ((i < 3 || draft->leases[i]) && !kaspace_lease_valid(draft->leases[i])) return EIMBA;
        for (i = 0; i < 2; i++)
            if (draft->outputs[i] && !aspace_copy_to_user_ok(kaspace_backend(draft->space),
                    draft->outputs[i], 4)) return EIMBA;
        if (!thread_startup_arch_bridge_valid(draft->bridge)) return EIMBA;
        return thread_startup_arch_validate(kaspace_backend(draft->space), &draft->args);
    }
#else
    return EINVFN;
#endif
}

void thread_startup_abort(THREAD_STARTUP_DRAFT **result)
{
    if (!result || !*result) return;
#if CONF_WITH_NATIVE_SCHED
    {
        THREAD_STARTUP_DRAFT *draft = *result;
        UWORD i;
        if (draft->state == THREAD_STARTUP_COMMITTED || draft->state == THREAD_STARTUP_RUNNABLE)
            panic("startup abort after publication");
        *result = NULL;
        /* Leave the sealed window before any resource/observer callbacks. */
        for (i = 0; i < 5; i++) kaspace_lease_release(&draft->leases[i]);
        if (draft->observer.release) draft->observer.release(draft->observer_context);
        thread_startup_arch_abort(draft->image);
        thread_startup_arch_bridge_release(&draft->bridge);
        kaspace_put(draft->space);
        kfree(draft);
    }
#endif
}

const void *thread_startup_image(const THREAD_STARTUP_DRAFT *draft)
{
#if CONF_WITH_NATIVE_SCHED
    return draft ? draft->image : NULL;
#else
    (void)draft;
    return NULL;
#endif
}

THREAD_STARTUP_STATE thread_startup_state(const THREAD_STARTUP_DRAFT *draft)
{
#if CONF_WITH_NATIVE_SCHED
    return draft ? draft->state : THREAD_STARTUP_CONSTRUCTED;
#else
    (void)draft; return THREAD_STARTUP_CONSTRUCTED;
#endif
}

LONG thread_startup_prepare_outputs(THREAD_STARTUP_DRAFT *draft, VADDR pid, VADDR tid)
{
    if (!draft) return EINVAL;
#if CONF_WITH_NATIVE_SCHED
    {
        VADDR addresses[2];
        KASPACE_LEASE *leases[2] = { NULL, NULL };
        UWORD i;
        LONG error;
        if (draft->state != THREAD_STARTUP_PREPARED || draft->outputs_prepared) return EINVAL;
        error = thread_startup_validate(draft);
        if (error) return error;
        addresses[0] = pid; addresses[1] = tid;
        if (pid && tid && (UQUAD)pid < (UQUAD)tid + 4 && (UQUAD)tid < (UQUAD)pid + 4) return EINVAL;
        for (i = 0; i < 2; i++) if (addresses[i]) {
            UQUAD address = addresses[i], end = address + 4;
            if ((address & 3) || address >= USER_VA_LIMIT || end > USER_VA_LIMIT ||
                (address < (UQUAD)draft->args.stack_base + draft->args.stack_size &&
                 draft->args.stack_base < end) ||
                ((address & ~(UQUAD)(USER_PAGE_SIZE - 1)) <= draft->args.entry &&
                 draft->args.entry < ((end + USER_PAGE_SIZE - 1) & ~(UQUAD)(USER_PAGE_SIZE - 1))) ||
                ((address & ~(UQUAD)(USER_PAGE_SIZE - 1)) <= thread_startup_arch_bridge_address(draft->bridge) &&
                 thread_startup_arch_bridge_address(draft->bridge) < ((end + USER_PAGE_SIZE - 1) & ~(UQUAD)(USER_PAGE_SIZE - 1)))) {
                error = EINVAL; break;
            }
            if (!aspace_copy_to_user_ok(kaspace_backend(draft->space), addresses[i], 4)) {
                error = EIMBA; break;
            }
            error = kaspace_lease_acquire(draft->space, addresses[i], 4,
                    ASPACE_PROT_USER | ASPACE_PROT_WRITE, &leases[i]);
            if (error) break;
        }
        if (error) {
            for (i = 0; i < 2; i++) kaspace_lease_release(&leases[i]);
            return error;
        }
        draft->leases[3] = leases[0]; draft->leases[4] = leases[1];
        draft->outputs[0] = pid; draft->outputs[1] = tid;
        draft->outputs_prepared = TRUE;
        return E_OK;
    }
#else
    (void)pid; (void)tid; return EINVFN;
#endif
}

LONG thread_startup_observe(THREAD_STARTUP_DRAFT *draft,
                            const THREAD_STARTUP_OBSERVER *observer, void *context)
{
    if (!draft || !observer || !observer->acquire || !observer->release) return EINVAL;
#if CONF_WITH_NATIVE_SCHED
    {
        LONG error;
        if (draft->state != THREAD_STARTUP_PREPARED) return EINVAL;
        error = thread_startup_validate(draft);
        if (error) return error;
        error = observer->acquire(context);
        if (error) return error;
        error = thread_startup_validate(draft);
        if (error) { observer->release(context); return error; }
        draft->observer = *observer;
        draft->observer_context = context;
        draft->state = THREAD_STARTUP_OBSERVED;
        return E_OK;
    }
#else
    (void)context; return EINVFN;
#endif
}

LONG thread_startup_seal(THREAD_STARTUP_DRAFT *draft)
{
    if (!draft) return EINVAL;
#if CONF_WITH_NATIVE_SCHED
    {
        KASPACE_LEASE *leases[5];
        UWORD i, count = 0;
        LONG error;
        if (draft->state != THREAD_STARTUP_OBSERVED) return EINVAL;
        error = thread_startup_validate(draft);
        if (error) return error;
        for (i = 0; i < 5; i++) if (draft->leases[i]) leases[count++] = draft->leases[i];
        if (!kaspace_lease_seal(leases, count)) return EIMBA;
        draft->state = THREAD_STARTUP_SEALED;
        return E_OK;
    }
#else
    return EINVFN;
#endif
}

LONG thread_startup_publication_check(const THREAD_STARTUP_DRAFT *draft)
{
    LONG error = thread_startup_validate(draft);
    return error ? error : EINVFN; /* identity-only commit never grants execution */
}

LONG thread_startup_retain_stack(THREAD_STARTUP_DRAFT *draft, KASPACE_LEASE **hold)
{
    if (!draft || !hold || *hold) return EINVAL;
#if CONF_WITH_NATIVE_SCHED
    {
        LONG error;
        if (draft->state != THREAD_STARTUP_PREPARED && draft->state != THREAD_STARTUP_OBSERVED) return EINVAL;
        error = thread_startup_validate(draft);
        if (error) return error;
        return kaspace_stack_hold_acquire(draft->space, draft->args.stack_base, draft->args.stack_size, hold);
    }
#else
    return EINVFN;
#endif
}

#if CONF_WITH_NATIVE_SCHED
LONG thread_startup_run_preflight(THREAD_STARTUP_DRAFT *draft)
{
    UWORD i;
    LONG error;
    if (!draft || draft->state != THREAD_STARTUP_OBSERVED) return EINVAL;
    error = thread_startup_validate(draft);
    if (error) return error;
    draft->frame_alias = thread_startup_arch_frame_alias(kaspace_backend(draft->space), draft->image);
    if (!draft->frame_alias) return EIMBA;
    for (i = 0; i < 2; i++) if (draft->outputs[i]) {
        draft->output_aliases[i] = aspace_user_write_alias(kaspace_backend(draft->space), draft->outputs[i], 4);
        if (!draft->output_aliases[i]) return EIMBA;
    }
    return E_OK;
}
void thread_startup_commit_stores(THREAD_STARTUP_DRAFT *draft, WORD pid, ULONG tid)
{
    thread_startup_arch_commit_frame(draft->image, draft->frame_alias);
    if (draft->output_aliases[0]) *(volatile LONG *)draft->output_aliases[0] = pid;
    if (draft->output_aliases[1]) *(volatile ULONG *)draft->output_aliases[1] = tid;
    draft->state = THREAD_STARTUP_COMMITTED;
}
void thread_startup_publish(THREAD_STARTUP_DRAFT *draft)
{
    __asm__ volatile ("" ::: "memory");
    draft->state = THREAD_STARTUP_RUNNABLE;
}
void thread_startup_entering(THREAD_STARTUP_DRAFT *draft)
{
    UWORD i;
    for (i = 0; i < 5; i++) kaspace_lease_release(&draft->leases[i]);
}
BOOL thread_startup_take_stacks(THREAD_STARTUP_DRAFT *draft, THREAD_STARTUP_STACK_OWNERS *owners)
{
    return draft && draft->state == THREAD_STARTUP_SEALED &&
        thread_startup_arch_take_stacks(draft->image, owners);
}
#endif

#if CONF_WITH_NATIVE_SCHED
void thread_startup_reclaim(THREAD_STARTUP_DRAFT **handle)
{
    jmp_buf saved_error;
    if (!handle || !*handle) return;
    if ((*handle)->state != THREAD_STARTUP_RUNNABLE || !ksched_startup_reclaimable(*handle))
        panic("startup reclaim outside stopped scheduler root");
    memcpy(saved_error, errbuf, sizeof(errbuf));
    if (setjmp(errbuf)) {
        memcpy(errbuf, saved_error, sizeof(errbuf));
        panic("startup reclaim: cleanup callback escaped");
    }
    (*handle)->state = THREAD_STARTUP_RECLAIMABLE;
    thread_startup_abort(handle);
    memcpy(errbuf, saved_error, sizeof(errbuf));
}
#endif
