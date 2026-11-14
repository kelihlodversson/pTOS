/* Real generic/x32 startup builder; fake mappings and counted kernel stacks.
 * This never executes a context, changes hardware state or writes user memory. */
#include "emutos.h"
#include "biosext.h"
#include "procmem.h"
#include "kheap.h"
#include "kaspace.h"
#include "thread_startup.h"
#include "thread_startup_x32.h"
#include "thread_return_x32.h"
#include "thread_startup_backend.h"
#include "syscall_return.h"
#include "gemerror.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct aspace { int marker; };
static unsigned allocations, frees, destroys, stacks, fail_alloc, fail_stack;
static BOOL live[8], stack_present = TRUE, bridge_present;
static UBYTE bridge_code[4096];
#define BRIDGE (USER_VA_LIMIT - USER_PAGE_SIZE)
void panic(const char *fmt, ...) { (void)fmt; abort(); }
BOOL aspace_load_private(ASPACE *as, VADDR va, VADDR bytes, UWORD prot, const void *src, ULONG filesz)
{
    (void)as;
    assert(va == BRIDGE && bytes == 4096 && filesz <= 4096 && !bridge_present);
    assert(prot == (ASPACE_PROT_USER | ASPACE_PROT_EXEC));
    memcpy(bridge_code, src, filesz); bridge_present = TRUE; return TRUE;
}
BOOL aspace_unmap_private(ASPACE *as, VADDR va, VADDR bytes)
{
    assert(va == BRIDGE && bytes == 4096 && bridge_present);
    if (!kaspace_mapping_change(as,va,bytes)) return FALSE;
    bridge_present = FALSE; return TRUE;
}
BOOL aspace_copy_from_user(const ASPACE *as, void *dst, VADDR va, ULONG bytes)
{
    (void)as;
    if (va != BRIDGE || bytes > 4096 || !bridge_present) return FALSE;
    memcpy(dst,bridge_code,bytes); return TRUE;
}
BOOL aspace_private_range_ok(const ASPACE *as, VADDR va, VADDR bytes)
{
    UWORD prot;
    return bytes && aspace_translate(as,va,NULL,&prot);
}
static UWORD code_prot = ASPACE_PROT_USER | ASPACE_PROT_EXEC;
static UWORD stack_prot = ASPACE_PROT_USER | ASPACE_PROT_WRITE;
#if CONF_WITH_FORK_RETURN_TEST
static BOOL force_stack_alias;
BOOL kstack_contains(PADDR physical, uintptr_t address)
{
    uintptr_t base = 0x100000 + (physical / 4096) * 16384 - 16384;
    return force_stack_alias || (address >= base && address - base < 16384);
}
#endif
#define CODE 0x400000UL
#define STACK 0x10000000UL
#define OUTPUT 0x20000000UL
void *kalloc(ULONG size)
{
    void *p;
    if (fail_alloc && !--fail_alloc) return NULL;
    p = malloc(size); if (p) allocations++; return p;
}
void kfree(void *p) { if (p) { frees++; free(p); } }
void aspace_destroy(ASPACE *as) { assert(as->marker == 42); destroys++; }
ASPACE_ROOT aspace_root(const ASPACE *as) { assert(as->marker == 42); return 0xabc000; }
BOOL aspace_translate(const ASPACE *as, VADDR address, PADDR *physical, UWORD *prot)
{
    UWORD bits;
    (void)as; (void)physical;
    if (address >= CODE && address < CODE + 4096) bits = code_prot;
    else if (stack_present && address >= STACK && address < STACK + 8192) bits = stack_prot;
    else if (address >= OUTPUT && address < OUTPUT + 4096) bits = stack_prot;
    else if (bridge_present && address >= BRIDGE && address < BRIDGE + 4096) bits = ASPACE_PROT_USER | ASPACE_PROT_EXEC;
    else return FALSE;
    if (prot) *prot = bits;
    return TRUE;
}
BOOL aspace_user_range_ok(const ASPACE *as, VADDR va, VADDR size, BOOL write)
{
    (void)as;
    return stack_present && va >= STACK && va < STACK + 8192 && size <= STACK + 8192 - va &&
        (stack_prot & ASPACE_PROT_USER) && (!write || (stack_prot & ASPACE_PROT_WRITE));
}
BOOL aspace_copy_to_user_ok(const ASPACE *as, VADDR va, ULONG size)
{
    UWORD prot;
    return va >= OUTPUT && va < OUTPUT + 4096 && size <= OUTPUT + 4096 - va &&
        aspace_translate(as, va, NULL, &prot) && (prot & ASPACE_PROT_WRITE);
}
BOOL aspace_copy_to_user(const ASPACE *as, VADDR va, const void *p, ULONG size)
{ (void)as; (void)va; (void)p; (void)size; abort(); }
#if CONF_WITH_MMAP
void fileback_put(FILE_BACKING *backing) { (void)backing; abort(); }
#endif
PADDR kstack_alloc(uintptr_t *top)
{
    unsigned i;
    if (fail_stack && !--fail_stack) return 0;
    for (i = 1; i < 8; i++) if (!live[i]) {
        live[i] = TRUE; stacks++; *top = 0x100000 + i * 16384 - 8; return i * 4096;
    }
    abort();
}
void kstack_free(PADDR physical)
{
    unsigned i;
    if (!physical) return;
    i = physical / 4096;
    assert(i < 8 && live[i] && stacks); live[i] = FALSE; stacks--;
}
static unsigned observers, acquire_calls, release_calls;
static BOOL fail_observer, revoke_observer;
static KASPACE *observed_space;
static LONG observer_acquire(void *context)
{
    assert(context == &observers); acquire_calls++;
    if (fail_observer) return ENSMEM;
    if (revoke_observer) assert(kaspace_mapping_change(kaspace_backend(observed_space), CODE, 1));
    observers++; return E_OK;
}
static void observer_release(void *context)
{
    assert(context == &observers && observers); observers--; release_calls++;
    /* Abort must leave the sealed window BEFORE invoking this callback. */
    assert(kaspace_mapping_change(kaspace_backend(observed_space), CODE, 1));
}
int main(void)
{
    ASPACE backend = { 42 };
    KASPACE *space = kaspace_adopt(&backend);
    THREAD_STARTUP_ARGS args, bad;
    THREAD_STARTUP_DRAFT *draft = NULL, *held;
    const X32_THREAD_STARTUP_DRAFT *image;
    unsigned i, before;
    THREAD_STARTUP_OBSERVER observer = { observer_acquire, observer_release };
    KASPACE_LEASE *lease = NULL, *old;
    UQUAD regs[15];
    assert(space);
    args.entry = CODE; args.argument = 0xfedcba98UL;
    args.stack_base = STACK; args.stack_size = 8192;
#if CONF_WITH_FORK_RETURN_TEST
    {
        SYSCALL_RETURN saved;
        X32_THREAD_ENTRY_IMAGE expected;
        LONG error = ENSMEM;
        memset(&saved, 0, sizeof(saved));
        saved.space = space; saved.mappings = args;
        saved.user.cs = 0x2b; saved.user.ss = saved.user.ds = saved.user.es = 0x23;
        saved.user.rip = CODE; saved.user.rsp = STACK + 512;
        saved.user.cr3 = 0xdead000; saved.user.rflags = 0x243;
        saved.user.rax = 0x100000190ULL; saved.user.rbx = 0x12345678;
        memset(saved.user.fpu, 0xa5, sizeof(saved.user.fpu));
        expected = saved.user; expected.rax = 0; expected.cr3 = aspace_root(&backend);
        before = allocations - frees;
        /* Every allocation in real saved-return construction fails in turn.
         * The final attempt succeeds once there is no remaining allocation. */
        for (i = 1; i < 32; i++) {
            fail_alloc = i;
            error = thread_startup_prepare_saved_return(space, &saved, &draft);
            fail_alloc = 0;
            if (!error) {
                image = thread_startup_image(draft);
                assert(image->saved_return && !memcmp(&image->user, &expected, sizeof(expected)));
                saved.user.rbx ^= 1;
                assert(image->user.rbx == expected.rbx); /* no creator-record alias */
                assert(kaspace_mapping_change(&backend, CODE, 1));
                assert(thread_startup_validate(draft) == EIMBA);
                thread_startup_abort(&draft);
                break;
            }
            assert(error == ENSMEM && !draft && !stacks && !bridge_present);
            assert(allocations - frees == before);
        }
        assert(!error && i < 32 && !stacks && allocations - frees == before);
        force_stack_alias = TRUE;
        assert(thread_startup_prepare_saved_return(space, &saved, &draft) == EACCDN);
        force_stack_alias = FALSE;
        saved.user.fs_base = 0x1000;
        assert(thread_startup_prepare_saved_return(space, &saved, &draft) == EINVFN);
        assert(!draft && !stacks && !bridge_present && allocations - frees == before);
    }
#endif
    {
        X32_RETURN_BRIDGE *bridge = NULL, *second = NULL;
        assert(x32_return_bridge_acquire(space,&bridge) == E_OK);
        assert(x32_return_bridge_acquire(space,&second) == E_OK && second == bridge);
        assert(!kaspace_mapping_change(&backend,BRIDGE,4096));
        {
            ASPACE foreign = { 42 };
            void *foreign_image = NULL;
            assert(thread_startup_arch_prepare(&foreign,&args,bridge,&foreign_image) == EIMBA && !foreign_image);
            bridge_code[0] ^= 1;
            assert(!x32_return_bridge_valid(bridge));
            bridge_code[0] ^= 1;
            assert(x32_return_bridge_valid(bridge));
        }
        x32_return_bridge_release(&bridge);
        assert(x32_return_bridge_valid(second));
        x32_return_bridge_release(&second);
        /* Even identical instructions at an occupied VA are not kernel trust. */
        bridge_present = TRUE;
        assert(x32_return_bridge_acquire(space,&bridge) == EACCDN && !bridge);
        bridge_present = FALSE;
    }
    observed_space = space;
    /* draft, image, entry lease, bridge lease, stack lease */
    for (i = 1; i <= 7; i++) {
        before = allocations - frees; fail_alloc = i;
        assert(thread_startup_prepare(space,&args,&draft) == ENSMEM && !draft);
        assert(!stacks && allocations - frees == before);
    }
    for (i = 1; i <= 2; i++) {
        before = allocations - frees; fail_alloc = i;
        assert(thread_startup_prepare(space,&args,&draft) == ENSMEM && !draft);
        assert(!stacks && allocations - frees == before);
        fail_stack = i;
        assert(thread_startup_prepare(space,&args,&draft) == ENSMEM && !draft);
        assert(!stacks && allocations - frees == before);
    }
#if CONF_WITH_X86_64_MEMTEST
    for (i = 1; i <= 3; i++) {
        before = allocations - frees; thread_startup_x32_test_fail_after(i);
        assert(thread_startup_prepare(space,&args,&draft) == ENSMEM && !draft);
        assert(!stacks && allocations - frees == before);
    }
#endif
    bad = args; bad.argument = 0x100000000ULL;
    assert(thread_startup_prepare(space,&bad,&draft) == EINVAL && !draft);
    bad = args; bad.stack_size = 136;
    assert(thread_startup_prepare(space,&bad,&draft) == EINVAL && !draft);
    bad = args; bad.stack_base++;
    assert(thread_startup_prepare(space,&bad,&draft) == EINVAL && !draft);
    bad = args; bad.stack_size = 143;
    assert(thread_startup_prepare(space,&bad,&draft) == EINVAL && !draft);
    bad = args; bad.stack_size = 144;
    assert(thread_startup_prepare(space,&bad,&draft) == E_OK);
    assert(((const X32_THREAD_STARTUP_DRAFT *)thread_startup_image(draft))->user.rsp == STACK + 136);
    thread_startup_abort(&draft);
    bad = args; bad.entry = STACK;
    assert(thread_startup_prepare(space,&bad,&draft) == EIMBA && !draft);
    assert(thread_startup_prepare(space,&args,&draft) == E_OK && draft && stacks == 2);
    held = draft; before = allocations - frees;
    assert(thread_startup_prepare(space,&args,&draft) == EINVAL && draft == held && allocations - frees == before);
    image = thread_startup_image(draft);
    assert(image && thread_startup_validate(draft) == E_OK && !((uintptr_t)image->user.fpu & 15));
    assert(image->user.rip == CODE && image->return_pc == BRIDGE);
    assert(image->user.rdi == 0xfedcba98ULL && image->user.rflags == 0x202 && image->user.cr3 == 0xabc000);
    assert((image->user.rsp & 15) == 8 && image->user.rsp >= STACK + 128);
    memcpy(regs, &image->user, sizeof(regs));
    for (i = 0; i < 15; i++) assert(regs[i] == (i == 5 ? 0xfedcba98ULL : 0));
    assert(!image->user.fs && !image->user.gs && !image->user.fs_base && !image->user.gs_base);
    for (i = 0; i < 512; i++) {
        unsigned expected = i == 0 ? 0x7f : i == 1 ? 3 : i == 24 ? 0x80 : i == 25 ? 0x1f : i == 28 ? 0xbf : i == 29 ? 0xff : 0;
        assert(image->user.fpu[i] == expected);
    }
    assert(thread_startup_state(draft) == THREAD_STARTUP_PREPARED);
    assert(thread_startup_seal(draft) == EINVAL);
    assert(thread_startup_publication_check(draft) == EINVFN);
    before = allocations - frees;
    for (i = 1; i <= 2; i++) {
        fail_alloc = i;
        assert(thread_startup_prepare_outputs(draft,OUTPUT,OUTPUT+4) == ENSMEM);
        assert(allocations - frees == before && thread_startup_validate(draft) == E_OK);
    }
    assert(thread_startup_prepare_outputs(draft,OUTPUT,OUTPUT+2) == EINVAL);
    assert(thread_startup_prepare_outputs(draft,OUTPUT,OUTPUT+4096) == EIMBA);
    assert(allocations - frees == before);
    assert(thread_startup_prepare_outputs(draft,STACK,CODE) == EINVAL);
    assert(thread_startup_prepare_outputs(draft,OUTPUT,OUTPUT+4) == E_OK);
    assert(thread_startup_prepare_outputs(draft,OUTPUT,OUTPUT+4) == EINVAL);
    fail_observer = TRUE;
    assert(thread_startup_observe(draft,&observer,&observers) == ENSMEM);
    assert(!observers && thread_startup_state(draft) == THREAD_STARTUP_PREPARED);
    fail_observer = FALSE;
    assert(thread_startup_observe(draft,&observer,&observers) == E_OK && observers == 1);
    assert(thread_startup_observe(draft,&observer,&observers) == EINVAL);
    assert(thread_startup_seal(draft) == E_OK);
    assert(thread_startup_state(draft) == THREAD_STARTUP_SEALED);
    assert(thread_startup_seal(draft) == EINVAL);
    assert(!kaspace_mapping_change(&backend, STACK + 4096, 1));
    assert(!kaspace_mapping_change(&backend, OUTPUT, 4));
    assert(!kaspace_mapping_change(&backend, CODE, 1));
    assert(thread_startup_validate(draft) == E_OK);
    assert(thread_startup_publication_check(draft) == EINVFN);
    thread_startup_abort(&draft); thread_startup_abort(&draft);
    assert(!draft && !observers && release_calls == 1 && acquire_calls == 2 && !stacks);
    /* Same address and permissions can never resurrect a revoked lease. */
    assert(thread_startup_prepare(space,&args,&draft) == E_OK);
    assert(kaspace_mapping_change(&backend, STACK, 8192));
    stack_present = FALSE; stack_present = TRUE;
    assert(thread_startup_validate(draft) == EIMBA);
    assert(thread_startup_observe(draft,&observer,&observers) == EIMBA && !observers);
    assert(thread_startup_publication_check(draft) == EIMBA);
    thread_startup_abort(&draft);
    /* A callback mutation is detected and its successful registration undone. */
    assert(thread_startup_prepare(space,&args,&draft) == E_OK);
    revoke_observer = TRUE;
    assert(thread_startup_observe(draft,&observer,&observers) == EIMBA && !observers);
    revoke_observer = FALSE;
    thread_startup_abort(&draft);
    assert(thread_startup_prepare(space,&args,&draft) == E_OK);
    code_prot = ASPACE_PROT_USER;
    assert(thread_startup_validate(draft) == EIMBA);
    code_prot |= ASPACE_PROT_EXEC;
    stack_prot = ASPACE_PROT_USER;
    assert(thread_startup_validate(draft) == EIMBA);
    stack_present = FALSE;
    thread_startup_abort(&draft); thread_startup_abort(&draft);
    assert(!draft && !stacks && !destroys);
    /* Generic lease ownership and sticky revocation, including occupied handle. */
    assert(kaspace_lease_acquire(space,CODE,1,ASPACE_PROT_USER|ASPACE_PROT_EXEC,&lease) == E_OK);
    old = lease; before = allocations - frees;
    assert(kaspace_lease_acquire(space,CODE,1,ASPACE_PROT_USER,&lease) == EINVAL && lease == old);
    assert(allocations - frees == before);
    assert(kaspace_mapping_change(&backend,CODE,4096) && !kaspace_lease_valid(lease));
    assert(!kaspace_lease_seal(&lease,1));
    kaspace_put(space); /* lease independently keeps backend alive */
    assert(!destroys);
    kaspace_lease_release(&lease); kaspace_lease_release(&lease);
    assert(destroys == 1 && allocations == frees && !bridge_present);
    return 0;
}

BOOL aspace_release_lazy(ASPACE *as,VADDR va,VADDR bytes) { (void)as;(void)va;(void)bytes;abort();return FALSE; }

void aspace_unborrow(ASPACE *as,VADDR va,VADDR bytes) { (void)as;(void)va;(void)bytes;abort(); }

BOOL aspace_release_lazy_retired(ASPACE *as,VADDR va,VADDR bytes) { (void)as;(void)va;(void)bytes;abort();return FALSE; }

void aspace_unborrow_retired(ASPACE *as,VADDR va,VADDR bytes) { (void)as;(void)va;(void)bytes;abort(); }

BOOL aspace_defer_unborrow(ASPACE *as,VADDR va,VADDR bytes) { (void)as;(void)va;(void)bytes;abort(); }
UQUAD ksched_context_id(void) { return 0; } /* one serialized hosted actor */
