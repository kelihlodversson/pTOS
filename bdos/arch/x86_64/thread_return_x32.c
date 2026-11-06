/* Kernel-selected single-THREAD PROCESS return bridge, never user code input. */
#include "emutos.h"
#include "biosext.h"
#include "gemerror.h"
#include "kheap.h"
#include "procmem.h"
#include "kaspace.h"
#include "kproc.h"
#include "kthread.h"
#include "string.h"
#include "thread_return_x32.h"
#include "thread_startup_backend.h"

/* x32 SysV function result in EAX; GEMDOS takes UWORD status in RDI.
 * mov edi,eax; movabs rax,(GEMDOS_CLASS<<32)|Pterm; syscall; ud2.
 * No C stack accesses, dynamic relocations, user-selected PC or new syscall.
 * UD2 contains an unexpected return from the nonreturning termination call. */
static const UBYTE return_code[] = {
    0x89, 0xc7, 0x48, 0xb8, 0x4c, 0, 0, 0, 1, 0, 0, 0,
    0x0f, 0x05, 0x0f, 0x0b,
    /* Public void entry: xor edi,edi; same Pterm/launcher path. */
    0x31, 0xff, 0x48, 0xb8, 0x4c, 0, 0, 0, 1, 0, 0, 0,
    0x0f, 0x05, 0x0f, 0x0b
};
#define RETURN_VA (USER_VA_LIMIT - USER_PAGE_SIZE)
struct x32_return_bridge {
    KASPACE *space; /* owning; registry membership is borrowed */
    KASPACE_LEASE *hold;
    ULONG users;
    struct x32_return_bridge *next;
};
static X32_RETURN_BRIDGE *bridges;

BOOL x32_return_bridge_valid(const X32_RETURN_BRIDGE *bridge)
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

BOOL x32_return_bridge_matches(const X32_RETURN_BRIDGE *bridge, const ASPACE *space)
{
    return bridge && kaspace_backend(bridge->space) == space && x32_return_bridge_valid(bridge);
}

LONG x32_return_bridge_acquire(KASPACE *space, X32_RETURN_BRIDGE **result)
{
    X32_RETURN_BRIDGE *bridge;
    ASPACE *as;
    LONG error;
    if (!space || !result || *result) return EINVAL;
    for (bridge = bridges; bridge; bridge = bridge->next) if (bridge->space == space) {
        if (!x32_return_bridge_valid(bridge)) return EIMBA;
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

VADDR x32_return_bridge_address(const X32_RETURN_BRIDGE *bridge)
{
    return x32_return_bridge_valid(bridge) ? RETURN_VA : 0;
}

VADDR x32_return_bridge_zero_address(const X32_RETURN_BRIDGE *bridge)
{
    return x32_return_bridge_valid(bridge) ? RETURN_VA + 16 : 0;
}

BOOL x32_return_bridge_process_eligible(KPROC *process, KTHREAD *thread)
{
    /* The process's non-owning membership must identify exactly one THREAD.
     * No observer callback or user state controls this decision. */
    return kproc_thread_is_sole(process, thread);
}

void x32_return_bridge_release(X32_RETURN_BRIDGE **result)
{
    X32_RETURN_BRIDGE *bridge, **link;
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
    X32_RETURN_BRIDGE *bridge = NULL;
    LONG error;
    if (!result || *result) return EINVAL;
    error = x32_return_bridge_acquire(space, &bridge);
    if (!error) *result = bridge;
    return error;
}
BOOL thread_startup_arch_bridge_valid(const void *bridge)
{
    return x32_return_bridge_valid(bridge);
}
VADDR thread_startup_arch_bridge_address(const void *bridge)
{
    return x32_return_bridge_address(bridge);
}
void thread_startup_arch_bridge_release(void **result)
{
    X32_RETURN_BRIDGE *bridge;
    if (!result) return;
    bridge = *result; *result = NULL;
    x32_return_bridge_release(&bridge);
}
