/* Exercise the production xclone handler with bounded caller-memory mocks. */
#include "emutos.h"
#include "fs.h"
#include "bdosstub.h"
#include "kproc.h"
#include "clone.h"
#include "clone_args.h"
#include "gemerror.h"
#include <assert.h>
#include <string.h>

PD *run;
static unsigned char memory[8192];
static unsigned copies;
static int second_page = 1, output_writable = 1;

BOOL kproc_copy_from_user(void *dst, VADDR address, ULONG size)
{
    uint64_t offset;
    copies++;
    if (address >= 0x80001000ULL) address -= 0x80000000ULL;
    if (address < 0x1000) return FALSE;
    offset = address - 0x1000;
    if (offset > sizeof(memory) || size > sizeof(memory) - offset ||
        (!second_page && offset + size > 4096)) return FALSE;
    memcpy(dst, memory + offset, size);
    return TRUE;
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
    before = copies;
    assert(xclone(0, 28) == EIMBA && xclone(0x100000000ULL, 28) == EIMBA);
    assert(xclone(0xfffffff0, 28) == EIMBA);
    assert(xclone(0x1000, 27) == EINVAL && xclone(0x1000, 0x100000000ULL) == EINVAL);
    assert(copies == before);
    args.flags = 1; put(0x1000, args); assert(xclone(0x1000,28) == EINVAL);
    args = good(); args.tls = 0x7000; put(0x1000,args); assert(xclone(0x1000,28) == EINVAL);
    args = good(); args.entry = 0x7000; put(0x1000,args); assert(xclone(0x1000,28) == EIMBA);
    args = good(); args.stack_base = 0x7000; put(0x1000,args); assert(xclone(0x1000,28) == EIMBA);
    args = good(); put(0x1000,args); output_writable = 0;
    assert(xclone(0x1000,28) == EIMBA); output_writable = 1;
    put(0x1ff4,args); second_page = 0; assert(xclone(0x1ff4,28) == EIMBA);
    second_page = 1; assert(xclone(0x1ff4,28) == EINVFN);
    assert(memory[0x1800] == 0xa5 && run == NULL);
    return 0;
}
