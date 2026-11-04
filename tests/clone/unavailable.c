/* The initial dispatcher never dereferences user arguments or creates a child. */
#include <assert.h>
#include <stdint.h>
#include "clone.h"
#include "gemerror.h"

int main(void)
{
    assert(xclone(1, 28) == EINVFN);
    assert(xclone(UINT64_MAX, UINT64_MAX) == EINVFN);
    return 0;
}
