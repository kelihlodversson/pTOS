/* Private anonymous COW metadata. Included once by the selected MMU core.
 * Hardware descriptors and allocation mechanics remain in that core. */
#if CONF_WITH_PRIVATE_CLONE
struct cow_frame {
    PADDR physical;
    ULONG references;
    struct cow_frame *next;
};
struct cow_mapping {
    VADDR address;
    UWORD protection;
    BOOL prepared; /* new source record, reversible until clone commit */
    struct cow_frame *frame;
    struct cow_mapping *next;
};
static BOOL cow_release_backing(PADDR physical);
static void cow_unlink_mapping(ASPACE *, VADDR);
static struct cow_mapping *cow_find(const ASPACE *, VADDR);
static BOOL clone_injected(void);
#endif
