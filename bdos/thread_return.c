/* Kernel-selected THREAD return bridge, never user code input. */
#include "emutos.h"
#include "biosext.h"
#include "gemerror.h"
#include "kheap.h"
#include "procmem.h"
#include "kaspace.h"
#include "kproc.h"
#include "kthread.h"
#include "string.h"
#include "thread_return.h"
#include "thread_startup_backend.h"

/* x32 SysV function result in EAX; GEMDOS takes UWORD status in RDI.
 * mov edi,eax; movabs rax,(GEMDOS_CLASS<<32)|Pterm; syscall; ud2.
 * No C stack accesses, dynamic relocations, user-selected PC or new syscall.
 * UD2 contains an unexpected return from the nonreturning termination call. */
#if ARCH_X86_64
static const UBYTE return_code[] = {
    0x89, 0xc7, 0x48, 0xb8, 0x4c, 0, 0, 0, 1, 0, 0, 0,
    0x0f, 0x05, 0x0f, 0x0b,
    /* Public void entry: xor edi,edi; same Pterm/launcher path. */
    0x31, 0xff, 0x48, 0xb8, 0x4c, 0, 0, 0, 1, 0, 0, 0,
    0x0f, 0x05, 0x0f, 0x0b
};
#elif ARCH_ARM
/* AAPCS: LR returns here; r0 is the result, GEMDOS uses r0/r1. */
static const UBYTE return_code[] = {
    0x00,0x10,0xa0,0xe1, 0x4c,0x00,0xa0,0xe3,
    0x01,0x00,0x00,0xef, 0xf0,0x00,0xf0,0xe7,
    0x00,0x10,0xa0,0xe3, 0x4c,0x00,0xa0,0xe3,
    0x01,0x00,0x00,0xef, 0xf0,0x00,0xf0,0xe7
};
#else
/* m68k C ABI: RTS pops the trusted bridge; GEMDOS arguments are packed. */
static const UBYTE return_code[] = {
    0x3f,0x00, 0x3f,0x3c,0x00,0x4c, 0x4e,0x41, 0x4a,0xfc,
    0x4a,0xfc, 0x4a,0xfc, 0x4a,0xfc,
    0x42,0x67, 0x3f,0x3c,0x00,0x4c, 0x4e,0x41, 0x4a,0xfc,
    0x4a,0xfc, 0x4a,0xfc, 0x4a,0xfc
};
#endif
/* 32-bit stacks end at USER_VA_LIMIT. Their bridge has a reserved image-
 * metadata page outside heap, interpreter, ancestor and growing stack ranges. */
#if ARCH_X86_64
#define RETURN_VA (USER_VA_LIMIT - USER_PAGE_SIZE)
#else
#define RETURN_VA USER_THREAD_RETURN_VA
#endif
struct thread_return_bridge {
    KASPACE *space; /* owning; registry membership is borrowed */
    KASPACE_LEASE *hold;
    ULONG users;
    struct thread_return_bridge *next;
};
static THREAD_RETURN_BRIDGE *bridges;

BOOL thread_return_bridge_valid(const THREAD_RETURN_BRIDGE *bridge)
{
    UBYTE code[sizeof(return_code)];
    UWORD prot;
    ASPACE *as;
    if (!bridge || !bridge->users || !kaspace_lease_valid(bridge->hold)) return FALSE;
    as = kaspace_backend(bridge->space);
    return aspace_translate(as, RETURN_VA, NULL, &prot) &&
        prot == (ASPACE_PROT_USER | ASPACE_PROT_EXEC) &&
        aspace_copy_from_user(as, code, RETURN_VA, sizeof(code)) &&
        !memcmp(code, return_code, sizeof(code));
}

BOOL thread_return_bridge_matches(const THREAD_RETURN_BRIDGE *bridge, const ASPACE *space)
{
    return bridge && kaspace_backend(bridge->space) == space && thread_return_bridge_valid(bridge);
}

