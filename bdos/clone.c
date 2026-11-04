/* Pclone GEMDOS entry. The backend is not available yet. */
#include "gemerror.h"
#include "clone.h"

long xclone(uint64_t address, uint64_t size)
{
    /* Do not read caller memory or report successful creation until argument
     * validation and the clone backend are both available. */
    (void)address;
    (void)size;
    return EINVFN;
}
