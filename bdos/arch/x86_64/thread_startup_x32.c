/* Build/cancel a user-entry image without granting execution or writing a
 * user stack. The existing scheduler/context switch remains unchanged. */
#include "emutos.h"
#include "gemerror.h"
#include "kheap.h"
#include "procmem.h"
#include "string.h"
#include "thread_startup_backend.h"
#include "thread_startup_x32.h"
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

static BOOL executable(const ASPACE *space, VADDR address)
{
    UWORD protection;
    return aspace_translate(space, address, NULL, &protection) &&
        (protection & (ASPACE_PROT_USER | ASPACE_PROT_EXEC)) ==
        (ASPACE_PROT_USER | ASPACE_PROT_EXEC);
}

LONG thread_startup_arch_validate(const ASPACE *space, const THREAD_STARTUP_ARGS *args)
{
    UQUAD end, rsp;
    if (!space || !args || !args->entry || !args->return_entry ||
        args->entry > 0xffffffffULL || args->return_entry > 0xffffffffULL ||
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
    if (!executable(space, args->entry) || !executable(space, args->return_entry) ||
        !aspace_user_range_ok(space, args->stack_base, args->stack_size, TRUE))
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

LONG thread_startup_arch_prepare(ASPACE *space, const THREAD_STARTUP_ARGS *args, void **image)
{
    X32_THREAD_STARTUP_DRAFT *draft;
    LONG error;
    if (!image || *image) return EINVAL;
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
    draft->return_pc = args->return_entry;
    *image = draft;
    return E_OK;
failed:
    thread_startup_arch_abort(draft);
    return ENSMEM;
}
