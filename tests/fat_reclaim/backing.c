/* A partial final reclaim must block allocation until its retry succeeds. */
#include "portab.h"
void *kalloc(ULONG size);
void kfree(void *memory);
#include "../../bdos/fileback.c"

static void *escape[5];
static int fail, calls, released;
static OFD retired_file, live_file;

long ofd_put(OFD *file)
{
    if (file != &retired_file) return EINTRN;
    calls++;
    if (fail == 1) __builtin_longjmp(escape, 1);
    return fail == 2 ? EWRITF : E_OK;
}
void kfree(void *memory) { (void)memory; released++; }

int main(void)
{
    static FILE_BACKING live, retired;
    live.file = &live_file;
    live.references = 1;
    live.next = &retired;
    retired.file = &retired_file;
    retired.references = 0;
    backings = &live;
    fail = 1;
    if (!__builtin_setjmp(escape)) {
        fileback_barrier();
        return 1;
    }
    if (released || live.next != &retired || calls != 1) return 2;
    fail = 2;
    if (fileback_barrier() != EWRITF || released || calls != 2) return 3;
    fail = 0;
    if (fileback_barrier() != E_OK || released != 1 || calls != 3 || live.next)
        return 4;
    if (fileback_barrier() != E_OK || calls != 3) return 5;
    return 0;
}
