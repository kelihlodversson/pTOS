/*
 * Process/trap-level typed callbacks. No user address is ever called at CPL0.
 * IRQ producers only queue work; delivery runs at a completed user boundary.
 */
#include "emutos.h"
#include "fs.h"
#include "user_callback.h"
#include "kproc.h"
#include "proc.h"
#include "gemerror.h"
#include "kheap.h"
#include "setjmp.h"
#include "string.h"
#include "tosvars.h"
#include "../bios/arch/x86_64/trap.h"
#include "../bios/arch/x86_64/gdt.h"
#if CONF_WITH_X86_64_MEMTEST
#include "../bios/machine/pc-x86_64/pc_x86_64_memtest.h"
#endif

/* Registrations have independent owner/channel/capability/invocation refs.
 * Revoked records may outlive the owner, but contain no executable address. */
struct user_callback {
    PD *owner, *lifetime_owner;
    ULONG handler, refs;
    UWORD signature;
    USER_CALLBACK *next;
};

struct user_callback_state {
    USER_CALLBACK *owned;
    struct {
        USER_CALLBACK *target;
        UWORD signature;
        USER_CALLBACK_NATIVE native;
    } cap[USER_CALLBACK_CAPS];
    ULONG ncaps, nowned;
    USER_CALLBACK_STACK stack;
    BOOL fatal;
};

typedef struct callback_frame {
    struct callback_frame *previous;
    USER_CALLBACK *registration;
    PD *owner, *caller;
    UQUAD kernel_rsp, user_rsp, irq_rsp, root, stack_phys;
    UQUAD flags, resume_rsp;
    UQUAD stack_bottom, stack_top;
    int gs_swapped;
    ULONG token, result, deadline;
    BOOL alternate, failed;
    jmp_buf resume;
    UBYTE fpu[512] __attribute__((aligned(16)));
} CALLBACK_FRAME;

static CALLBACK_FRAME *active;
static ULONG next_token;
static UWORD depth;
static void (*work_hooks[4])(void);
extern PD *run;

static const UBYTE initial_fpu[512] __attribute__((aligned(16))) = {
    0x7f, 0x03, [24] = 0x80, [25] = 0x1f
};
extern void x86_64_callback_enter(UQUAD entry, UQUAD rsp, LONG a, LONG b,
                                  const void *fpu) NORETURN;

