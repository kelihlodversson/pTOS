/* Build/cancel a user-entry image without granting execution or writing a
 * user stack. The existing scheduler/context switch remains unchanged. */
#include "emutos.h"
#include "gemerror.h"
#include "kheap.h"
#include "procmem.h"
#include "string.h"
#include "thread_startup_backend.h"
#include "thread_startup_x32.h"
#include "thread_return_x32.h"
#include "syscall_return.h"
#include <stddef.h>
typedef char entry_layout_check[(offsetof(X32_THREAD_ENTRY_IMAGE, fpu) == 224 &&
    offsetof(X32_THREAD_ENTRY_IMAGE, cr3) == 144 && offsetof(X32_THREAD_ENTRY_IMAGE, ss) == 160 &&
    offsetof(X32_THREAD_ENTRY_IMAGE, r15) == 112) ? 1 : -1];
#include "../../../bios/arch/x86_64/gdt.h"

#if CONF_WITH_X86_64_MEMTEST
static UWORD fail_stage;
void thread_startup_x32_test_fail_after(UWORD stage) { fail_stage = stage; }
static BOOL injected(UWORD stage)
{
    if (stage != fail_stage) return FALSE;
    fail_stage = 0; return TRUE;
}
#else
#define injected(stage) FALSE
#endif

LONG thread_startup_arch_validate(const ASPACE *space, const THREAD_STARTUP_ARGS *args)
{
    UQUAD end, rsp;
    if (!space || !args || !args->entry ||
        args->entry > 0xffffffffULL ||
        args->argument > 0xffffffffULL || (args->stack_base & 15) ||
        !args->stack_size || args->stack_base >= USER_VA_LIMIT ||
        args->stack_size > USER_VA_LIMIT - args->stack_base)
        return EINVAL;
    end = args->stack_base + args->stack_size;
    /* x32 SysV function entry: 8-byte return address, (RSP+8)%16 == 0,
     * and the entire 128-byte red zone inside the supplied writable region.
     * Alignment can discard up to 15 bytes. This is a frame floor, not an
     * application stack-size recommendation. */
    if (args->stack_size < 136 || (end & ~15ULL) < 8) return EINVAL;
    rsp = (end & ~15ULL) - 8;
    if (rsp < args->stack_base || rsp - args->stack_base < 128) return EINVAL;
    {
        UWORD prot;
        if (!aspace_translate(space, args->entry, NULL, &prot) ||
            (prot & (ASPACE_PROT_USER | ASPACE_PROT_EXEC)) != (ASPACE_PROT_USER | ASPACE_PROT_EXEC)) return EIMBA;
    }
    if (!aspace_user_range_ok(space, args->stack_base, args->stack_size, TRUE))
        return EIMBA;
    return E_OK;
}

void thread_startup_arch_abort(void *resource)
{
    X32_THREAD_STARTUP_DRAFT *draft = resource;
    if (!draft) return;
    kstack_free(draft->syscall_physical);
    kthread_stack_release(&draft->scheduler_stack);
    kfree(draft);
}

LONG thread_startup_arch_prepare(ASPACE *space, const THREAD_STARTUP_ARGS *args, const void *return_bridge, void **image)
{
    X32_THREAD_STARTUP_DRAFT *draft;
    LONG error;
    if (!image || *image) return EINVAL;
    if (!x32_return_bridge_matches(return_bridge, space)) return EIMBA;
    error = thread_startup_arch_validate(space, args);
    if (error) return error;
    draft = kalloc(sizeof(*draft));
    if (!draft) return ENSMEM;
    bzero(draft, sizeof(*draft));
    if (injected(1) || !kthread_stack_allocate(&draft->scheduler_stack)) goto failed;
    if (injected(2)) goto failed;
    draft->syscall_physical = kstack_alloc(&draft->syscall_top);
    if (!draft->syscall_physical || injected(3)) goto failed;
    draft->user.rip = args->entry;
    draft->user.rdi = args->argument; /* native width, zero-extended x32 value */
    draft->user.rsp = ((args->stack_base + args->stack_size) & ~15ULL) - 8;
    draft->user.rflags = 0x202; /* fixed bit + IF; no DF/TF/IOPL */
    draft->user.cr3 = aspace_root(space);
    draft->user.cs = X86_64_USER_CODE_SEL;
    draft->user.ss = draft->user.ds = draft->user.es = X86_64_USER_DATA_SEL;
    /* All GPRs except RDI and all FS/GS selectors/bases are zero. FXSAVE image:
     * default FCW, empty x87 tags, zero ST/XMM registers and default MXCSR.
     * MXCSR_MASK is the architectural fallback (FXRSTOR ignores that field). */
    draft->user.fpu[0] = 0x7f; draft->user.fpu[1] = 0x03;
    draft->user.fpu[24] = 0x80; draft->user.fpu[25] = 0x1f;
    draft->user.fpu[28] = 0xbf; draft->user.fpu[29] = 0xff;
    draft->return_pc = x32_return_bridge_address(return_bridge);
    *image = draft;
    return E_OK;
failed:
    thread_startup_arch_abort(draft);
    return ENSMEM;
}

