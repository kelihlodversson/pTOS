/* Private anonymous COW metadata. Included once by the selected MMU core.
 * Hardware descriptors and allocation mechanics remain in that core. */
#include "aspace_index.h"
#if CONF_WITH_PRIVATE_CLONE
struct cow_frame {
    struct cow_index index;
    PADDR physical;
    ULONG references;
};
struct cow_mapping {
    struct cow_index index;
    VADDR address;
    UWORD protection;
    BOOL prepared; /* new source record, reversible until clone commit */
    struct cow_frame *frame;
    struct cow_mapping *next;
    struct cow_mapping **previous;
};
static BOOL cow_release_backing(PADDR physical);
static void cow_unlink_mapping(ASPACE *, VADDR);
static struct cow_mapping *cow_find(const ASPACE *, VADDR);
static BOOL clone_injected(void);
#endif