static UQUAD interrupt_off(void)
{
    UQUAD flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static void interrupt_restore(UQUAD flags)
{
    if (flags & 0x200)
        __asm__ volatile ("sti" ::: "memory");
}

static void release(USER_CALLBACK *cb)
{
    if (cb && !--cb->refs)
        kfree(cb);
}

BOOL user_callback_owner(USER_CALLBACK *cb, PD *pd)
{
    return cb && cb->lifetime_owner == pd;
}

void user_callback_drop(USER_CALLBACK **slot)
{
    USER_CALLBACK *old;
    UQUAD flags = interrupt_off();

    old = *slot;
    *slot = NULL;
    interrupt_restore(flags);
    release(old);
}

static USER_CALLBACK_STATE *state_get(void)
{
    USER_CALLBACK_STATE *state = kproc_callbacks_get(run);

    if (!state) {
        state = kalloc(sizeof(*state));
        if (state && !kproc_callbacks_set(run, state)) {
            kfree(state);
            return NULL;
        }
        if (state)
            state->stack.ss_flags = USER_CALLBACK_SS_DISABLE;
    }
    return state;
}

static BOOL executable(PD *owner, UQUAD handler)
{
    ASPACE *as = kproc_user_aspace(owner);
    UWORD prot;

    return handler && handler <= 0xffffffffULL && as &&
        aspace_translate(as, handler, NULL, &prot) &&
        (prot & (ASPACE_PROT_USER | ASPACE_PROT_EXEC)) ==
                (ASPACE_PROT_USER | ASPACE_PROT_EXEC);
}

static LONG capability(USER_CALLBACK_STATE *state, USER_CALLBACK *target,
                       UWORD signature, const USER_CALLBACK_NATIVE *native,
                       ULONG *address)
{
    ULONG i;

    for (i = 0; i < state->ncaps; i++)
        if (state->cap[i].target == target && state->cap[i].signature == signature &&
            (target || !memcmp(&state->cap[i].native, native, sizeof(*native))))
            break;
    if (i == USER_CALLBACK_CAPS)
        return ENSMEM;
    if (i == state->ncaps) {
        state->cap[i].target = target;
        state->cap[i].signature = signature;
        state->cap[i].native = *native;
        if (target)
            target->refs++;
        state->ncaps++;
    }
    *address = USER_CALLBACK_IMAGE + USER_CALLBACK_STUBS + i * USER_CALLBACK_STUB_SIZE;
    return E_OK;
}

LONG user_callback_exchange(USER_CALLBACK **slot, UWORD signature,
                             UQUAD handler, const USER_CALLBACK_NATIVE *fallback,
                             ULONG *old)
{
    USER_CALLBACK_STATE *state;
    USER_CALLBACK *target = NULL, *before = *slot;
    UQUAD flags;
    ULONG i;
    LONG rc;

    if (signature >= USER_CALLBACK_SIGNATURES || !fallback || !fallback->i32_void)
        return EINVAL;
    state = state_get();
    if (!state)
        return ENSMEM;
    if (handler >= USER_CALLBACK_IMAGE && handler < USER_CALLBACK_IMAGE + 4096) {
        /* A chaining stub is a caller-local capability, never a raw handler. */
        if (handler < USER_CALLBACK_IMAGE + USER_CALLBACK_STUBS ||
            (handler - USER_CALLBACK_IMAGE - USER_CALLBACK_STUBS) % USER_CALLBACK_STUB_SIZE)
            return EIMBA;
        i = (ULONG)(handler - USER_CALLBACK_IMAGE - USER_CALLBACK_STUBS) / USER_CALLBACK_STUB_SIZE;
        if (i >= state->ncaps || state->cap[i].signature != signature)
            return EIMBA;
        target = state->cap[i].target;
        if (target && !target->owner)
            target = NULL;
    } else if (handler) {
        if (!executable(run, handler))
            return EIMBA;
        /* Deduplicate exact owner/signature pairs, preserving OLD semantics. */
        for (target = state->owned; target; target = target->next)
            if (target->handler == handler && target->signature == signature)
                break;
        if (!target) {
            if (state->nowned == USER_CALLBACK_CAPS)
                return ENSMEM;
            target = kalloc(sizeof(*target));
            if (!target)
                return ENSMEM;
            target->owner = run;
            target->lifetime_owner = run;
            target->handler = (ULONG)handler;
            target->signature = signature;
            target->refs = 1;
            target->next = state->owned;
            state->owned = target;
            state->nowned++;
        }
    }
    if (before && before->owner == run && before->signature == signature &&
        executable(run, before->handler)) {
        *old = before->handler;
    } else {
        rc = capability(state, before, signature, fallback, old);
        if (rc != E_OK)
            return rc;
    }
    if (target)
        target->refs++;
    flags = interrupt_off();
    *slot = target;
    interrupt_restore(flags);
    release(before);
    return E_OK;
}

/* Immutable user instructions: RETURN consumes a token beneath the return
 * address; each CHAIN stub embeds its own never-reused capability index. */
BOOL user_callback_image_load(ASPACE *as)
{
    UBYTE *image = kalloc(4096);
    ULONG i, offset;
    UQUAD call;
    BOOL ok;
    static const UBYTE chain[] = {
        0x89, 0xf2, 0x89, 0xfe, 0xbf, 0, 0, 0, 0,
        0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0x0f, 0x05, 0xc3
    };
    static const UBYTE finish[] = {
        0x89, 0xc6, 0x5f, 0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0x0f, 0x05, 0x0f, 0x0b
    };

    if (!image)
        return FALSE;
    memset(image, 0xcc, 4096);
    memcpy(image, finish, sizeof(finish));
    call = (UQUAD)USER_CALLBACK_CLASS << 32 | USER_CALLBACK_RETURN;
    memcpy(image + 5, &call, sizeof(call));
    for (i = 0; i < USER_CALLBACK_CAPS; i++) {
        offset = USER_CALLBACK_STUBS + i * USER_CALLBACK_STUB_SIZE;
        memcpy(image + offset, chain, sizeof(chain));
        memcpy(image + offset + 5, &i, sizeof(i));
        call = (UQUAD)USER_CALLBACK_CLASS << 32 | USER_CALLBACK_CHAIN;
        memcpy(image + offset + 11, &call, sizeof(call));
    }
    ok = aspace_load_private(as, USER_CALLBACK_IMAGE, 4096,
                             ASPACE_PROT_USER | ASPACE_PROT_EXEC, image, 4096);
    kfree(image);
    return ok;
}

static BOOL on_altstack(PD *pd)
{
    CALLBACK_FRAME *frame;

    for (frame = active; frame; frame = frame->previous)
        if (frame->owner == pd && frame->alternate)
            return TRUE;
    return FALSE;
}

static BOOL owner_active(PD *pd)
{
    CALLBACK_FRAME *frame;

    for (frame = active; frame; frame = frame->previous)
        if (frame->owner == pd)
            return TRUE;
    return FALSE;
}

static BOOL overlaps(UQUAD a, UQUAD size, UQUAD b, UQUAD bytes)
{
    return size && bytes && a < b + bytes && b < a + size;
}

BOOL user_callback_pinned(PD *pd, UQUAD address, UQUAD size)
{
    USER_CALLBACK_STATE *state = kproc_callbacks_get(pd);

    return state && !(state->stack.ss_flags & USER_CALLBACK_SS_DISABLE) &&
        overlaps(address, size, state->stack.ss_sp, state->stack.ss_size);
}

static LONG altstack(UQUAD input, UQUAD output)
{
    USER_CALLBACK_STATE *state = state_get();
    USER_CALLBACK_STACK next, old;
    UQUAD bottom = kproc_stack_bottom(run);

    if (!state)
        return ENSMEM;
    if (output && !kproc_validate_user_write(output, sizeof(old)))
        return EIMBA;
    old = state->stack;
    if (on_altstack(run))
        old.ss_flags |= USER_CALLBACK_SS_ONSTACK;
    if (input) {
        if (owner_active(run))
            return EACCDN;
        if (!kproc_copy_from_user(&next, input, sizeof(next)))
            return EIMBA;
        if (next.ss_flags & ~USER_CALLBACK_SS_DISABLE)
            return EINVAL;
        if (next.ss_flags & USER_CALLBACK_SS_DISABLE) {
            next.ss_sp = next.ss_size = 0;
        } else {
            if ((next.ss_sp & 15) || (next.ss_size & 15) ||
                next.ss_size < USER_CALLBACK_MIN_STACK || next.ss_size > 1024UL * 1024UL ||
                (UQUAD)next.ss_sp + next.ss_size > USER_VA_LIMIT ||
                overlaps(next.ss_sp, next.ss_size, bottom, USER_STACK_TOP - bottom))
                return EINVAL;
            if (!kproc_callback_stack_stable(run, next.ss_sp, next.ss_size))
                return EACCDN;
            if (!kproc_validate_user_write(next.ss_sp, next.ss_size))
                return EIMBA;
        }
        state->stack = next;
    }
    return !output || kproc_copy_to_user(output, &old, sizeof(old)) ? E_OK : EIMBA;
}

static void revoke(USER_CALLBACK_STATE *state)
{
    USER_CALLBACK *cb;

    state->fatal = TRUE;
    for (cb = state->owned; cb; cb = cb->next) {
        cb->owner = NULL;
        cb->handler = 0;
    }
}

BOOL user_callback_fault(void)
{
    USER_CALLBACK_STATE *state;

    if (!active || active->owner != run)
        return FALSE;
    state = kproc_callbacks_get(run);
    if (state)
        revoke(state);
    active->failed = TRUE;
    longjmp(active->resume, 1);
}

static void check_fatal(void)
{
    USER_CALLBACK_STATE *state = kproc_callbacks_get(run);

    if (state && state->fatal) {
        user_callback_fault();
        xterm((UWORD)-1);
    }
}

LONG user_callback_invoke(USER_CALLBACK *cb, UWORD signature,
                           LONG a, LONG b, ULONG *result)
{
    USER_CALLBACK_STATE *state;
    CALLBACK_FRAME *frame;
    UQUAD sp, top, bottom, stack_top, words[2];
    BOOL failed;

    *result = signature == USER_CALLBACK_I16_I16_U32 ? MAKE_ULONG(a, b) : 0;
    if (!cb || !cb->owner)
        return E_OK;
    if (cb->signature != signature || !executable(cb->owner, cb->handler))
        return EIMBA;
    state = kproc_callbacks_get(cb->owner);
    if (!state || state->fatal)
        return EACCDN;
    if (depth == USER_CALLBACK_DEPTH) {
        /* Abort the requesting callback, never an unrelated registered owner. */
        user_callback_fault();
        return EACCDN;
    }
    frame = kalloc(sizeof(*frame));
    if (!frame)
        return ENSMEM;
    frame->stack_phys = kstack_alloc(&stack_top);
    if (!frame->stack_phys) {
        kfree(frame);
        return ENSMEM;
    }
    frame->registration = cb;
    cb->refs++;
    frame->owner = cb->owner;
    frame->caller = run;
    frame->resume_rsp = kproc_resume_rsp(frame->owner);
    frame->kernel_rsp = x86_64_get_kernel_stack();
    frame->user_rsp = x86_64_get_saved_user_rsp();
    frame->irq_rsp = x86_64_get_irq_stack();
    frame->previous = active;
    frame->deadline = (ULONG)hz_200 + 100; /* 500ms, also includes nested calls */
    if (active && (LONG)(frame->deadline - active->deadline) > 0)
        frame->deadline = active->deadline;
    frame->token = ++next_token;
    if (!frame->token)
        frame->token = ++next_token;
    frame->flags = interrupt_off();
    __asm__ volatile ("fxsave %0" : "=m"(frame->fpu));
    run = frame->owner;
    frame->root = aspace_enter(kproc_user_aspace(run));
    top = kproc_resume_rsp(run);
    bottom = kproc_stack_bottom(run);
    if (!(state->stack.ss_flags & USER_CALLBACK_SS_DISABLE)) {
        bottom = state->stack.ss_sp;
        if (!on_altstack(run))
            top = bottom + state->stack.ss_size;
        frame->alternate = TRUE;
    }
    /* Preserve the interrupted x32 ABI's 128-byte red zone, and keep a full
     * page for the handler before its first instruction can run. */
    sp = top >= 152 ? ((top - 144) & ~15ULL) - 8 : 0;
    frame->stack_bottom = bottom;
    frame->stack_top = frame->alternate ?
        (UQUAD)state->stack.ss_sp + state->stack.ss_size : USER_STACK_TOP;
    frame->failed = sp < bottom + 4096 ||
        top > (frame->alternate ? (UQUAD)state->stack.ss_sp + state->stack.ss_size : USER_STACK_TOP);
    frame->gs_swapped = x86_64_gs_to_user();
    active = frame;
    depth++;
    if (!setjmp(frame->resume)) {
        if (frame->failed)
            user_callback_fault();
#if CONF_WITH_X86_64_MEMTEST
        x86_64_memtest_callback_entry();
#endif
        words[0] = USER_CALLBACK_IMAGE;
        words[1] = frame->token;
        if (!kproc_copy_to_user(sp, words, sizeof(words)))
            user_callback_fault();
        x86_64_set_kernel_stack(stack_top);
        /* IDT and SYSCALL use separate stack tops within this allocation.
         * A user IRQ may suspend a kernel caller whose stack is still live. */
        x86_64_set_irq_stack(stack_top + 8);
        x86_64_callback_enter(cb->handler, sp, a, b, initial_fpu);
    }
    interrupt_off();
    x86_64_syscall_abandoned();
    active = frame->previous;
    depth--;
    run = frame->caller;
    aspace_leave(frame->root);
    x86_64_set_kernel_stack(frame->kernel_rsp);
    x86_64_set_saved_user_rsp(frame->user_rsp);
    x86_64_set_irq_stack(frame->irq_rsp);
    kproc_record_rsp(frame->owner, frame->resume_rsp);
    if (frame->gs_swapped)
        x86_64_gs_back_to_syscall();
    __asm__ volatile ("fxrstor %0" :: "m"(frame->fpu));
    *result = signature == USER_CALLBACK_I16_I16_U32 ? frame->result : 0;
    failed = frame->failed;
    release(frame->registration);
    kstack_free(frame->stack_phys);
    interrupt_restore(frame->flags);
    kfree(frame);
    /* A suspended foreign owner is quarantined now and terminated at its
     * next user boundary; unwinding its launch here would kill the caller. */
    check_fatal();
    return failed ? EACCDN : E_OK;
}

LONG user_callback_service(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD pc)
{
    USER_CALLBACK_STATE *state = kproc_callbacks_get(run);
    ULONG result;
    LONG rc;

    if (fn == USER_CALLBACK_RETURN) {
        if (!active || active->owner != run || a != active->token ||
            pc != USER_CALLBACK_IMAGE + 15)
            return EACCDN;
        active->result = (ULONG)b;
        longjmp(active->resume, 1);
    }
    if (fn == USER_CALLBACK_CHAIN) {
        if (!state || a >= state->ncaps ||
            pc != USER_CALLBACK_IMAGE + USER_CALLBACK_STUBS +
                  a * USER_CALLBACK_STUB_SIZE + 21)
            return EACCDN;
        if (!state->cap[a].target) {
            USER_CALLBACK_NATIVE native = state->cap[a].native;

            switch (state->cap[a].signature) {
            case USER_CALLBACK_I32_VOID: native.i32_void((int)b); break;
            case USER_CALLBACK_I16_VOID: native.i16_void((WORD)b); break;
            case USER_CALLBACK_I16_I16_VOID: native.i16_i16_void((WORD)b, (WORD)c); break;
            case USER_CALLBACK_I16_I16_U32: return native.i16_i16_u32((WORD)b, (WORD)c);
            }
            return E_OK;
        }
        rc = user_callback_invoke(state->cap[a].target, state->cap[a].signature,
                                  (LONG)b, (LONG)c, &result);
        return rc == E_OK ? (LONG)result : rc;
    }
    if (fn == USER_CALLBACK_ALTSTACK)
        return altstack(a, b);
    return EINVFN;
}

/* Callback execution must not enter a blocking kernel call: the watchdog
 * regains control at user IRQ/syscall boundaries, never by unwinding a live
 * filesystem operation or allocator. Memory/identity and typed services are
 * bounded; application I/O and process control resume after the callback. */
BOOL user_callback_syscall_allowed(UQUAD call)
{
    ULONG fn = (ULONG)call;
    UQUAD service = call >> 32;

    check_fatal();
    if (!active)
        return TRUE;
    if (kproc_resume_rsp(run) < active->stack_bottom ||
        kproc_resume_rsp(run) > active->stack_top)
        user_callback_fault();
    if ((LONG)((ULONG)hz_200 - active->deadline) >= 0)
        user_callback_fault();
    if (service == USER_CALLBACK_CLASS || service == 0x100 || service == 0x101)
        return TRUE;
    if (service != 1)
        return FALSE;
    return fn == 0x44 || fn == 0x48 || fn == 0x49 || fn == 0x4a ||
           fn == 0x10b || fn == 0x10c || fn == 0x10f || fn == 0x114 ||
           fn == 0x138 || fn == 0x139 || fn == 0x17d;
}

BOOL user_callback_work_register(void (*work)(void))
{
    UWORD i;

    for (i = 0; i < ARRAY_SIZE(work_hooks); i++) {
        if (work_hooks[i] == work)
            return TRUE;
        if (!work_hooks[i]) {
            work_hooks[i] = work;
            return TRUE;
        }
    }
    return FALSE;
}

void user_callback_return_work(UQUAD rsp)
{
    kproc_record_rsp(run, rsp);
    if (active && (rsp < active->stack_bottom || rsp > active->stack_top))
        user_callback_fault();
    if (active && (LONG)((ULONG)hz_200 - active->deadline) >= 0)
        user_callback_fault();
    check_fatal();
    if (!active) {
        UWORD i;

        for (i = 0; i < ARRAY_SIZE(work_hooks); i++)
            if (work_hooks[i])
                work_hooks[i]();
    }
}

void user_callback_process_exit(PD *pd)
{
    USER_CALLBACK_STATE *state = kproc_callbacks_get(pd);
    USER_CALLBACK *cb, *next;
    ULONG i;

    if (!state)
        return;
    revoke(state);
    for (i = 0; i < state->ncaps; i++)
        release(state->cap[i].target);
    for (cb = state->owned; cb; cb = next) {
        next = cb->next;
        release(cb);
    }
    kproc_callbacks_set(pd, NULL);
    kfree(state);
}
