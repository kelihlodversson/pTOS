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
 * window. May survive yields/user execution. Rejects borrowed and file pages;
 * anonymous COW may change frames while retaining this logical VA hold.
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
/* Pure admission check for a whole multi-root mutation; never revokes. */
BOOL kaspace_mapping_can_change(const struct aspace *backend, VADDR address, VADDR bytes);
/* Pure first busy interval within [from,to), clipped to that gap. Includes
 * durable/sealed views and pending retirement fences in the existing ledger. */
BOOL kaspace_next_held(const KASPACE *space,VADDR from,VADDR to,VADDR *first,VADDR *end);

BOOL kaspace_mapping_change(struct aspace *backend, VADDR address, VADDR bytes);
#if CONF_WITH_NATIVE_SCHED
/* Loader-only, synchronous no-yield permission preparation. Abort only after
 * releasing all new startup/region holds. Existing lease pointers cannot be
 * released/reentered during the transaction. Never an IRQ/SMP primitive. */
typedef struct kaspace_permission_transaction KASPACE_PERMISSION_TRANSACTION;
typedef struct { VADDR address,bytes; UWORD rights; } KASPACE_PROTECTION;
LONG kaspace_permission_prepare(KASPACE *,const KASPACE_PROTECTION *,UWORD,
                               KASPACE_PERMISSION_TRANSACTION **);
/* PRG borrowed-text variant; unsupported backends return EINVFN. The live
 * R1 token must cover every rounded page with USER backing rights and remain
 * owned until finish. Its nonowning journal copy never releases the token.
 * Changes mapping rights only; no raw allocation ownership or pin changes. */
struct procmem_hold;
LONG kaspace_permission_prepare_procmem(KASPACE *,const struct procmem_hold *,
        const KASPACE_PROTECTION *,UWORD,KASPACE_PERMISSION_TRANSACTION **);
void kaspace_permission_finish(KASPACE_PERMISSION_TRANSACTION **,BOOL commit);
#endif

/* Logical reservation-preserving lazy/file backing only. This cannot authorize
 * arbitrary remap, unmap or permission changes, nor bypass sealed aliases. */
#if CONF_WITH_SHARED_VM_PEXEC_TEST
/* Backend-only, after complete resident private-frame preflight; only image
 * write removal which preserves all retained minimum-access obligations. */
BOOL kaspace_relro_change(struct aspace *,VADDR,VADDR);
#endif
BOOL kaspace_materialization_change(struct aspace *backend, VADDR address,
                                   VADDR bytes, UWORD logical_rights);

#if CONF_WITH_PRIVATE_CLONE
/* Physical COW preserves logical VA/rights and durable stack holds. Reject
 * preparation/sealed leases that may retain a cached physical write alias. */
BOOL kaspace_cow_change(struct aspace *backend, VADDR address, VADDR bytes);
#endif
/* Ordinary Malloc reservations only. Loader image staging and loans remain
 * separately owned by KPROC and never put borrowed PDs in this table. */
