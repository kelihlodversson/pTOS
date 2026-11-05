/* Independent ownership of an architecture address-space backend. */
#ifndef KASPACE_H
#define KASPACE_H

#include "portab.h"
#include <aspace_types.h>
struct aspace;
typedef struct kaspace KASPACE;
typedef struct kaspace_lease KASPACE_LEASE;
/* Startup leases require resident mappings and retain the containing ASPACE.
 * Preparation leases are revocable: ANY overlapping mapping/permission change
 * permanently invalidates them, including same-address/same-permission remaps.
 * No translation snapshot or wrapping generation counter can revive a lease.
 * All calls/mutations are serialized in cooperative process context. */
LONG kaspace_lease_acquire(KASPACE *space, VADDR address, VADDR bytes,
                          UWORD access, KASPACE_LEASE **result);
BOOL kaspace_lease_valid(const KASPACE_LEASE *lease);
/* Atomic, allocation-free group seal. Failure changes none of the leases.
 * Success excludes overlapping mutations until release. This short critical
 * section MUST NOT yield, invoke callbacks, or enter user mode. Future running
 * stack retention requires a separate execution-lifetime policy (#892/#893). */
BOOL kaspace_lease_seal(KASPACE_LEASE *const *leases, UWORD count);
void kaspace_lease_release(KASPACE_LEASE **lease);
/* Durable private-mapping hold, distinct from the short sealed publication
 * window. May survive yields/user execution. Rejects borrowed and COW pages.
 * The caller must release from a safe context after execution cannot resume. */
BOOL kaspace_lease_hold_private(KASPACE_LEASE *lease);
/* User stacks must lie in ordinary heap or anonymous mmap reservations, never
 * loader staging/borrowed basepages. Acquisition never backs lazy/COW pages. */
LONG kaspace_stack_hold_acquire(KASPACE *space, VADDR address, VADDR bytes,
                               KASPACE_LEASE **result);
KASPACE *kaspace_stack_hold_space(const KASPACE_LEASE *hold); /* borrowed */
/* Backend mutation barrier, called BEFORE changing leaves or lazy metadata.
 * FALSE means a sealed lease or durable hold rejects it, nothing revoked.
 * TRUE revokes overlapping preparation leases even if the mutation later fails.
 * Inputs are rounded out to pages. Backend lifetime is retained by each lease. */
BOOL kaspace_mapping_change(struct aspace *backend, VADDR address, VADDR bytes);
/* Ordinary Malloc reservations only. Loader image staging and loans remain
 * separately owned by KPROC and never put borrowed PDs in this table. */
typedef struct {
    VADDR va;
    ULONG pages;
} KASPACE_HEAP_BLOCK;
typedef struct {
    KASPACE_HEAP_BLOCK *blocks;
    ULONG count, capacity;
} KASPACE_HEAP;
KASPACE_HEAP *kaspace_heap(KASPACE *space);
BOOL kaspace_heap_contains(KASPACE *space, UQUAD address);
/* -1: not reserved; otherwise the usual demand-fault result. */
int kaspace_heap_demand_page(KASPACE *space, UQUAD address, UWORD access);
#if CONF_WITH_MMAP
#include "fileback.h"
typedef struct mmap_region {
    ULONG address, length;
    UWORD prot;
    FILE_BACKING *backing;
    ULONG offset;
    struct mmap_region *next;
} MMAP_REGION;
typedef struct {
    MMAP_REGION *regions;
    UQUAD bytes;
} KASPACE_MMAP;
/* Borrowed metadata, valid only while holding the containing space. */
KASPACE_MMAP *kaspace_mmap(KASPACE *space);
/* Return the logical protection of a reservation without faulting it in. */
BOOL kaspace_mmap_protection(KASPACE *space, UQUAD address, UWORD *protection);
/* Shutdown drops file mappings from every live space, including spaces with
 * no remaining process. The registry itself holds no ownership references. */
void kaspace_shutdown_file_mappings(void);
/* Back a reserved mmap page using this space's metadata: -1 if the address
 * is not an mmap reservation, otherwise the normal demand-fault result. */
int kaspace_mmap_fault(KASPACE *space, UQUAD address, UWORD access);
/* Complete mmap fault dispatch: -1 means it is not an mmap reservation. */
int kaspace_mmap_demand_page(KASPACE *space, UQUAD address, UWORD access);
#endif

/* backend must be exclusively owned by the caller and not previously adopted.
 * On success transfers ownership; on failure ownership stays with the caller.
 * References may be manipulated only in serialized process context. */
KASPACE *kaspace_adopt(struct aspace *backend);
/* Return another owning reference, or NULL on reference-count exhaustion. */
KASPACE *kaspace_share(KASPACE *space);
/* Borrowed pointer: valid only while the caller holds an owning reference. */
struct aspace *kaspace_backend(const KASPACE *space);
/* Drop one owning reference. The final drop destroys the backend, then
 * releases mapping metadata and its backing references. */
void kaspace_put(KASPACE *space);
/* Independent cloning requires an architecture backend; none exists yet. */
LONG kaspace_clone(const KASPACE *source, KASPACE **result);

#if CONF_WITH_X86_64_MEMTEST
/* Fail the next adoption, leaving backend ownership with its caller. */
void kaspace_test_fail_next(void);
/* Fail the Nth lease acquisition before allocation/reference retention. */
void kaspace_test_fail_lease_after(UWORD count);
#endif

#endif
