/* Real generic/x32 startup builder; fake mappings and counted kernel stacks.
 * This never executes a context, changes hardware state or writes user memory. */
#include "emutos.h"
#include "procmem.h"
#include "kheap.h"
#include "kaspace.h"
#include "thread_startup.h"
#include "thread_startup_x32.h"
#include "gemerror.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct aspace { int marker; };
static unsigned allocations, frees, destroys, stacks, fail_alloc, fail_stack;
static BOOL live[8], stack_present = TRUE;
static UWORD code_prot = ASPACE_PROT_USER | ASPACE_PROT_EXEC;
static UWORD stack_prot = ASPACE_PROT_USER | ASPACE_PROT_WRITE;
#define CODE 0x400000UL
#define STACK 0x10000000UL
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
int main(void)
{
    ASPACE backend = { 42 };
    KASPACE *space = kaspace_adopt(&backend);
    THREAD_STARTUP_ARGS args, bad;
    THREAD_STARTUP_DRAFT *draft = NULL, *held;
    const X32_THREAD_STARTUP_DRAFT *image;
    unsigned i, before;
    UQUAD regs[15];
    assert(space);
    args.entry = CODE; args.return_entry = CODE + 16; args.argument = 0xfedcba98UL;
    args.stack_base = STACK; args.stack_size = 8192;
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
    bad = args; bad.entry = STACK;
    assert(thread_startup_prepare(space,&bad,&draft) == EIMBA && !draft);
    assert(thread_startup_prepare(space,&args,&draft) == E_OK && draft && stacks == 2);
    held = draft; before = allocations - frees;
    assert(thread_startup_prepare(space,&args,&draft) == EINVAL && draft == held && allocations - frees == before);
    image = thread_startup_image(draft);
    assert(image && thread_startup_validate(draft) == E_OK && !((uintptr_t)image->user.fpu & 15));
    assert(image->user.rip == CODE && image->return_pc == CODE + 16);
    assert(image->user.rdi == 0xfedcba98ULL && image->user.rflags == 0x202 && image->user.cr3 == 0xabc000);
    assert((image->user.rsp & 15) == 8 && image->user.rsp >= STACK + 128);
    memcpy(regs, &image->user, sizeof(regs));
    for (i = 0; i < 15; i++) assert(regs[i] == (i == 5 ? 0xfedcba98ULL : 0));
    assert(!image->user.fs && !image->user.gs && !image->user.fs_base && !image->user.gs_base);
    for (i = 0; i < 512; i++) {
        unsigned expected = i == 0 ? 0x7f : i == 1 ? 3 : i == 24 ? 0x80 : i == 25 ? 0x1f : i == 28 ? 0xbf : i == 29 ? 0xff : 0;
        assert(image->user.fpu[i] == expected);
    }
    code_prot = ASPACE_PROT_USER;
    assert(thread_startup_validate(draft) == EIMBA);
    code_prot |= ASPACE_PROT_EXEC;
    stack_prot = ASPACE_PROT_USER;
    assert(thread_startup_validate(draft) == EIMBA);
    stack_present = FALSE;
    thread_startup_abort(&draft); thread_startup_abort(&draft);
    assert(!draft && !stacks && !destroys);
    kaspace_put(space);
    assert(destroys == 1 && allocations == frees);
    return 0;
}
