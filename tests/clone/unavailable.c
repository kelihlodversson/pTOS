/* The initial dispatcher never dereferences user arguments or creates a child. */
#include <assert.h>
#include <stdint.h>
#include "clone.h"
#include "gemerror.h"

int main(void)
{
    long native[] = { 0x190, 1, 28 };
    int16_t m68k[] = { 0x190, 0, 1, 0, 28 };
    assert(clone_dispatch_native(native) == EINVFN);
    assert(clone_dispatch_m68k(m68k) == EINVFN);
    assert(xclone(UINT64_MAX, UINT64_MAX) == EINVFN);
    return 0;
}
