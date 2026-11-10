/* Shared ARM/m68k startup allocation, validation and ownership transaction. */
#include "emutos.h"
#include "gemerror.h"
#include "kheap.h"
#include "procmem.h"
#include "string.h"
#include "thread_startup_backend.h"
#include "thread_startup32.h"
#include "thread_return.h"
#include "syscall_return.h"

LONG thread_startup_arch_validate(const ASPACE *space, const THREAD_STARTUP_ARGS *args)
{
    UWORD prot;
    if (!space || !args || !args->entry ||
        args->entry % CONF_PCLONE_ENTRY_ALIGNMENT ||
        args->stack_base % CONF_PCLONE_STACK_ALIGNMENT ||
        args->stack_size < CONF_PCLONE_MIN_STACK ||
        args->stack_base >= USER_VA_LIMIT ||
        args->stack_size > USER_VA_LIMIT - args->stack_base)
        return EINVAL;
    if (!aspace_translate(space, args->entry, NULL, &prot) ||
        (prot & (ASPACE_PROT_USER | ASPACE_PROT_EXEC)) != (ASPACE_PROT_USER | ASPACE_PROT_EXEC) ||
        !aspace_user_range_ok(space, args->stack_base, args->stack_size, TRUE)) return EIMBA;
    return E_OK;
}
void thread_startup_arch_abort(void *image)
{
    THREAD_STARTUP32 *draft = image;
    if (!draft) return;
#if CONF_WITH_FORK_RETURN_TEST
    kfree(draft->saved_return);
#endif
    if (draft->syscall.physical) kstack_free(draft->syscall.physical);
    kthread_stack_release(&draft->scheduler_stack);
    kfree(draft);
}
LONG thread_startup_arch_prepare(ASPACE *space, const THREAD_STARTUP_ARGS *args,
                                const void *bridge, void **image)
{
    THREAD_STARTUP32 *draft;
    LONG error;
    if (!image || *image) return EINVAL;
    if (!thread_return_bridge_matches(bridge, space)) return EIMBA;
    error = thread_startup_arch_validate(space, args);
    if (error) return error;
    draft = kalloc(sizeof(*draft));
    if (!draft) return ENSMEM;
    bzero(draft, sizeof(*draft));
    if (!kthread_stack_allocate(&draft->scheduler_stack)) goto failed;
    draft->syscall.physical = kstack_alloc(&draft->syscall.top);
    if (!draft->syscall.physical) goto failed;
    draft->entry = args->entry;
    draft->argument = args->argument;
    draft->return_pc = thread_return_bridge_address(bridge);
#if ARCH_ARM
    /* AAPCS32: aligned SP, argument r0, return PC in LR; reserve one word
     * below the aligned top for the protected, precommitted frame alias. */
    draft->sp = (args->stack_base + args->stack_size) & ~7UL;
#else
    /* Native C ABI: return PC followed by a 32-bit argument. */
    draft->sp = ((args->stack_base + args->stack_size) & ~1UL) - 8;
#endif
    *image = draft;
    return E_OK;
failed:
    thread_startup_arch_abort(draft);
    return ENSMEM;
}
void *thread_startup_arch_frame_alias(ASPACE *space, const void *image)
{
    THREAD_STARTUP32 *draft = (THREAD_STARTUP32 *)image;
    UWORD i;
    VADDR address;
    if (!draft) return NULL;
#if ARCH_ARM
    address = draft->sp - 4;
#else
    address = draft->sp;
#endif
    /* m68k permits a two-byte-aligned frame across discontiguous pages. */
    for (i = 0; i <
#if ARCH_ARM
        2
#else
        4
#endif
        ; i++) {
        draft->frame_aliases[i] = aspace_user_write_alias(space, address + 2*i, 2);
        if (!draft->frame_aliases[i]) return NULL;
    }
    return draft;
}
void thread_startup_arch_commit_frame(const void *image, void *alias)
{
    const THREAD_STARTUP32 *draft = image;
    ULONG frame[2];
    UWORD words[4], i;
    UNUSED(alias);
    frame[0] = draft->return_pc; frame[1] = draft->argument;
    memcpy(words, frame, sizeof(words));
    for (i = 0; i <
#if ARCH_ARM
        2
#else
        4
#endif
        ; i++) *draft->frame_aliases[i] = words[i];
}
BOOL thread_startup_arch_take_stacks(void *image, THREAD_STARTUP_STACK_OWNERS *owners)
{
    THREAD_STARTUP32 *draft = image;
    if (!draft || !owners || owners->syscall.physical || owners->syscall.top ||
        owners->scheduler.physical || owners->scheduler.top ||
        !draft->syscall.physical || !draft->scheduler_stack.physical) return FALSE;
    owners->syscall = draft->syscall;
    owners->scheduler.physical = draft->scheduler_stack.physical;
    owners->scheduler.top = draft->scheduler_stack.top;
    bzero(&draft->syscall, sizeof(draft->syscall));
    bzero(&draft->scheduler_stack, sizeof(draft->scheduler_stack));
    return TRUE;
}
LONG thread_startup_arch_public_return(void *image, const void *bridge)
{
    THREAD_STARTUP32 *draft = image;
    VADDR address = thread_return_bridge_zero_address(bridge);
    if (!draft || !address) return EIMBA;
    draft->return_pc = address;
    return E_OK;
}
#if CONF_WITH_FORK_RETURN_TEST
LONG thread_startup_arch_saved_return(void *image, const SYSCALL_RETURN *saved)
{
    THREAD_STARTUP32 *draft = image;
    SYSCALL_USER_IMAGE *user;
    if (!draft || !saved || draft->saved_return) return EINVAL;
    if (draft->syscall.physical == draft->scheduler_stack.physical ||
        kstack_contains(draft->syscall.physical, (uintptr_t)saved) ||
        kstack_contains(draft->scheduler_stack.physical, (uintptr_t)saved)) return EACCDN;
#if ARCH_ARM
    if ((saved->user.status & 0x3f) != 0x10 || saved->user.pc != saved->mappings.entry)
        return EINVFN; /* A32 user return only, no Thumb/pending exception restart */
#else
    if ((saved->user.frame.sr & 0x2000) || (saved->user.frame.format & 0xf000)) return EINVFN;
#endif
    user = kalloc(sizeof(*user));
    if (!user) return ENSMEM;
    *user = saved->user;
#if ARCH_ARM
    user->r[0] = 0;
    draft->sp = user->usp;
#else
    user->frame.d[0] = 0;
    draft->sp = user->frame.usp;
#endif
    draft->saved_return = user;
    return E_OK;
}
#endif