LONG thread_return_bridge_acquire(KASPACE *space, THREAD_RETURN_BRIDGE **result)
{
    THREAD_RETURN_BRIDGE *bridge;
    ASPACE *as;
    LONG error;
    if (!space || !result || *result) return EINVAL;
    for (bridge = bridges; bridge; bridge = bridge->next) if (bridge->space == space) {
        if (!thread_return_bridge_valid(bridge)) return EIMBA;
        if (bridge->users == ~(ULONG)0) return ENSMEM;
        bridge->users++; *result = bridge; return E_OK;
    }
    as = kaspace_backend(space);
    /* A coincidentally identical existing page is NOT a trusted identity. */
    if (aspace_translate(as, RETURN_VA, NULL, NULL)) return EACCDN;
    bridge = kalloc(sizeof(*bridge));
    if (!bridge) return ENSMEM;
    bridge->space = kaspace_share(space);
    if (!bridge->space) { kfree(bridge); return ENSMEM; }
    bridge->hold = NULL; bridge->users = 1;
    if (!aspace_load_private(as, RETURN_VA, USER_PAGE_SIZE,
            ASPACE_PROT_USER | ASPACE_PROT_EXEC, return_code, sizeof(return_code))) {
        error = ENSMEM; goto failed;
    }
    error = kaspace_lease_acquire(space, RETURN_VA, USER_PAGE_SIZE,
            ASPACE_PROT_USER | ASPACE_PROT_EXEC, &bridge->hold);
    if (error || !kaspace_lease_hold_private(bridge->hold)) {
        if (!error) error = EIMBA;
        kaspace_lease_release(&bridge->hold);
        if (!aspace_unmap_private(as, RETURN_VA, USER_PAGE_SIZE)) panic("return bridge rollback");
        goto failed;
    }
    bridge->next = bridges; bridges = bridge;
    *result = bridge;
    return E_OK;
failed:
    kaspace_put(bridge->space); kfree(bridge); return error;
}

VADDR thread_return_bridge_address(const THREAD_RETURN_BRIDGE *bridge)
{
    return thread_return_bridge_valid(bridge) ? RETURN_VA : 0;
}
#if CONF_WITH_PRIVATE_CLONE
LONG thread_startup_arch_bridge_acquire_cloned(KASPACE *source, KASPACE *space, void **out)
{
    THREAD_RETURN_BRIDGE *original, *bridge;
    ASPACE *as = kaspace_backend(space);
    PADDR before, copied;
    UWORD prot;
    LONG error;
    if (!out || *out || !source || !space || source == space) return EINVAL;
    for (original = bridges; original && original->space != source; original = original->next) { }
    if (!original) return thread_startup_arch_bridge_acquire(space, out);
    if (!thread_return_bridge_valid(original) ||
        !aspace_translate(kaspace_backend(source), RETURN_VA, &before, NULL) ||
        !aspace_translate(as, RETURN_VA, &copied, &prot) || before != copied ||
        prot != (ASPACE_PROT_USER | ASPACE_PROT_EXEC) ||
        !aspace_private_range_ok(as, RETURN_VA, USER_PAGE_SIZE)) return EACCDN;
    bridge = kalloc(sizeof(*bridge));
    if (!bridge) return ENSMEM;
    bridge->space = kaspace_share(space);
    if (!bridge->space) { kfree(bridge); return ENSMEM; }
    bridge->users = 1;
    error = kaspace_lease_acquire(space, RETURN_VA, USER_PAGE_SIZE,
                                ASPACE_PROT_USER | ASPACE_PROT_EXEC, &bridge->hold);
    if (!error && !kaspace_lease_hold_private(bridge->hold)) error = EIMBA;
    if (error) {
        kaspace_lease_release(&bridge->hold); kaspace_put(bridge->space); kfree(bridge);
        return error;
    }
    bridge->next = bridges; bridges = bridge;
    *out = bridge;
    return E_OK;
}
#endif

VADDR thread_return_bridge_zero_address(const THREAD_RETURN_BRIDGE *bridge)
{
    return thread_return_bridge_valid(bridge) ? RETURN_VA + 16 : 0;
}

BOOL thread_return_bridge_process_eligible(KPROC *process, KTHREAD *thread)
{
    /* Legacy sole-identity model preflight. Runnable managed siblings instead
     * use trusted startup plus the thread-only launcher exit contract. */
    return kproc_thread_is_sole(process, thread);
}

void thread_return_bridge_release(THREAD_RETURN_BRIDGE **result)
{
    THREAD_RETURN_BRIDGE *bridge, **link;
    if (!result || !*result) return;
    bridge = *result; *result = NULL;
    if (--bridge->users) return;
    for (link = &bridges; *link != bridge; link = &(*link)->next) { }
    *link = bridge->next;
    kaspace_lease_release(&bridge->hold);
    if (!aspace_unmap_private(kaspace_backend(bridge->space), RETURN_VA, USER_PAGE_SIZE))
        panic("return bridge still leased at final release");
    kaspace_put(bridge->space);
    kfree(bridge);
}

LONG thread_startup_arch_bridge_acquire(KASPACE *space, void **result)
{
    THREAD_RETURN_BRIDGE *bridge = NULL;
    LONG error;
    if (!result || *result) return EINVAL;
    error = thread_return_bridge_acquire(space, &bridge);
    if (!error) *result = bridge;
    return error;
}
BOOL thread_startup_arch_bridge_valid(const void *bridge)
{
    return thread_return_bridge_valid(bridge);
}
VADDR thread_startup_arch_bridge_address(const void *bridge)
{
    return thread_return_bridge_address(bridge);
}
void thread_startup_arch_bridge_release(void **result)
{
    THREAD_RETURN_BRIDGE *bridge;
    if (!result) return;
    bridge = *result; *result = NULL;
    thread_return_bridge_release(&bridge);
}