typedef struct {
    VADDR va;
    ULONG pages;
    UQUAD reservation; /* immutable birth identity; not a physical frame */
    UWORD flags; /* zero for ordinary allocations */
} KASPACE_HEAP_BLOCK;
#define KASPACE_HEAP_GUARD 1U
typedef struct {
    KASPACE_HEAP_BLOCK *blocks;
    ULONG count, capacity;
} KASPACE_HEAP;
KASPACE_HEAP *kaspace_heap(KASPACE *space);
BOOL kaspace_heap_contains(KASPACE *space, UQUAD address);
BOOL kaspace_heap_protection(KASPACE *,UQUAD address,UWORD *rights);
/* -1: not reserved; otherwise the usual demand-fault result. */
int kaspace_heap_demand_page(KASPACE *space, UQUAD address, UWORD access);
#if CONF_WITH_MMAP
#include "fileback.h"
typedef struct mmap_region {
    ULONG address, length;
    UWORD prot;
    FILE_BACKING *backing;
    ULONG offset;
    UQUAD reservation; /* split fragments preserve this original identity */
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

/* Reservation ownership, including absent/lazy pages. The authoritative
 * ledger is the existing heap/mmap metadata; no page is faulted in. */
typedef struct { UQUAD ticket; } KASPACE_RESERVATION_HOLD;
typedef struct {
    VADDR address, bytes;
    UQUAD identity;
    UWORD rights, flags;
} KASPACE_RESERVATION_INFO;
#define KASPACE_RESERVATION_LAZY 1
#define KASPACE_RESERVATION_CAPTURED 2
#define KASPACE_RESERVATION_INACCESSIBLE 4 /* ownership only, no USER access */
UQUAD kaspace_reservation_next(void); /* 0 on permanent exhaustion */
BOOL kaspace_reservation_info(KASPACE *space, VADDR address, VADDR bytes,
                             KASPACE_RESERVATION_INFO *result);
LONG kaspace_reservation_hold(KASPACE *space, UQUAD identity,
        VADDR address, VADDR bytes, UWORD rights, UQUAD owner_number,
        void *owner_identity, KASPACE_RESERVATION_HOLD *result);
/* Trusted KPROC capture only: descriptor is freshly authenticated against
 * its live image/basepage/environment/stack/staging owner before this call.
 * A copied descriptor is NOT authority; the upper owner revalidates it. */
LONG kaspace_reservation_hold_captured(KASPACE *space,
        const KASPACE_RESERVATION_INFO *description, VADDR address, VADDR bytes,
        UWORD rights, UQUAD owner_number, void *owner_identity,
        KASPACE_RESERVATION_HOLD *result);
BOOL kaspace_retained_lazy_protection(KASPACE *space, VADDR address, UWORD *rights);
/* Borrowed canonical owner; source ticket must stay owned across duplication. */
void *kaspace_reservation_owner(const KASPACE_RESERVATION_HOLD *hold);
#if CONF_WITH_SHARED_VM_PEXEC_TEST
/* Pure authentication of this selected live captured nonlazy receipt, not a
 * query whose metadata could be supplied by another hold. Caller owns the
 * receipt throughout; no retain, backing, access grant or transfer occurs. */
BOOL kaspace_reservation_matches(const KASPACE_RESERVATION_HOLD *,
        const KASPACE *,UQUAD birth,VADDR exact_address,VADDR exact_bytes);
#endif
LONG kaspace_reservation_duplicate(const KASPACE_RESERVATION_HOLD *source,
        void *owner_identity, KASPACE_RESERVATION_HOLD *result);
BOOL kaspace_reservation_release(KASPACE_RESERVATION_HOLD *hold, void **owner_identity);
/* Stopped/unstarted owner only. Transfer cleanup into an existing blocking
 * reservation lease without allocation; release after its last conflicting
 * owner, outside exclusion. These are not public destructive-mutation bypasses. */
BOOL kaspace_retire_private(KASPACE *space, VADDR address, VADDR bytes);
BOOL kaspace_retire_borrow(KASPACE *space, VADDR address, VADDR bytes);
/* Backend-only: requires an exact published, consumed, in-flight retirement
 * owner and no live conflicting holder. Ordinary mutations cannot use this. */
BOOL kaspace_retirement_change(struct aspace *backend,VADDR address,VADDR bytes,BOOL private);


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

#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
ULONG kaspace_test_live(void); /* address spaces with at least one owner */
ULONG kaspace_test_references(void);
#endif
#if CONF_WITH_X86_64_MEMTEST
/* Fail the next adoption, leaving backend ownership with its caller. */
void kaspace_test_fail_next(void);
/* Fail the Nth lease acquisition before allocation/reference retention. */
void kaspace_test_fail_lease_after(UWORD count);
#endif

#endif
