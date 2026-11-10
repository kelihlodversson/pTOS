/* Exercise the production xclone handler with bounded caller-memory mocks. */
#include "emutos.h"
#include "fs.h"
#include "bdosstub.h"
#include "kproc.h"
#include "clone.h"
#include "clone_args.h"
#include "gemerror.h"
#if CONF_WITH_NATIVE_SCHED
#include "proc_clone.h"
#include "ksched.h"
#endif
#include <assert.h>
#include <string.h>

PD *run;
static unsigned char memory[8192];
static unsigned copies;
static LONG copy_error;
static unsigned scope_depth;
static int second_page = 1, output_writable = 1;

LONG kproc_copy_from_user_checked(void *dst, VADDR address, ULONG size)
{
    uint64_t offset;
    copies++;
#if CONF_WITH_NATIVE_SCHED
    assert(scope_depth == 1);
#endif
    if (copy_error) return copy_error;
    if (address >= 0x80001000ULL) address -= 0x80000000ULL;
    if (address < 0x1000) return EIMBA;
    offset = address - 0x1000;
    if (offset > sizeof(memory) || size > sizeof(memory) - offset ||
        (!second_page && offset + size > 4096)) return EIMBA;
    memcpy(dst, memory + offset, size);
    return E_OK;
}
BOOL kproc_validate_user_exec(VADDR address) { return address == 0x4000; }
BOOL kproc_user_access(PD *pd, VADDR address, ULONG size, BOOL write)
{
    (void)pd;
    assert(write);
    return (address == 0x5000 && size == 256) ||
           (address == 0x6000 && size == 4 && output_writable);
}
static CLONE_USER_V1 good(void)
{
    CLONE_USER_V1 args = { 0 };
    args.entry = 0x4000; args.stack_base = 0x5000; args.stack_size = 256;
    args.child_tid = 0x6000;
    return args;
}
static void put(uint32_t address, CLONE_USER_V1 args)
{ memcpy(memory + address - 0x1000, &args, sizeof(args)); }

int main(void)
{
    CLONE_USER_V1 args = good();
    unsigned before;
    memset(memory, 0xa5, sizeof(memory)); put(0x1000, args);
    assert(xclone(0x1000, 28) == EINVFN && copies == 1);
    assert(xclone(0x1000, 40) == EINVFN && copies == 2);
    assert(xclone(0x80001000ULL, 28) == EINVFN && copies == 3);
    copy_error = ENSMEM;
    assert(xclone(0x1000,32) == ENSMEM && !scope_depth);
    copy_error = EREADF;
    assert(xclone(0x1000,32) == EREADF && !scope_depth);
    copy_error = E_OK;
    before = copies;
    assert(xclone(0, 28) == EIMBA && xclone(0x100000000ULL, 28) == EIMBA);
    assert(xclone(0xfffffff0, 28) == EIMBA);
    assert(xclone(0x1000, 27) == EINVAL && xclone(0x1000, 0x100000000ULL) == EINVAL);
    assert(copies == before);
    args.flags = CLONE_VM; put(0x1000,args);
    assert(xclone(0x1000,28) == EINVFN); /* no managed source/backend */
    args.flags = CLONE_VM | CLONE_COPY_STANDARD_HANDLES | CLONE_COPY_CWD;
    put(0x1000,args); assert(xclone(0x1000,32) == EINVFN);
    args.flags = CLONE_SAME_PROCESS; put(0x1000,args);
    assert(xclone(0x1000,32) == EINVAL);
    args.flags = CLONE_VM | CLONE_SAME_PROCESS; put(0x1000,args);
    assert(xclone(0x1000,32) == EINVFN);
    args.flags = CLONE_VM | CLONE_SAME_PROCESS | CLONE_COPY_CWD; put(0x1000,args);
    assert(xclone(0x1000,32) == EINVAL);
    args.flags = 0x80000000U; put(0x1000, args); assert(xclone(0x1000,28) == EINVAL);
    args = good(); args.tls = 0x7000; put(0x1000,args); assert(xclone(0x1000,28) == EINVAL);
    args = good(); args.entry = 0x7000; put(0x1000,args); assert(xclone(0x1000,28) == EIMBA);
    args = good(); args.stack_base = 0x7000; put(0x1000,args); assert(xclone(0x1000,28) == EIMBA);
    args = good(); put(0x1000,args); output_writable = 0;
    assert(xclone(0x1000,28) == EIMBA); output_writable = 1;
    put(0x1ff4,args); second_page = 0; assert(xclone(0x1ff4,28) == EIMBA);
    second_page = 1; assert(xclone(0x1ff4,28) == EINVFN);
    assert(memory[0x1800] == 0xa5 && run == NULL && !scope_depth);
    return 0;
}

#if CONF_WITH_NATIVE_SCHED
void ksched_cleanup_begin(KSCHED_CLEANUP_SCOPE *scope)
{ assert(!scope->active && !scope_depth); scope->active = TRUE; scope_depth++; }
void ksched_cleanup_end(KSCHED_CLEANUP_SCOPE *scope)
{ assert(scope->active && scope_depth == 1); scope->active = FALSE; scope_depth--; }
KPROC *kproc_retain(PD *pd) { assert(!pd); return NULL; }
void kproc_release(KPROC *p) { (void)p; assert(0); }
LONG proc_clone_start_public(const PROC_CLONE_ARGS *a, const THREAD_STARTUP_ARGS *s, VADDR pid, VADDR tid)
{ (void)a; (void)s; (void)pid; (void)tid; assert(0); return EINVFN; }
#endif