BOOL thread_startup_x32_take_stacks(X32_THREAD_STARTUP_DRAFT *draft, X32_THREAD_STACK_OWNERS *owners)
{
    if (!draft || !owners || owners->syscall.physical || owners->syscall.top ||
        owners->scheduler.physical || owners->scheduler.top ||
        !draft->syscall_physical || !draft->scheduler_stack.physical) return FALSE;
    owners->syscall.physical = draft->syscall_physical;
    owners->syscall.top = draft->syscall_top;
    owners->scheduler.physical = draft->scheduler_stack.physical;
    owners->scheduler.top = draft->scheduler_stack.top;
    draft->scheduler_stack.physical = draft->scheduler_stack.top = 0;
    draft->syscall_physical = 0; draft->syscall_top = 0;
    return TRUE;
}
void thread_startup_x32_release_syscall(X32_THREAD_STACK_OWNERS *owners)
{
    if (owners) {
        KTHREAD_STACK stack = { owners->syscall.physical, owners->syscall.top };
        owners->syscall.physical = owners->syscall.top = 0;
        kthread_stack_release(&stack);
    }
}
void thread_startup_x32_release_scheduler(X32_THREAD_STACK_OWNERS *owners)
{
    if (owners) {
        KTHREAD_STACK stack = { owners->scheduler.physical, owners->scheduler.top };
        owners->scheduler.physical = owners->scheduler.top = 0;
        kthread_stack_release(&stack);
    }
}

void *thread_startup_arch_frame_alias(ASPACE *space, const void *image)
{
    const X32_THREAD_STARTUP_DRAFT *draft = image;
    return draft ? aspace_user_write_alias(space, draft->user.rsp, 8) : NULL;
}
void thread_startup_arch_commit_frame(const void *image, void *alias)
{
    const X32_THREAD_STARTUP_DRAFT *draft = image;
    *(volatile UQUAD *)alias = draft->return_pc;
}

BOOL thread_startup_arch_take_stacks(void *image, THREAD_STARTUP_STACK_OWNERS *owners)
{
    return thread_startup_x32_take_stacks(image, owners);
}

LONG thread_startup_arch_public_return(void *image, const void *bridge)
{
    X32_THREAD_STARTUP_DRAFT *draft = image;
    VADDR address = x32_return_bridge_zero_address(bridge);
    if (!draft || !address) return EIMBA;
    draft->return_pc = address; return E_OK;
}
#if CONF_WITH_FORK_RETURN_TEST
LONG thread_startup_arch_saved_return(void *image, const SYSCALL_RETURN *saved)
{
    X32_THREAD_STARTUP_DRAFT *draft = image;
    if (!draft || !saved || saved->user.cs != X86_64_USER_CODE_SEL ||
        saved->user.ss != X86_64_USER_DATA_SEL ||
        saved->user.rip != saved->mappings.entry ||
        saved->user.rsp < saved->mappings.stack_base + 128 ||
        saved->user.rsp >= saved->mappings.stack_base + saved->mappings.stack_size ||
        (saved->user.rflags & 0x27000)) return EINVFN;
    /* The integrated cooperative context has no general segment/TLS switch
     * contract. Capture all fields, but do not admit nondefault state until
     * its scheduler/launcher nesting restoration has native evidence. */
    if (saved->user.ds != X86_64_USER_DATA_SEL || saved->user.es != X86_64_USER_DATA_SEL ||
        saved->user.fs || saved->user.gs || saved->user.fs_base || saved->user.gs_base)
        return EINVFN;
    if (draft->syscall_physical == draft->scheduler_stack.physical ||
        kstack_contains(draft->syscall_physical, (uintptr_t)saved) ||
        kstack_contains(draft->scheduler_stack.physical, (uintptr_t)saved)) return EACCDN;
    draft->user = saved->user;
    draft->user.rax = 0;
    draft->saved_return = TRUE;
    return E_OK;
}
#endif
