/* Address-space ownership; mapping and cloning remain backend responsibilities. */
#include "emutos.h"
#include "biosext.h"
#include "gemerror.h"
#include "kheap.h"
#include "procmem.h"
#include "string.h"
#include "kaspace.h"
#if CONF_WITH_NATIVE_SCHED
#include "ksched.h"
#endif
#if CONF_WITH_PRIVATE_CLONE_TEST
#include "mapping_lifetime_test.h"
#endif

struct kaspace_scope {
    KASPACE *space; /* borrowed; members/metadata remain inside this root */
    BOOL runtime; /* explicit loader lifetime; ordinary allocations are common */
    KASPACE_MEMBER *members;
    KASPACE_SCOPE *next;
};
struct kaspace_member {
    KASPACE_SCOPE *scope;
    UQUAD number;
    void *identity; /* borrowed canonical identity; detached before KPROC wipe */
    KASPACE_MEMBER *next, *owner_next;
};
struct kaspace {
    ASPACE *backend;
    ULONG refs;
    BOOL resident_permissions_tightened; /* monotonic stronger capture validation */
    KASPACE_HEAP heap;
    KASPACE_LEASE *leases;
    KASPACE_SCOPE *scopes;
    /* Canonical metadata is the no-allocation pending cleanup record. This
     * exact in-flight job uses the existing backend retirement protocol. */
    UQUAD retiring_birth;
    VADDR retiring_address, retiring_bytes;
    BOOL retiring_committed;
    BOOL reaping;
    BOOL mutating;
    UQUAD mutation_actor,reap_actor;
    struct kaspace *next;
#if CONF_WITH_MMAP
    KASPACE_MMAP mmap;
#endif
};

static UQUAD lease_ids; /* Non-recycled storage identity; not an owning handle. */
static KASPACE *spaces; /* borrowed registry; serialized with retain/release */
struct kaspace_lease {
    UQUAD identity;
    KASPACE *space; /* owning; list membership is non-owning */
    VADDR first, last; /* inclusive page addresses; no end-address overflow */
    UWORD access;
    BOOL revoked, sealed, held, stack;
    UQUAD reservation_ticket, reservation_id, owner_number;
    UQUAD attachment_ticket; /* distinct role; never an R2 hold or access grant */
    VADDR exact_address, exact_bytes;
    void *owner_identity;
    UWORD reservation_rights, reservation_flags;
    BOOL captured, retire_private, retire_running, retire_committed;
    UQUAD retire_heap_birth;
    VADDR retire_address, retire_bytes;
    KASPACE_LEASE *next;
};

static void reap_retired(KASPACE *space);
static BOOL scope_live(const KASPACE_SCOPE *scope);
static BOOL reservation_lookup(KASPACE *space,VADDR address,VADDR bytes,
                               KASPACE_RESERVATION_INFO *result,BOOL pending);
static BOOL reservation_range_matches(KASPACE *space,UQUAD birth,VADDR address,VADDR bytes,UWORD rights)
{
    VADDR at=address,end=address+bytes;
    while (at<end) {
        KASPACE_RESERVATION_INFO info;
        if (!reservation_lookup(space,at,1,&info,TRUE) || info.identity!=birth ||
            (info.flags&KASPACE_RESERVATION_CAPTURED) || info.rights!=rights) return FALSE;
        at=info.address+info.bytes;
    }
    return TRUE;
}
static BOOL mapping_live(KASPACE *space,KASPACE_SCOPE *scope,UQUAD birth,VADDR address,VADDR bytes)
{
    KASPACE_LEASE *lease;
    if (scope_live(scope)) return TRUE;
    for (lease=space->leases;lease;lease=lease->next)
        if (lease->attachment_ticket && lease->reservation_id==birth && !lease->revoked &&
            address>=lease->exact_address && address-lease->exact_address<lease->exact_bytes &&
            bytes<=lease->exact_bytes-(address-lease->exact_address)) return TRUE;
    return FALSE;
}
static BOOL pending_overlap(KASPACE *space,VADDR address,VADDR bytes)
{
    ULONG i;
    for (i=0;i<space->heap.count;i++) {
        KASPACE_HEAP_BLOCK *block=&space->heap.blocks[i];
        if (!mapping_live(space,block->scope,block->reservation,block->va,(VADDR)block->pages*USER_PAGE_SIZE) &&
            address<block->va+(VADDR)block->pages*USER_PAGE_SIZE &&
            block->va<address+bytes) return TRUE;
    }
#if CONF_WITH_MMAP
    {
        MMAP_REGION *region;
        for (region=space->mmap.regions;region;region=region->next)
            if (!mapping_live(space,region->scope,region->reservation,region->address,region->length) &&
                address<(UQUAD)region->address+region->length &&
                region->address<address+bytes) return TRUE;
    }
#endif
    return FALSE;
}

static BOOL scope_live(const KASPACE_SCOPE *scope)
{ return !scope || scope->members; } /* NULL: legacy root-owned metadata */

static UQUAD mapping_actor(void)
{
#if CONF_WITH_NATIVE_SCHED
    return ksched_context_id(); /* current continuation, no retain or checkpoint */
#else
    return 0;
#endif
}
BOOL kaspace_mutation_busy(const KASPACE *space)
{ return !space || space->mutating || space->reaping; }
BOOL kaspace_mutation_reentrant(const KASPACE *space)
{
    UQUAD actor=mapping_actor();
    return space && ((space->mutating && space->mutation_actor==actor) ||
                     (space->reaping && space->reap_actor==actor));
}
BOOL kaspace_mutation_begin(KASPACE *space)
{
    if (kaspace_mutation_busy(space)) return FALSE;
    space->mutating=TRUE;space->mutation_actor=mapping_actor();
    return TRUE;
}
void kaspace_mutation_end(KASPACE *space)
{
    if (!space || !space->mutating || space->mutation_actor!=mapping_actor())
        panic("mapping mutation owner changed");
    space->mutating=FALSE;space->mutation_actor=0;
#if CONF_WITH_MMAP
    kaspace_mmap_coalesce(space);
#endif
    reap_retired(space);
}

#if CONF_WITH_PRIVATE_CLONE_TEST
static UWORD fail_mapping_after;
void kaspace_test_fail_mapping_after(UWORD count) { fail_mapping_after=count; }
static void *mapping_alloc(ULONG bytes)
{
    if (fail_mapping_after && !--fail_mapping_after) return NULL;
    return kalloc(bytes);
}
#else
#define mapping_alloc(bytes) kalloc(bytes)
#endif

static LONG member_new(KASPACE *space,UQUAD number,void *identity,BOOL runtime,KASPACE_MEMBER **result)
{
    KASPACE_SCOPE *scope;
    KASPACE_MEMBER *member;
    if (!space || !number || !identity || !result || *result) return EINVAL;
    if (space->mutating || space->reaping) return EACCDN;
    scope=mapping_alloc(sizeof(*scope));
    if (!scope) return ENSMEM;
    member=mapping_alloc(sizeof(*member));
    if (!member) { kfree(scope);return ENSMEM; }
    if (!kaspace_share(space)) { kfree(member);kfree(scope);return ENSMEM; }
    scope->space=space;scope->runtime=runtime;scope->members=member;scope->next=space->scopes;
    member->scope=scope;member->number=number;member->identity=identity;
    member->next=member->owner_next=NULL;
    space->scopes=scope;
    *result=member;
    return E_OK;
}

LONG kaspace_member_new(KASPACE *space,UQUAD number,void *identity,KASPACE_MEMBER **result)
{ return member_new(space,number,identity,FALSE,result); }

LONG kaspace_member_add_runtime(KASPACE *space,UQUAD number,void *identity,
                               KASPACE_MEMBER **members,KASPACE_SCOPE **selected)
{
    KASPACE_MEMBER *fresh=NULL,**tail;
    LONG error;
    if (!members || !selected || *selected ||
        !kaspace_member_matches(*members,space,number,identity)) return EINVAL;
    error=member_new(space,number,identity,TRUE,&fresh);
    if (error) return error;
    for (tail=members;*tail;tail=&(*tail)->owner_next) { }
    *tail=fresh;*selected=fresh->scope;
    return E_OK;
}

static LONG member_inherit(const KASPACE_MEMBER *source,UQUAD number,void *identity,
                           BOOL general_only,KASPACE_MEMBER **result)
{
    KASPACE_MEMBER *prepared=NULL,*copy,**tail=&prepared;
    const KASPACE_MEMBER *at;
    if (!source || !number || !identity || !result || *result) return EINVAL;
    for (at=source;at;at=at->owner_next) {
        if (!at->identity || !at->number || !at->scope->members ||
            at->scope->space->mutating || at->scope->space->reaping) goto denied;
        if (general_only && at->scope->runtime) continue;
        copy=mapping_alloc(sizeof(*copy));
        if (!copy) goto failed;
        if (!kaspace_share(at->scope->space)) { kfree(copy);goto failed; }
        copy->scope=at->scope;copy->number=number;copy->identity=identity;
        copy->next=at->scope->members;at->scope->members=copy;
        copy->owner_next=NULL;*tail=copy;tail=&copy->owner_next;
    }
    *result=prepared;
    return E_OK;
denied:
    kaspace_member_release(&prepared);
    return EACCDN;
failed:
    kaspace_member_release(&prepared);
    return ENSMEM;
}

LONG kaspace_member_inherit(const KASPACE_MEMBER *source,UQUAD number,void *identity,
                           KASPACE_MEMBER **result)
{ return member_inherit(source,number,identity,FALSE,result); }
LONG kaspace_member_inherit_general(const KASPACE_MEMBER *source,UQUAD number,void *identity,
                                   KASPACE_MEMBER **result)
{ return member_inherit(source,number,identity,TRUE,result); }

KASPACE_SCOPE *kaspace_member_scope(const KASPACE_MEMBER *member)
{ return member && member->identity && member->number ? member->scope : NULL; }

KASPACE_SCOPE *kaspace_member_runtime(const KASPACE_MEMBER *member)
{
    for (;member;member=member->owner_next)
        if (member->scope->runtime) return member->scope;
    return NULL;
}
BOOL kaspace_member_selects(const KASPACE_MEMBER *member,const KASPACE_SCOPE *scope,BOOL runtime)
{
    for (;member;member=member->owner_next)
        if (member->scope==scope) return member->scope->runtime==runtime;
    return FALSE;
}

BOOL kaspace_member_matches(const KASPACE_MEMBER *member,const KASPACE *space,
                            UQUAD number,const void *identity)
{
    const KASPACE_MEMBER *at;
    if (!member || !space || !number || !identity) return FALSE;
    for (at=member;at;at=at->owner_next)
        if (at->scope->space!=space || at->number!=number || at->identity!=identity ||
            !at->scope->members) return FALSE;
    return TRUE;
}

void kaspace_member_claim_private(KASPACE_MEMBER *member)
{
    KASPACE *space;
    KASPACE_SCOPE *general;
#if CONF_WITH_MMAP
    KASPACE_SCOPE *runtime;
#endif
    ULONG i;
    if (!member || member->next || member->scope->runtime) panic("invalid private mapping owner");
    space=member->scope->space;
    general=member->scope;
#if CONF_WITH_MMAP
    runtime=kaspace_member_runtime(member);
#endif
    for (i=0;i<space->heap.count;i++) {
        if (space->heap.blocks[i].scope) panic("private clone inherited mapping owner");
        space->heap.blocks[i].scope=general;
    }
#if CONF_WITH_MMAP
    {
        MMAP_REGION *region;
        for (region=space->mmap.regions;region;region=region->next) {
            if (region->scope) panic("private clone inherited mmap owner");
            if (region->runtime_owned && !runtime) panic("private runtime mapping has no owner");
            region->scope=region->runtime_owned?runtime:general;
        }
    }
#endif
}

void kaspace_member_release(KASPACE_MEMBER **members)
{
    KASPACE_MEMBER *member,*next,**at;
    if (!members) return;
    member=*members;*members=NULL; /* close owner admission before callbacks */
    while (member) {
        KASPACE *space=member->scope->space;
        next=member->owner_next;
        for (at=&member->scope->members;*at && *at!=member;at=&(*at)->next) { }
        if (!*at) panic("mapping membership detached twice");
        *at=member->next;
        member->identity=NULL;member->number=0;
        kfree(member);
        reap_retired(space); /* detached member still owns this root reference */
        kaspace_put(space);
        member=next;
    }
}

#if CONF_WITH_X86_64_MEMTEST
static UWORD fail_lease_after;
#endif

BOOL kaspace_lease_valid(const KASPACE_LEASE *lease)
{
    VADDR page;
    if (!lease || lease->revoked || lease->attachment_ticket) return FALSE;
    if (lease->reservation_ticket) {
        if (lease->captured) return lease->held;
        return (lease->reservation_rights&lease->access)==lease->access &&
               reservation_range_matches(lease->space,lease->reservation_id,lease->exact_address,
                                          lease->exact_bytes,lease->reservation_rights);
    }
    for (page = lease->first; ; page += USER_PAGE_SIZE) {
        UWORD prot;
        if (!
#if CONF_WITH_PRIVATE_CLONE
            aspace_logical_protection(lease->space->backend, page, &prot)
#else
            aspace_translate(lease->space->backend, page, NULL, &prot)
#endif
            ||
            (prot & lease->access) != lease->access) return FALSE;
        if (page == lease->last) return TRUE;
    }
}

LONG kaspace_lease_acquire(KASPACE *space, VADDR address, VADDR bytes,
                          UWORD access, KASPACE_LEASE **result)
{
    KASPACE_LEASE *lease;
    if (!result || *result || !space || !bytes || address >= USER_VA_LIMIT ||
        bytes > USER_VA_LIMIT - address || !(access & ASPACE_PROT_USER) ||
        (access & ~(ASPACE_PROT_USER | ASPACE_PROT_WRITE | ASPACE_PROT_EXEC))) return EINVAL;
    if (pending_overlap(space,address,bytes)) return EACCDN;
    if (space->mutating || space->reaping) return EACCDN;
#if CONF_WITH_X86_64_MEMTEST
    if (fail_lease_after && !--fail_lease_after) return ENSMEM;
#endif
    if (lease_ids == ~(UQUAD)0) return ENSMEM;
    lease = kalloc(sizeof(*lease));
    if (!lease) return ENSMEM;
    lease->reservation_ticket = lease->reservation_id = lease->owner_number = 0;
    lease->attachment_ticket=0;
    lease->owner_identity = NULL; lease->exact_address = lease->exact_bytes = 0;
    lease->reservation_rights = lease->reservation_flags = 0; lease->captured = FALSE;
    lease->retire_address=lease->retire_bytes=0; lease->retire_private=lease->retire_running=lease->retire_committed=FALSE;
    lease->retire_heap_birth=0;
    lease->space = space;
    lease->first = address & ~(VADDR)(USER_PAGE_SIZE - 1);
    lease->last = (address + bytes - 1) & ~(VADDR)(USER_PAGE_SIZE - 1);
    lease->access = access;
    lease->revoked = lease->sealed = lease->held = lease->stack = FALSE;
    if (!kaspace_lease_valid(lease)) { kfree(lease); return EIMBA; }
    if (!kaspace_share(space)) { kfree(lease); return ENSMEM; }
    lease->identity=++lease_ids;
    lease->next = space->leases;
    space->leases = lease;
    *result = lease;
    return E_OK;
}

BOOL kaspace_lease_hold_private(KASPACE_LEASE *lease)
{
    if (!kaspace_lease_valid(lease) || lease->held ||
        !aspace_private_range_ok(lease->space->backend, lease->first,
                lease->last - lease->first + USER_PAGE_SIZE)) return FALSE;
    lease->held = TRUE;
    return TRUE;
}

LONG kaspace_stack_hold_acquire(KASPACE *space, VADDR address, VADDR bytes, KASPACE_LEASE **result)
{
    KASPACE_LEASE *lease = NULL;
    VADDR page, last;
    LONG error;
    if (!result || *result || !space || !bytes || address >= USER_VA_LIMIT ||
        bytes > USER_VA_LIMIT - address) return EINVAL;
    last = (address + bytes - 1) & ~(VADDR)(USER_PAGE_SIZE - 1);
    for (page = address & ~(VADDR)(USER_PAGE_SIZE - 1); ; page += USER_PAGE_SIZE) {
        BOOL reserved = kaspace_heap_contains(space, page);
#if CONF_WITH_MMAP
        MMAP_REGION *region;
        for (region = space->mmap.regions; !reserved && region; region = region->next)
            if (mapping_live(space,region->scope,region->reservation,region->address,region->length) &&
                !region->backing && page >= region->address &&
                page < (UQUAD)region->address + region->length &&
                (region->prot & (ASPACE_PROT_USER | ASPACE_PROT_WRITE)) ==
                    (ASPACE_PROT_USER | ASPACE_PROT_WRITE)) reserved = TRUE;
#endif
        if (!reserved) return EACCDN;
        if (page == last) break;
    }
    error = kaspace_lease_acquire(space, address, bytes, ASPACE_PROT_USER | ASPACE_PROT_WRITE, &lease);
    if (error) return error;
    if (!kaspace_lease_hold_private(lease)) { kaspace_lease_release(&lease); return EACCDN; }
    lease->stack = TRUE;
    *result = lease;
    return E_OK;
}

KASPACE *kaspace_stack_hold_space(const KASPACE_LEASE *hold)
{
    return hold && hold->stack && hold->held && kaspace_lease_valid(hold) ? hold->space : NULL;
}

BOOL kaspace_lease_seal(KASPACE_LEASE *const *leases, UWORD count)
{
    UWORD i;
    if (!leases || !count) return FALSE;
    for (i = 0; i < count; i++) if (!kaspace_lease_valid(leases[i])) return FALSE;
    for (i = 0; i < count; i++) leases[i]->sealed = TRUE;
    return TRUE;
}

static void reap_retired(KASPACE *space);

void kaspace_lease_release(KASPACE_LEASE **handle)
{
    KASPACE_LEASE *lease, **link;
    KASPACE *space;
    if (!handle || !*handle) return;
    lease = *handle; *handle = NULL; space = lease->space;
    for (link = &space->leases; *link != lease; link = &(*link)->next) { }
    *link = lease->next;
    kfree(lease);
    reap_retired(space); /* this detached lease still owns its root reference */
    kaspace_put(space);
}

BOOL kaspace_next_held(const KASPACE *space,VADDR from,VADDR to,VADDR *first,VADDR *end)
{
    const KASPACE_LEASE *lease;
    VADDR begin=to,limit=from;
    if (!space || !first || !end || from>=to || to>USER_VA_LIMIT) return FALSE;
    for (lease=space->leases;lease;lease=lease->next) {
        VADDR lo,hi;
        if (!lease->held && !lease->sealed) continue;
        lo=lease->first>from?lease->first:from;
        hi=lease->last+USER_PAGE_SIZE<to?lease->last+USER_PAGE_SIZE:to;
        if (lo>=hi) continue;
        if (lo<begin || (lo==begin && hi>limit)) { begin=lo;limit=hi; }
    }
    if (begin>=limit) return FALSE;
    *first=begin;*end=limit;return TRUE;
}

BOOL kaspace_mapping_can_change(const ASPACE *backend, VADDR address, VADDR bytes)
{
    KASPACE *space;
    KASPACE_LEASE *lease;
    VADDR first, last;
    if (!bytes || address >= USER_VA_LIMIT || bytes > USER_VA_LIMIT - address) return FALSE;
    first = address & ~(VADDR)(USER_PAGE_SIZE - 1);
    last = (address + bytes - 1) & ~(VADDR)(USER_PAGE_SIZE - 1);
    for (space = spaces; space && space->backend != backend; space = space->next) { }
    if (!space) return TRUE; /* backend not yet adopted */
    if (pending_overlap(space,first,last-first+USER_PAGE_SIZE)) return FALSE;
    for (lease = space->leases; lease; lease = lease->next)
        if ((lease->sealed || lease->held) && first <= lease->last && lease->first <= last) return FALSE;
    return TRUE;
}
BOOL kaspace_mapping_change(ASPACE *backend, VADDR address, VADDR bytes)
{
    KASPACE *space;
    KASPACE_LEASE *lease;
    VADDR first, last;
    if (!kaspace_mapping_can_change(backend,address,bytes)) return FALSE;
    first = address & ~(VADDR)(USER_PAGE_SIZE - 1);
    last = (address + bytes - 1) & ~(VADDR)(USER_PAGE_SIZE - 1);
    for (space = spaces; space && space->backend != backend; space = space->next) { }
    if (!space) return TRUE; /* backend not yet adopted */
    for (lease = space->leases; lease; lease = lease->next)
        if (!lease->attachment_ticket && first <= lease->last && lease->first <= last) lease->revoked = TRUE;
    return TRUE;
}
/* Retirement may widen a destruction fence, never an owner's backing
 * authority. Page materialization uses the immutable exact hold, rounded only
 * to the hardware page granularity. A consumed ticket is cleanup ownership. */
static BOOL reservation_backing_contains(const KASPACE_LEASE *lease,
                                         VADDR first, VADDR last)
{
    return lease->reservation_ticket && lease->captured && lease->held &&
           !lease->revoked && lease->exact_bytes &&
           first >= (lease->exact_address & ~(VADDR)(USER_PAGE_SIZE-1)) &&
           last <= ((lease->exact_address+lease->exact_bytes-1) &
                    ~(VADDR)(USER_PAGE_SIZE-1));
}

static BOOL independent_backing_owner(KASPACE *space, VADDR address,
                                     VADDR bytes, UWORD rights)
{
    KASPACE_RESERVATION_INFO info;
    KASPACE_LEASE *lease;
    VADDR first=address&~(VADDR)(USER_PAGE_SIZE-1);
    VADDR last=(address+bytes-1)&~(VADDR)(USER_PAGE_SIZE-1);
    /* Live ordinary metadata owns its backing, including FILE_BACKING for
     * a file mapping. Captured fallback is checked by exact lazy ticket below;
     * an immutable captured description alone cannot retain a file backend. */
    if (kaspace_reservation_info(space,address,bytes,&info) &&
        !(info.flags&KASPACE_RESERVATION_CAPTURED) && rights==info.rights) return TRUE;
    for (lease=space->leases;lease;lease=lease->next)
        if (reservation_backing_contains(lease,first,last) &&
            (lease->reservation_flags&KASPACE_RESERVATION_LAZY) &&
            rights==lease->reservation_rights) return TRUE;
        else if (lease->reservation_ticket && !lease->captured && lease->held &&
                 !lease->revoked && lease->exact_bytes &&
                 first>=(lease->exact_address&~(VADDR)(USER_PAGE_SIZE-1)) &&
                 last<=((lease->exact_address+lease->exact_bytes-1)&~(VADDR)(USER_PAGE_SIZE-1)) &&
                 rights==lease->reservation_rights &&
                 reservation_range_matches(space,lease->reservation_id,address,bytes,rights)) return TRUE;
    return FALSE;
}

/* Pending canonical metadata retains real backing, but is not fresh ownership
 * or broad fault authority. Only existing exact durable views can use it. */
static BOOL pending_backing_allowed(KASPACE *space,VADDR address,VADDR bytes,UWORD rights)
{
    KASPACE_RESERVATION_INFO pending,live;
    KASPACE_LEASE *lease;
    VADDR first=address&~(VADDR)(USER_PAGE_SIZE-1);
    VADDR last=(address+bytes-1)&~(VADDR)(USER_PAGE_SIZE-1);
    VADDR page;
    if (!pending_overlap(space,first,last-first+USER_PAGE_SIZE)) return TRUE;
    for (page=first;;page+=USER_PAGE_SIZE) {
        BOOL allowed=FALSE;
        if (!reservation_lookup(space,page,USER_PAGE_SIZE,&pending,TRUE) ||
            (kaspace_reservation_info(space,page,USER_PAGE_SIZE,&live) &&
             !(live.flags&KASPACE_RESERVATION_CAPTURED))) allowed=TRUE;
        else for (lease=space->leases;lease;lease=lease->next) {
            if (lease->reservation_ticket && lease->reservation_id==pending.identity &&
                lease->held && !lease->revoked && rights==pending.rights && rights==lease->reservation_rights &&
                page>=(lease->exact_address&~(VADDR)(USER_PAGE_SIZE-1)) &&
                page<=((lease->exact_address+lease->exact_bytes-1)&~(VADDR)(USER_PAGE_SIZE-1)) &&
                (!lease->captured || ((pending.flags&KASPACE_RESERVATION_LAZY) &&
                                      (lease->reservation_flags&KASPACE_RESERVATION_LAZY)))) { allowed=TRUE;break; }
            if (lease->stack && lease->held && !lease->revoked &&
                page>=lease->first && page<=lease->last &&
                rights==(ASPACE_PROT_USER|ASPACE_PROT_WRITE) &&
                (pending.flags&KASPACE_RESERVATION_LAZY)) { allowed=TRUE;break; }
        }
        if (!allowed) return FALSE;
        if (page==last) return TRUE;
    }
}

BOOL kaspace_materialization_change(ASPACE *backend, VADDR address,
                                   VADDR bytes, UWORD rights)
{
    KASPACE *space;
    KASPACE_LEASE *lease;
    VADDR first, last;
    if (!bytes || address >= USER_VA_LIMIT || bytes > USER_VA_LIMIT-address) return FALSE;
    first = address & ~(VADDR)(USER_PAGE_SIZE-1);
    last = (address+bytes-1) & ~(VADDR)(USER_PAGE_SIZE-1);
    for (space=spaces;space && space->backend!=backend;space=space->next) { }
    if (!space) return TRUE;
    if (!pending_backing_allowed(space,address,bytes,rights)) return FALSE;
    for (lease=space->leases;lease;lease=lease->next) {
        if (lease->attachment_ticket) continue;
        if (first > lease->last || lease->first > last) continue;
        if (lease->retire_bytes &&
            !reservation_backing_contains(lease,first,last)) {
            /* This overlap is only a cleanup fence. It supplies no rights,
             * and must not veto a different, independently retained owner. */
            if (!independent_backing_owner(space,address,bytes,rights)) return FALSE;
            continue;
        }
        if (lease->reservation_ticket) {
            if (!kaspace_lease_valid(lease) ||
                (lease->reservation_flags&KASPACE_RESERVATION_INACCESSIBLE)) return FALSE;
            if (lease->captured) {
                /* Different authenticated owner views can cover the same
                 * stack page. Validate each immutable witness independently. */
                if (!(lease->reservation_flags&KASPACE_RESERVATION_LAZY) ||
                    rights!=lease->reservation_rights) return FALSE;
            } else if (!reservation_range_matches(space,lease->reservation_id,address,bytes,rights)) return FALSE;
        } else if (lease->sealed || lease->held) return FALSE;
    }
    /* Physical translations can change; unsealed alias snapshots are revoked.
     * Reservation holds retain logical birth/rights, never a physical alias. */
    for (lease=space->leases;lease;lease=lease->next)
        if (!lease->attachment_ticket && !lease->reservation_ticket && !lease->retire_bytes &&
            first <= lease->last && lease->first <= last)
            lease->revoked=TRUE;
    return TRUE;
}

#if CONF_WITH_PRIVATE_CLONE
BOOL kaspace_cow_change(ASPACE *backend, VADDR address, VADDR bytes)
{
    KASPACE *space;
    KASPACE_LEASE *lease;
    VADDR first, last;
    if (!bytes || address >= USER_VA_LIMIT || bytes > USER_VA_LIMIT - address) return FALSE;
    first = address & ~(VADDR)(USER_PAGE_SIZE - 1);
    last = (address + bytes - 1) & ~(VADDR)(USER_PAGE_SIZE - 1);
    for (space = spaces; space && space->backend != backend; space = space->next) { }
    if (!space) return TRUE;
    if (pending_overlap(space,first,last-first+USER_PAGE_SIZE)) {
        KASPACE_RESERVATION_INFO info;
        VADDR page;
        for (page=first;;page+=USER_PAGE_SIZE) {
            if (reservation_lookup(space,page,USER_PAGE_SIZE,&info,TRUE) &&
                !pending_backing_allowed(space,page,USER_PAGE_SIZE,info.rights)) return FALSE;
            if (page==last) break;
        }
    }
    for (lease = space->leases; lease; lease = lease->next)
        if (!lease->attachment_ticket && first <= lease->last && lease->first <= last && (lease->sealed || !lease->held))
            return FALSE;
    return TRUE;
}
#endif

static UQUAD reservation_ids, reservation_tickets;
UQUAD kaspace_reservation_next(void)
{
    if (reservation_ids == ~(UQUAD)0) return 0;
    return ++reservation_ids;
}

static BOOL scope_owned(KASPACE_SCOPE *scope,UQUAD number,void *identity)
{
    KASPACE_MEMBER *member;
    if (!scope) return TRUE; /* established unclassified/common root metadata */
    for (member=scope->members;member;member=member->next)
        if (member->number==number && member->identity==identity) return TRUE;
    return FALSE;
}

BOOL kaspace_mapping_owned(KASPACE *space,UQUAD number,void *identity,UQUAD birth,
                           VADDR address,VADDR bytes)
{
    VADDR at=address,end;
    if (!space || !number || !identity || !birth || !bytes ||
        address>=USER_VA_LIMIT || bytes>USER_VA_LIMIT-address) return FALSE;
    end=address+bytes;
    while (at<end) {
        KASPACE_RESERVATION_INFO info;
        KASPACE_SCOPE *scope=NULL;
        KASPACE_LEASE *lease;
        ULONG i;
        BOOL owned;
        if (!reservation_lookup(space,at,1,&info,FALSE) || info.identity!=birth ||
            (info.flags&KASPACE_RESERVATION_CAPTURED)) return FALSE;
        for (i=0;i<space->heap.count;i++)
            if (space->heap.blocks[i].reservation==birth && space->heap.blocks[i].va==info.address)
                { scope=space->heap.blocks[i].scope;break; }
#if CONF_WITH_MMAP
        if (i==space->heap.count) {
            MMAP_REGION *region;
            for (region=space->mmap.regions;region;region=region->next)
                if (region->reservation==birth && region->address==info.address) { scope=region->scope;break; }
        }
#endif
        owned=scope_owned(scope,number,identity);
        if (!owned) for (lease=space->leases;lease;lease=lease->next)
            if (lease->attachment_ticket && !lease->revoked && lease->reservation_id==birth &&
                lease->owner_number==number && lease->owner_identity==identity &&
                at>=lease->exact_address && at-lease->exact_address<lease->exact_bytes &&
                (end<info.address+info.bytes?end:info.address+info.bytes)-at<=
                    lease->exact_bytes-(at-lease->exact_address)) { owned=TRUE;break; }
        if (!owned) return FALSE;
        at=info.address+info.bytes;
    }
    return TRUE;
}

static KASPACE_LEASE *attachment_find(const KASPACE_ATTACHMENT *token,UQUAD number,void *identity)
{
    KASPACE *space;
    KASPACE_LEASE *lease;
    if (!token || !token->ticket || !number || !identity) return NULL;
    for (space=spaces;space;space=space->next)
        for (lease=space->leases;lease;lease=lease->next)
            if (lease->attachment_ticket==token->ticket && lease->owner_number==number &&
                lease->owner_identity==identity && !lease->revoked) return lease;
    return NULL;
}

#if CONF_WITH_MMAP
static MMAP_REGION *attachment_split_node(const MMAP_REGION *region)
{
    MMAP_REGION *node=mapping_alloc(sizeof(*node));
    if (!node) return NULL;
    if (region->backing && !fileback_try_hold(region->backing)) { kfree(node);return NULL; }
    *node=*region;
    return node;
}
static void attachment_split(MMAP_REGION *region,VADDR at,MMAP_REGION *tail)
{
    *tail=*region; /* its independent FILE_BACKING reference is pre-acquired */
    tail->offset+=(ULONG)(at-region->address);tail->address=(ULONG)at;
    tail->length=region->length-(ULONG)(at-region->address);
    region->length=(ULONG)(at-region->address);region->next=tail;
}
#endif

LONG kaspace_attachment_acquire(KASPACE *space,UQUAD birth,VADDR address,VADDR bytes,
        UQUAD source_number,void *source,UQUAD target_number,void *target,KASPACE_ATTACHMENT *result)
{
    KASPACE_LEASE *lease;
    ULONG i;
#if CONF_WITH_MMAP
    MMAP_REGION *first=NULL,*last=NULL,*left=NULL,*right=NULL;
#endif
    if (!result || result->ticket || !target_number || !target || !bytes ||
        (address&(USER_PAGE_SIZE-1)) || (bytes&(USER_PAGE_SIZE-1))) return EINVAL;
    if (!kaspace_mapping_owned(space,source_number,source,birth,address,bytes)) return EACCDN;
    if (space->mutating || space->reaping) return EACCDN;
    for (i=0;i<space->heap.count;i++)
        if (space->heap.blocks[i].reservation==birth) {
            if (space->heap.blocks[i].va!=address ||
                (VADDR)space->heap.blocks[i].pages*USER_PAGE_SIZE!=bytes) return EINVAL;
            break;
        }
    if (reservation_tickets==~(UQUAD)0) return ENSMEM;
#if CONF_WITH_X86_64_MEMTEST
    if (fail_lease_after && !--fail_lease_after) return ENSMEM;
#endif
    lease=mapping_alloc(sizeof(*lease));
    if (!lease) return ENSMEM;
#if CONF_WITH_MMAP
    if (i==space->heap.count) {
        MMAP_REGION *region;
        for (region=space->mmap.regions;region;region=region->next) {
            if (address>=region->address && address<(UQUAD)region->address+region->length) first=region;
            if (address+bytes>region->address && address+bytes<=(UQUAD)region->address+region->length) last=region;
        }
        if (!first || !last) { kfree(lease);return EACCDN; }
        if (address>first->address && !(left=attachment_split_node(first))) goto failed;
        if (address+bytes<(UQUAD)last->address+last->length && !(right=attachment_split_node(last))) goto failed;
    }
#endif
    if (!kaspace_share(space)) goto failed;
    memset(lease,0,sizeof(*lease));
    lease->space=space;lease->reservation_id=birth;
    lease->exact_address=address;lease->exact_bytes=bytes;
    lease->first=address;lease->last=address+bytes-USER_PAGE_SIZE;
    lease->owner_number=target_number;lease->owner_identity=target;
    lease->attachment_ticket=++reservation_tickets;
#if CONF_WITH_MMAP
    if (left) { attachment_split(first,address,left);if (last==first) last=left; }
    if (right) attachment_split(last,address+bytes,right);
#endif
    lease->next=space->leases;space->leases=lease;
    result->ticket=lease->attachment_ticket;
    return E_OK;
failed:
#if CONF_WITH_MMAP
    if (left) { fileback_put(left->backing);kfree(left); }
    if (right) { fileback_put(right->backing);kfree(right); }
#endif
    kfree(lease);
    return ENSMEM;
}

LONG kaspace_attachment_duplicate(const KASPACE_ATTACHMENT *source,UQUAD source_number,void *source_identity,
                                  UQUAD number,void *identity,KASPACE_ATTACHMENT *result)
{
    KASPACE_LEASE *lease=attachment_find(source,source_number,source_identity),*copy;
    KASPACE_RESERVATION_INFO info;
    VADDR at;
    if (!result || result->ticket || !number || !identity) return EINVAL;
    if (!lease) return EACCDN;
    /* Removed holes convey no authority. At least one surviving exact birth
     * is required; replacement births in the old interval are never adopted. */
    for (at=lease->exact_address;at<lease->exact_address+lease->exact_bytes;at+=USER_PAGE_SIZE)
        if (reservation_lookup(lease->space,at,1,&info,FALSE) && info.identity==lease->reservation_id) break;
    if (at==lease->exact_address+lease->exact_bytes) return EACCDN;
    if (reservation_tickets==~(UQUAD)0) return ENSMEM;
    copy=mapping_alloc(sizeof(*copy));
    if (!copy) return ENSMEM;
    if (!kaspace_share(lease->space)) { kfree(copy);return ENSMEM; }
    *copy=*lease;copy->attachment_ticket=++reservation_tickets;
    copy->owner_number=number;copy->owner_identity=identity;
    copy->next=lease->space->leases;lease->space->leases=copy;
    result->ticket=copy->attachment_ticket;
    return E_OK;
}

BOOL kaspace_attachment_release(KASPACE_ATTACHMENT *token,UQUAD number,void *identity)
{
    KASPACE_LEASE *lease=attachment_find(token,number,identity),**at;
    KASPACE *space;
    if (!lease) return FALSE;
    space=lease->space;token->ticket=0;
    for (at=&space->leases;*at!=lease;at=&(*at)->next) { }
    *at=lease->next;lease->attachment_ticket=0;lease->owner_identity=NULL;
    kfree(lease);
#if CONF_WITH_MMAP
    kaspace_mmap_coalesce(space);
#endif
    reap_retired(space);kaspace_put(space);
    return TRUE;
}

void kaspace_attachment_release_owner(KASPACE *space,UQUAD number,void *identity)
{
    KASPACE_LEASE **at,*lease;
    ULONG roots=0;
    if (!space) return;
    for (at=&space->leases;(lease=*at);) {
        if (lease->attachment_ticket && lease->owner_number==number && lease->owner_identity==identity) {
            *at=lease->next;lease->attachment_ticket=0;lease->owner_identity=NULL;kfree(lease);roots++;
        } else at=&lease->next;
    }
#if CONF_WITH_MMAP
    kaspace_mmap_coalesce(space);
#endif
    reap_retired(space);
    while (roots) { roots--;kaspace_put(space); }
}

LONG kaspace_attachment_inherit(KASPACE *space,UQUAD source_number,void *source,UQUAD number,void *identity)
{
    KASPACE_LEASE *lease;
    LONG error;
    if (!space || !number || !identity || source_number==number || source==identity) return EINVAL;
    for (lease=space->leases;lease;lease=lease->next)
        if (lease->attachment_ticket && lease->owner_number==source_number && lease->owner_identity==source) {
            KASPACE_ATTACHMENT from,to={0};
            from.ticket=lease->attachment_ticket;
            error=kaspace_attachment_duplicate(&from,source_number,source,number,identity,&to);
            if (error==EACCDN) continue; /* all old bytes explicitly unmapped */
            if (error) { kaspace_attachment_release_owner(space,number,identity);return error; }
        }
    return E_OK;
}

BOOL kaspace_mapping_same_owners(KASPACE *space,UQUAD birth,VADDR a,VADDR an,VADDR b,VADDR bn)
{
    KASPACE_LEASE *lease;
    for (lease=space->leases;lease;lease=lease->next)
        if (lease->attachment_ticket && lease->reservation_id==birth) {
            BOOL first=a>=lease->exact_address && a+an<=lease->exact_address+lease->exact_bytes;
            BOOL second=b>=lease->exact_address && b+bn<=lease->exact_address+lease->exact_bytes;
            if (first!=second) return FALSE;
        }
    return TRUE;
}

#if CONF_WITH_MMAP
void kaspace_mmap_coalesce(KASPACE *space)
{
    MMAP_REGION *region;
    if (!space || space->reaping || space->mutating) return;
    space->reaping=TRUE; /* backing release may drop the last conflicting hold */
    space->reap_actor=mapping_actor();
    for (region=space->mmap.regions;region && region->next;) {
        MMAP_REGION *next=region->next;
        if ((UQUAD)region->address+region->length==next->address &&
            region->reservation==next->reservation && region->scope==next->scope &&
            region->prot==next->prot && region->backing==next->backing &&
            (!region->backing || (UQUAD)region->offset+region->length==next->offset) &&
            kaspace_mapping_same_owners(space,region->reservation,region->address,region->length,
                                        next->address,next->length)) {
            region->length+=next->length;region->next=next->next;
            fileback_put(next->backing);kfree(next);
        } else region=next;
    }
    space->reaping=FALSE;
    space->reap_actor=0;
    reap_retired(space);
}
#endif

static BOOL reservation_lookup(KASPACE *space, VADDR address, VADDR bytes,
                               KASPACE_RESERVATION_INFO *result,BOOL pending)
{
    ULONG i;
    if (!space || !result || !bytes || address >= USER_VA_LIMIT ||
        bytes > USER_VA_LIMIT-address) return FALSE;
    for (i=0;i<space->heap.count;i++) {
        KASPACE_HEAP_BLOCK *block = &space->heap.blocks[i];
        VADDR length = (VADDR)block->pages*USER_PAGE_SIZE;
        if ((pending || mapping_live(space,block->scope,block->reservation,block->va,length)) &&
            block->reservation && address >= block->va && address-block->va < length &&
            bytes <= length-(address-block->va)) {
            result->address = block->va; result->bytes = length;
            result->identity = block->reservation;
            result->rights = (block->flags&KASPACE_HEAP_GUARD)?0:ASPACE_PROT_USER|ASPACE_PROT_WRITE;
            result->flags = (block->flags&KASPACE_HEAP_GUARD)?KASPACE_RESERVATION_INACCESSIBLE:KASPACE_RESERVATION_LAZY;
            return TRUE;
        }
    }
#if CONF_WITH_MMAP
    {
        MMAP_REGION *region;
        for (region=space->mmap.regions;region;region=region->next)
            if ((pending || mapping_live(space,region->scope,region->reservation,region->address,region->length)) &&
                region->reservation && address >= region->address &&
                address-region->address < region->length) {
                MMAP_REGION *last=region;
                VADDR end=(VADDR)region->address+region->length;
                while (end<address+bytes && last->next) {
                    MMAP_REGION *next=last->next;
                    if (next->address!=end || next->reservation!=region->reservation ||
                        next->prot!=region->prot || next->backing!=region->backing ||
                        (region->backing && (UQUAD)last->offset+last->length!=next->offset) ||
                        (!pending && !mapping_live(space,next->scope,next->reservation,next->address,next->length))) break;
                    end=(VADDR)next->address+next->length;last=next;
                }
                if (end<address+bytes) continue;
                result->address = region->address; result->bytes = end-region->address;
                result->identity = region->reservation; result->rights = region->prot;
                result->flags = region->backing ? 0 : KASPACE_RESERVATION_LAZY;
                if (!region->prot) result->flags |= KASPACE_RESERVATION_INACCESSIBLE;
                return TRUE;
            }
    }
#endif
    return FALSE;
}

/* Once initialization removes WRITE beneath retained read-only witnesses,
 * immutable captured ceilings alone cannot authenticate a new resident grant.
 * This never backs pages and does not change lazy/inaccessible ownership. */
static BOOL capture_current_rights(KASPACE *space,UQUAD birth,VADDR address,VADDR bytes,
                                   UWORD ceiling,UWORD *rights)
{
    VADDR page,last;UWORD current=ceiling;
    if(!space->resident_permissions_tightened || !ceiling) {*rights=ceiling;return TRUE;}
    {
        KASPACE_RESERVATION_INFO ordinary;
        /* File reservations own FILE_BACKING independently of residency;
         * their logical WRITE may be file-COW behind a read-only PTE. */
        if(reservation_lookup(space,address,bytes,&ordinary,FALSE)) {
            if(ordinary.identity!=birth) return FALSE;
            *rights=ceiling&ordinary.rights;return TRUE;
        }
    }
    last=(address+bytes-1)&~(VADDR)(USER_PAGE_SIZE-1);
    for(page=address&~(VADDR)(USER_PAGE_SIZE-1);;page+=USER_PAGE_SIZE) {
        UWORD protection;
        if(!
#if CONF_WITH_PRIVATE_CLONE
            aspace_logical_protection(space->backend,page,&protection)
#else
            aspace_translate(space->backend,page,NULL,&protection)
#endif
        ) return FALSE;
        current &= protection;
        if(page==last) break;
    }
    *rights=current;return TRUE;
}

#if CONF_WITH_SHARED_VM_PEXEC_TEST
/* Authenticated image RELRO only: private backend separately checks resident
 * frame ownership and preserves USER/EXEC. No callbacks/allocations/yields.
 * Every page needs a live nonlazy witness; cleanup fences grant no authority. */
BOOL kaspace_relro_change(ASPACE *backend,VADDR address,VADDR bytes)
{
    KASPACE *space;KASPACE_LEASE *lease;VADDR page,last,first;
    if(!bytes || address>=USER_VA_LIMIT || bytes>USER_VA_LIMIT-address ||
       (address&(USER_PAGE_SIZE-1)) || (bytes&(USER_PAGE_SIZE-1))) return FALSE;
    for(space=spaces;space && space->backend!=backend;space=space->next) { }
    if(!space) return FALSE;
    first=address;last=address+bytes-USER_PAGE_SIZE;
    for(page=first;;page+=USER_PAGE_SIZE) {
        BOOL witness=FALSE;
        for(lease=space->leases;lease;lease=lease->next) {
            if(page<lease->first || page>lease->last) continue;
            if(lease->sealed) return FALSE;
            if(!lease->held) continue; /* revocable snapshots retire below */
            if(lease->retire_bytes && !lease->reservation_ticket) continue;
            if(!lease->reservation_ticket || !lease->captured || lease->revoked ||
               (lease->reservation_flags&KASPACE_RESERVATION_LAZY) ||
               (lease->access&~ASPACE_PROT_USER)) return FALSE;
            if(reservation_backing_contains(lease,page,page)) witness=TRUE;
        }
        if(!witness) return FALSE;
        if(page==last) break;
    }
    for(lease=space->leases;lease;lease=lease->next)
        if(!lease->attachment_ticket && !lease->held && !lease->sealed &&
           first<=lease->last && lease->first<=last) lease->revoked=TRUE;
    /* Backend has completed its whole-range/frame preflight. The following
     * leaf/COW permission stores are infallible and cannot yield. */
    space->resident_permissions_tightened=TRUE;
    return TRUE;
}
#endif

BOOL kaspace_reservation_info(KASPACE *space, VADDR address, VADDR bytes,
                             KASPACE_RESERVATION_INFO *result)
{
    if (!space || !result || !bytes || address >= USER_VA_LIMIT ||
        bytes > USER_VA_LIMIT-address) return FALSE;
    if(reservation_lookup(space,address,bytes,result,FALSE)) return TRUE;
    {
        KASPACE_LEASE *lease;
        for (lease=space->leases;lease;lease=lease->next)
            if (lease->reservation_ticket && lease->captured && lease->held && !lease->revoked &&
                address >= lease->exact_address && address-lease->exact_address < lease->exact_bytes &&
                bytes <= lease->exact_bytes-(address-lease->exact_address)) {
                result->address = lease->exact_address;
                result->bytes = lease->exact_bytes;
                result->identity = lease->reservation_id;
                result->rights = lease->reservation_rights;
                if(!(lease->reservation_flags&KASPACE_RESERVATION_LAZY) && space->resident_permissions_tightened) {
                    if(!capture_current_rights(space,lease->reservation_id,address,bytes,result->rights,&result->rights)) return FALSE;
                    result->address=address;result->bytes=bytes; /* only this extent was revalidated */
                }
                result->flags = lease->reservation_flags|KASPACE_RESERVATION_CAPTURED;
                return TRUE;
            }
    }
    return FALSE;
}

static LONG reservation_install(KASPACE *space, const KASPACE_RESERVATION_INFO *info,
        VADDR address, VADDR bytes, UWORD rights, UQUAD owner_number,
        void *owner_identity, KASPACE_RESERVATION_HOLD *result)
{
    KASPACE_LEASE *lease;
    if (!result || result->ticket || !space || !info || !info->identity ||
        !owner_number || !owner_identity || !bytes ||
        info->address >= USER_VA_LIMIT || info->bytes > USER_VA_LIMIT-info->address ||
        address < info->address || address-info->address >= info->bytes ||
        bytes > info->bytes-(address-info->address) ||
        (!(rights & ASPACE_PROT_USER) && !(rights==0 && info->rights==0 &&
          (info->flags&KASPACE_RESERVATION_INACCESSIBLE))) ||
        (rights & ~(ASPACE_PROT_USER|ASPACE_PROT_WRITE|ASPACE_PROT_EXEC)) ||
        (info->flags & ~(KASPACE_RESERVATION_LAZY|KASPACE_RESERVATION_CAPTURED|KASPACE_RESERVATION_INACCESSIBLE))) return EINVAL;
    if ((info->rights & rights) != rights) return EACCDN;
    if((info->flags&KASPACE_RESERVATION_CAPTURED) && !(info->flags&KASPACE_RESERVATION_LAZY) && rights) {
        UWORD current;
        if(!capture_current_rights(space,info->identity,address,bytes,info->rights,&current) ||
           (current&rights)!=rights) return EACCDN;
    }
    if (space->mutating || space->reaping) return EACCDN;
    if (reservation_tickets == ~(UQUAD)0) return ENSMEM;
#if CONF_WITH_X86_64_MEMTEST
    if (fail_lease_after && !--fail_lease_after) return ENSMEM;
#endif
    if (lease_ids == ~(UQUAD)0) return ENSMEM;
    lease = kalloc(sizeof(*lease));
    if (!lease) return ENSMEM;
    if (!kaspace_share(space)) { kfree(lease); return ENSMEM; }
    memset(lease,0,sizeof(*lease));
    lease->space = space; lease->first = address & ~(VADDR)(USER_PAGE_SIZE-1);
    lease->last = (address+bytes-1) & ~(VADDR)(USER_PAGE_SIZE-1);
    lease->access = rights; lease->held = TRUE;
    lease->exact_address = address; lease->exact_bytes = bytes;
    lease->reservation_id = info->identity; lease->owner_number = owner_number;
    lease->reservation_rights = info->rights; lease->reservation_flags = info->flags;
    lease->captured = (info->flags & KASPACE_RESERVATION_CAPTURED) != 0;
    lease->owner_identity = owner_identity; lease->reservation_ticket = ++reservation_tickets;
    lease->identity=++lease_ids;
    lease->next = space->leases; space->leases = lease;
    result->ticket = lease->reservation_ticket;
    return E_OK;
}

LONG kaspace_reservation_hold(KASPACE *space, UQUAD identity,
        VADDR address, VADDR bytes, UWORD rights, UQUAD owner_number,
        void *owner_identity, KASPACE_RESERVATION_HOLD *result)
{
    KASPACE_RESERVATION_INFO info;
    if (!result || result->ticket || !identity || !owner_number || !owner_identity ||
        (rights & ~(ASPACE_PROT_USER|ASPACE_PROT_WRITE|ASPACE_PROT_EXEC))) return EINVAL;
    if (!kaspace_reservation_info(space,address,bytes,&info) || info.identity!=identity) return EACCDN;
    return reservation_install(space,&info,address,bytes,rights,owner_number,owner_identity,result);
}

LONG kaspace_reservation_hold_captured(KASPACE *space,
        const KASPACE_RESERVATION_INFO *description, VADDR address, VADDR bytes,
        UWORD rights, UQUAD owner_number, void *owner_identity, KASPACE_RESERVATION_HOLD *result)
{
    KASPACE_RESERVATION_INFO info;
    if (!description) return EINVAL;
    info = *description; info.flags |= KASPACE_RESERVATION_CAPTURED;
    return reservation_install(space,&info,address,bytes,rights,owner_number,owner_identity,result);
}

BOOL kaspace_retained_lazy_protection(KASPACE *space, VADDR address, UWORD *rights)
{
    KASPACE_LEASE *lease;
    if (!space || !rights) return FALSE;
    /* Page backing is deliberately separate from exact reservation authority. */
    for (lease=space->leases;lease;lease=lease->next)
        if (reservation_backing_contains(lease,address&~(VADDR)(USER_PAGE_SIZE-1),
                                        address&~(VADDR)(USER_PAGE_SIZE-1)) &&
            (lease->reservation_flags&KASPACE_RESERVATION_LAZY) &&
            !(lease->reservation_flags&KASPACE_RESERVATION_INACCESSIBLE)) {
            *rights=lease->reservation_rights; return TRUE;
        }
    return FALSE;
}

static KASPACE_LEASE *reservation_find(const KASPACE_RESERVATION_HOLD *hold)
{
    KASPACE *space;
    KASPACE_LEASE *lease;
    if (!hold || !hold->ticket) return NULL;
    for (space=spaces;space;space=space->next)
        for (lease=space->leases;lease;lease=lease->next)
            if (lease->reservation_ticket == hold->ticket) return lease;
    return NULL;
}

void *kaspace_reservation_owner(const KASPACE_RESERVATION_HOLD *hold)
{
    KASPACE_LEASE *lease = reservation_find(hold);
    return lease ? lease->owner_identity : NULL;
}

#if CONF_WITH_SHARED_VM_PEXEC_TEST
BOOL kaspace_reservation_matches(const KASPACE_RESERVATION_HOLD *hold,
        const KASPACE *space,UQUAD birth,VADDR address,VADDR bytes)
{
    KASPACE_LEASE *lease=reservation_find(hold);
    return lease && space && birth && bytes && lease->space==space &&
        lease->held && lease->captured && !lease->revoked &&
        !(lease->reservation_flags&KASPACE_RESERVATION_LAZY) &&
        lease->reservation_id==birth && lease->exact_address==address &&
        lease->exact_bytes==bytes;
}
#endif

LONG kaspace_reservation_duplicate(const KASPACE_RESERVATION_HOLD *source,
        void *owner_identity, KASPACE_RESERVATION_HOLD *result)
{
    KASPACE_LEASE *lease = reservation_find(source), *copy;
    if (!result || result->ticket) return EINVAL;
    if (!lease || !owner_identity || lease->owner_identity != owner_identity ||
        !kaspace_lease_valid(lease)) return EACCDN;
    if (reservation_tickets == ~(UQUAD)0) return ENSMEM;
#if CONF_WITH_X86_64_MEMTEST
    if (fail_lease_after && !--fail_lease_after) return ENSMEM;
#endif
    if (lease_ids == ~(UQUAD)0) return ENSMEM;
    copy = kalloc(sizeof(*copy));
    if (!copy) return ENSMEM;
    if (!kaspace_share(lease->space)) { kfree(copy); return ENSMEM; }
    *copy = *lease; copy->identity=++lease_ids; copy->reservation_ticket = ++reservation_tickets;
    copy->retire_address=copy->retire_bytes=0; copy->retire_private=copy->retire_running=copy->retire_committed=FALSE;
    copy->retire_heap_birth=0;
    copy->first=copy->exact_address&~(VADDR)(USER_PAGE_SIZE-1);
    copy->last=(copy->exact_address+copy->exact_bytes-1)&~(VADDR)(USER_PAGE_SIZE-1);
    copy->next = lease->space->leases; lease->space->leases = copy;
    result->ticket = copy->reservation_ticket;
    return E_OK;
}

/* Consume a stopped owner's range without allocating a second registry.
 * The existing overlapping reservation lease becomes the no-fail cleanup owner.
 * Expanding its mutation barrier never expands its exact authenticated bounds. */
static BOOL retire_range(KASPACE *space, VADDR address, VADDR bytes, BOOL private,
                         UQUAD heap_birth)
{
    KASPACE_LEASE *lease;
    VADDR first,last;
    if (!space || !bytes || (address&(USER_PAGE_SIZE-1)) ||
        (bytes&(USER_PAGE_SIZE-1)) || address>=USER_VA_LIMIT || bytes>USER_VA_LIMIT-address) return FALSE;
    if (kaspace_mapping_can_change(space->backend,address,bytes)) {
        if (private) return aspace_release_lazy(space->backend,address,bytes);
        aspace_unborrow(space->backend,address,bytes); return TRUE;
    }
    first=address; last=address+bytes-USER_PAGE_SIZE;
    for (lease=space->leases;lease;lease=lease->next)
        if (lease->reservation_ticket && lease->held && !lease->revoked &&
            first<=lease->last && lease->first<=last && !lease->retire_bytes) {
            lease->retire_address=address; lease->retire_bytes=bytes;
            lease->retire_private=private;
            lease->retire_heap_birth=heap_birth;
            if (first<lease->first) lease->first=first;
            if (last>lease->last) lease->last=last;
            return TRUE;
        }
    return FALSE;
}
BOOL kaspace_retire_private(KASPACE *space, VADDR address, VADDR bytes)
{ return retire_range(space,address,bytes,TRUE,0); }

/* A shared stack is an ordinary reservation. Descendants must still be able
 * to duplicate that exact birth while its backing is held after owner exit.
 * The existing final cleanup fence also owns removal of its canonical record. */
static void remove_retired_heap(KASPACE *space,VADDR address,VADDR bytes,UQUAD birth)
{
    ULONG i;
    for(i=0;i<space->heap.count;i++) {
        KASPACE_HEAP_BLOCK *block=&space->heap.blocks[i];
        if(block->va==address && block->reservation==birth &&
           (VADDR)block->pages*USER_PAGE_SIZE==bytes &&
           (block->flags&KASPACE_HEAP_RETIRED)) {
            memmove(block,block+1,(--space->heap.count-i)*sizeof(*block));
            return;
        }
    }
    panic("retired heap lost its exact reservation birth");
}
BOOL kaspace_retire_heap(KASPACE *space,VADDR address,UQUAD birth)
{
    ULONG i;VADDR bytes;BOOL immediate;
    if(!space || !birth) return FALSE;
    for(i=0;i<space->heap.count;i++) {
        KASPACE_HEAP_BLOCK *block=&space->heap.blocks[i];
        if(block->va!=address || block->reservation!=birth) continue;
        if(block->flags&KASPACE_HEAP_RETIRED) return FALSE;
        bytes=(VADDR)block->pages*USER_PAGE_SIZE;
        immediate=kaspace_mapping_can_change(space->backend,address,bytes);
        block->flags|=KASPACE_HEAP_RETIRED;
        if(!retire_range(space,address,bytes,TRUE,birth)) {
            block->flags&=~KASPACE_HEAP_RETIRED;
            return FALSE;
        }
        if(immediate) remove_retired_heap(space,address,bytes,birth);
        return TRUE;
    }
    return FALSE;
}
BOOL kaspace_retire_borrow(KASPACE *space, VADDR address, VADDR bytes)
{
    VADDR first=address&~(VADDR)(USER_PAGE_SIZE-1);
    VADDR end;
    if (!bytes || address>=USER_VA_LIMIT || bytes>USER_VA_LIMIT-address) return FALSE;
    end=(address+bytes+USER_PAGE_SIZE-1)&~(VADDR)(USER_PAGE_SIZE-1);
    /* Borrow pins are keyed by their ORIGINAL, possibly short byte extent. */
    if (kaspace_mapping_can_change(space->backend,first,end-first)) {
        aspace_unborrow(space->backend,address,bytes); return TRUE;
    }
    {
        KASPACE_LEASE *lease,*candidate=NULL;
        for (lease=space->leases;lease;lease=lease->next) {
            /* One held view can outlive arbitrarily many overlapping exact
             * borrow pins. Keep their ownership in the existing backend pin
             * records, and coalesce ONLY the cleanup fence, not pin extents. */
            if (lease->retire_bytes && !lease->retire_private && !lease->retire_running &&
                first<=lease->last && lease->first<end) { candidate=lease;break; }
            if (!candidate && lease->reservation_ticket && lease->held && !lease->revoked &&
                first<=lease->last && lease->first<end && !lease->retire_bytes) candidate=lease;
        }
        if (candidate) {
            if (!aspace_defer_unborrow(space->backend,address,bytes)) return FALSE;
            if (candidate->retire_bytes) {
                VADDR old_end=candidate->retire_address+candidate->retire_bytes;
                if (old_end>end) end=old_end;
                if (candidate->retire_address<first) first=candidate->retire_address;
            }
            candidate->retire_address=first;candidate->retire_bytes=end-first;
            candidate->retire_private=FALSE;
            if (first<candidate->first) candidate->first=first;
            if (end-USER_PAGE_SIZE>candidate->last) candidate->last=end-USER_PAGE_SIZE;
            return TRUE;
        }
    }
    return FALSE;
}
static BOOL retirement_unblocked(KASPACE *space,VADDR address,VADDR bytes)
{
    KASPACE_LEASE *lease;
    VADDR first=address&~(VADDR)(USER_PAGE_SIZE-1);
    VADDR last=(address+bytes-1)&~(VADDR)(USER_PAGE_SIZE-1);
    for (lease=space->leases;lease;lease=lease->next)
        if (first<=lease->last && lease->first<=last && (lease->sealed || lease->held) &&
            !(lease->retire_bytes && !lease->reservation_ticket)) return FALSE;
    return TRUE;
}
BOOL kaspace_retirement_change(ASPACE *backend,VADDR address,VADDR bytes,BOOL private)
{
    KASPACE *space;
    KASPACE_LEASE *lease,*authority=NULL;
    VADDR first,last;
    if (!bytes || address>=USER_VA_LIMIT || bytes>USER_VA_LIMIT-address) return FALSE;
    for (space=spaces;space && space->backend!=backend;space=space->next) { }
    if (!space) return FALSE;
    if (private && space->retiring_birth && !space->retiring_committed &&
        address==space->retiring_address && bytes==space->retiring_bytes) {
        KASPACE_RESERVATION_INFO info;
        if (!reservation_lookup(space,address,bytes,&info,TRUE) ||
            info.identity!=space->retiring_birth || info.address!=address || info.bytes!=bytes ||
            !pending_overlap(space,address,bytes) || !retirement_unblocked(space,address,bytes)) return FALSE;
        space->retiring_committed=TRUE;
        first=address&~(VADDR)(USER_PAGE_SIZE-1);last=(address+bytes-1)&~(VADDR)(USER_PAGE_SIZE-1);
        for (lease=space->leases;lease;lease=lease->next)
            if (!lease->attachment_ticket && !lease->held && !lease->sealed && !lease->retire_bytes &&
                first<=lease->last && lease->first<=last) lease->revoked=TRUE;
        return TRUE;
    }
    for (lease=space->leases;lease;lease=lease->next)
        if (lease->retire_running && !lease->retire_committed && !lease->reservation_ticket && lease->retire_bytes==bytes &&
            lease->retire_address==address && lease->retire_private==private) { authority=lease; break; }
    if (!authority || !retirement_unblocked(space,address,bytes)) return FALSE;
    authority->retire_committed=TRUE; /* this exact backend commit cannot be replayed */
    first=address&~(VADDR)(USER_PAGE_SIZE-1); last=(address+bytes-1)&~(VADDR)(USER_PAGE_SIZE-1);
    for (lease=space->leases;lease;lease=lease->next)
        if (!lease->attachment_ticket && !lease->held && !lease->sealed && !lease->retire_bytes && first<=lease->last && lease->first<=last)
            lease->revoked=TRUE;
    return TRUE;
}
static void reap_retired(KASPACE *space)
{
    KASPACE_LEASE **at,*lease;
    ULONG roots=0;
    if (space->reaping || space->mutating) return;
    space->reaping=TRUE;
    space->reap_actor=mapping_actor();
    for (;;) {
        for (lease=space->leases;lease;lease=lease->next)
            if (lease->retire_bytes && !lease->reservation_ticket && !lease->retire_running &&
                retirement_unblocked(space,lease->retire_address,lease->retire_bytes)) break;
        if (!lease) {
            ULONG i;
            VADDR address=0,bytes=0;
            UQUAD birth=0;
            for (i=0;i<space->heap.count;i++) {
                KASPACE_HEAP_BLOCK *block=&space->heap.blocks[i];
                if (!mapping_live(space,block->scope,block->reservation,block->va,(VADDR)block->pages*USER_PAGE_SIZE) &&
                        retirement_unblocked(space,block->va,
                        (VADDR)block->pages*USER_PAGE_SIZE) &&
                        aspace_release_lazy_retired_allowed(space->backend,block->va,
                        (VADDR)block->pages*USER_PAGE_SIZE)) {
                    address=block->va;bytes=(VADDR)block->pages*USER_PAGE_SIZE;
                    birth=block->reservation;break;
                }
            }
#if CONF_WITH_MMAP
            if (!birth) {
                MMAP_REGION *region;
                for (region=space->mmap.regions;region;region=region->next)
                    if (!mapping_live(space,region->scope,region->reservation,region->address,region->length) &&
                        retirement_unblocked(space,region->address,region->length) &&
                        aspace_release_lazy_retired_allowed(space->backend,region->address,region->length)) {
                        address=region->address;bytes=region->length;birth=region->reservation;break;
                    }
            }
#endif
            if (!birth) break;
            space->retiring_birth=birth;space->retiring_address=address;space->retiring_bytes=bytes;
            space->retiring_committed=FALSE;
            if (!aspace_release_lazy_retired(space->backend,address,bytes) || !space->retiring_committed)
                panic("ordinary reservation retirement lost ownership");
            /* Backend callbacks may move the heap vector. Reauthenticate the
             * exact published job; no raw node pointer crosses that boundary. */
            for (i=0;i<space->heap.count;i++)
                if (space->heap.blocks[i].reservation==birth && space->heap.blocks[i].va==address &&
                    (VADDR)space->heap.blocks[i].pages*USER_PAGE_SIZE==bytes) break;
            if (i<space->heap.count) {
                memmove(&space->heap.blocks[i],&space->heap.blocks[i+1],
                        (space->heap.count-i-1)*sizeof(space->heap.blocks[0]));
                space->heap.count--;
            }
#if CONF_WITH_MMAP
            else {
                MMAP_REGION **link,*region;
                for (link=&space->mmap.regions;*link;link=&(*link)->next)
                    if ((*link)->reservation==birth && (*link)->address==address && (*link)->length==bytes) break;
                if (!*link) panic("ordinary cleanup metadata lost during reentry");
                region=*link;*link=region->next;
                space->mmap.bytes-=region->length;
                fileback_put(region->backing);kfree(region);
            }
#else
            else panic("ordinary cleanup metadata lost during reentry");
#endif
            space->retiring_birth=0;space->retiring_address=space->retiring_bytes=0;
            space->retiring_committed=FALSE;
            continue;
        }
        /* Consumed retirement fences do not deadlock one another. Keep ALL
         * fences published throughout backend cleanup/reentry; only this
         * exact in-flight job can authorize its no-yield leaf commit. */
        lease->retire_running=TRUE;
        if (lease->retire_private) {
            if (!aspace_release_lazy_retired(space->backend,lease->retire_address,lease->retire_bytes))
                panic("retired reservation lost private ownership");
        } else aspace_unborrow_retired(space->backend,lease->retire_address,lease->retire_bytes);
        if (!lease->retire_committed) panic("retirement backend skipped its ownership commit");
        if(lease->retire_heap_birth)
            remove_retired_heap(space,lease->retire_address,lease->retire_bytes,
                                lease->retire_heap_birth);
        for (at=&space->leases;*at && *at!=lease;at=&(*at)->next) { }
        if (!*at) panic("retirement cleanup owner lost during reentry");
        *at=lease->next; kfree(lease); roots++;
    }
    {
        KASPACE_SCOPE **link,*scope;
        for (link=&space->scopes;(scope=*link);) {
            ULONG i;
            BOOL used=scope->members!=NULL;
            for (i=0;!used && i<space->heap.count;i++) used=space->heap.blocks[i].scope==scope;
#if CONF_WITH_MMAP
            {
                MMAP_REGION *region;
                for (region=space->mmap.regions;!used && region;region=region->next) used=region->scope==scope;
            }
#endif
            if (used) link=&scope->next;
            else { *link=scope->next;kfree(scope); }
        }
    }
    space->reaping=FALSE;
    space->reap_actor=0;
    while (roots) { roots--; kaspace_put(space); }
}

BOOL kaspace_reservation_release(KASPACE_RESERVATION_HOLD *hold, void **owner_identity)
{
    KASPACE *space;
    KASPACE_LEASE **link, *lease;
    if (!hold || !hold->ticket || !owner_identity || *owner_identity) return FALSE;
    /* Authenticate against live registry/list membership before dereferencing
     * any retained storage; a copied spent ticket cannot touch recycled space. */
    for (space=spaces;space;space=space->next)
        for (link=&space->leases;*link;link=&(*link)->next)
            if ((*link)->reservation_ticket == hold->ticket) {
                lease = *link; hold->ticket = 0; *link = lease->next;
                *owner_identity = lease->owner_identity;
                if (lease->retire_bytes) {
                    lease->owner_identity=NULL; lease->owner_number=0; lease->reservation_ticket=0;
                    lease->next=space->leases; space->leases=lease;
                    reap_retired(space); /* this record keeps the root if still blocked */
                } else {
                    kfree(lease); reap_retired(space); kaspace_put(space);
                }
                return TRUE;
            }
    return FALSE;
}

#if CONF_WITH_X86_64_MEMTEST
static BOOL fail_next_adoption;
void kaspace_test_fail_lease_after(UWORD count) { fail_lease_after = count; }
void kaspace_test_fail_next(void) { fail_next_adoption = TRUE; }
#endif

#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
ULONG kaspace_test_live(void)
{
    const KASPACE *space;
    ULONG count = 0;
    for (space = spaces; space; space = space->next) count++;
    return count;
}
ULONG kaspace_test_references(void)
{
    const KASPACE *space;
    ULONG count = 0;
    for (space = spaces; space; space = space->next) count += space->refs;
    return count;
}
#endif

#if CONF_WITH_PRIVATE_CLONE_TEST
void kaspace_test_mapping_snapshot(const ASPACE *backend,uint32_t *values)
{
    KASPACE *space;
    for (space=spaces;space;space=space->next) {
        KASPACE_SCOPE *scope;
        KASPACE_LEASE *lease;
        ULONG i;
        values[MLT_ALL_PRIVATE]+=aspace_private_pages(space->backend);
        values[MLT_ALL_TABLES]+=aspace_table_pages(space->backend);
        values[MLT_ALL_PINS]+=aspace_test_pin_count(space->backend,FALSE);
        if (space->backend!=backend) continue;
        values[MLT_HEAP]=space->heap.count;values[MLT_HEAP_CAPACITY]=space->heap.capacity;
        values[MLT_ROOT_REFS]=space->refs;
        for (scope=space->scopes;scope;scope=scope->next) {
            KASPACE_MEMBER *member;
            values[MLT_SCOPES]++;
            for (member=scope->members;member;member=member->next) values[MLT_MEMBERS]++;
        }
        for (lease=space->leases;lease;lease=lease->next) {
            if (lease->attachment_ticket) values[MLT_ATTACHMENTS]++;
            if (lease->reservation_ticket) values[MLT_HOLDS]++;
            if (lease->stack) values[MLT_STACK_HOLDS]++;
            if (lease->retire_bytes) values[MLT_FENCES]++;
        }
        for (i=0;i<space->heap.count;i++) {
            KASPACE_HEAP_BLOCK *block=&space->heap.blocks[i];
            if (!mapping_live(space,block->scope,block->reservation,block->va,(VADDR)block->pages*USER_PAGE_SIZE))
                values[MLT_PENDING]++;
        }
#if CONF_WITH_MMAP
        {
            MMAP_REGION *region;
            values[MLT_BYTES]=(uint32_t)space->mmap.bytes;
            for (region=space->mmap.regions;region;region=region->next) {
                values[MLT_MMAP]++;
                if (!mapping_live(space,region->scope,region->reservation,region->address,region->length))
                    values[MLT_PENDING]++;
            }
        }
#endif
    }
}
#endif

KASPACE *kaspace_adopt(ASPACE *backend)
{
    KASPACE *space;
    if (!backend) return NULL;
#if CONF_WITH_X86_64_MEMTEST
    if (fail_next_adoption) { fail_next_adoption = FALSE; return NULL; }
#endif
    space = kalloc(sizeof(*space));
    if (!space) return NULL;
    space->backend = backend;
    space->refs = 1;
    space->resident_permissions_tightened=FALSE;
    space->leases = NULL;
    space->scopes=NULL;space->retiring_birth=0;
    space->retiring_address=space->retiring_bytes=0;
    space->retiring_committed=space->reaping=FALSE;
    space->mutating=FALSE;
    space->mutation_actor=space->reap_actor=0;
    space->next = spaces;
    spaces = space;
    space->heap.blocks = NULL;
    space->heap.count = space->heap.capacity = 0;
#if CONF_WITH_MMAP
    space->mmap.regions = NULL;
    space->mmap.bytes = 0;
#endif
    return space;
}

KASPACE *kaspace_share(KASPACE *space)
{
    if (!space || space->refs == (ULONG)0xffffffffUL) return NULL;
    space->refs++;
    return space;
}

ASPACE *kaspace_backend(const KASPACE *space)
{
    return space ? space->backend : NULL;
}

KASPACE_HEAP *kaspace_heap(KASPACE *space)
{
    return space ? &space->heap : NULL;
}

BOOL kaspace_heap_contains(KASPACE *space, UQUAD address)
{
    ULONG i;
    if (!space) return FALSE;
    for (i = 0; i < space->heap.count; i++) {
        const KASPACE_HEAP_BLOCK *b = &space->heap.blocks[i];
        if (mapping_live(space,b->scope,b->reservation,b->va,(VADDR)b->pages*USER_PAGE_SIZE) &&
            address >= b->va && address < (UQUAD)b->va + (UQUAD)b->pages * USER_PAGE_SIZE)
            return TRUE;
    }
    return FALSE;
}

BOOL kaspace_heap_protection(KASPACE *space,UQUAD address,UWORD *rights)
{
    ULONG i;
    if(!space || !rights) return FALSE;
    for(i=0;i<space->heap.count;i++) {
        KASPACE_HEAP_BLOCK *block=&space->heap.blocks[i];
        if(address>=block->va && address-block->va<(VADDR)block->pages*USER_PAGE_SIZE) {
            *rights=(block->flags&KASPACE_HEAP_GUARD)?0:ASPACE_PROT_USER|ASPACE_PROT_WRITE;return TRUE;
        }
    }
    return FALSE;
}

int kaspace_heap_demand_page(KASPACE *space, UQUAD address, UWORD access)
{
#if CONF_WITH_DEMAND_PAGING
    VADDR va;UWORD rights;
    if (!kaspace_heap_protection(space,address,&rights)) return -1;
    if(!rights || (access & ~rights)) return 0;
    va = (VADDR)address;
    if (!pending_backing_allowed(space,va&~(VADDR)(USER_PAGE_SIZE-1),USER_PAGE_SIZE,
                                ASPACE_PROT_USER|ASPACE_PROT_WRITE)) return 0;
#if CONF_WITH_PRIVATE_CLONE
    {
        int result = aspace_cow_fault(space->backend, va, access);
        if (result >= 0) return result;
    }
#endif
    if (aspace_translate(space->backend, va, NULL, NULL)) return 0;
    return aspace_fault_in(space->backend, va, ASPACE_PROT_USER | ASPACE_PROT_WRITE) ? 1 : 2;
#else
    UNUSED(space); UNUSED(address); UNUSED(access);
    return -1;
#endif
}

#if CONF_WITH_MMAP
KASPACE_MMAP *kaspace_mmap(KASPACE *space)
{
    return space ? &space->mmap : NULL;
}

static MMAP_REGION *mmap_find(KASPACE *space, UQUAD address)
{
    MMAP_REGION *region;
    if (!space) return NULL;
    for (region = space->mmap.regions; region; region = region->next)
        if (address >= region->address && address < (UQUAD)region->address + region->length)
            return region;
    return NULL;
}

BOOL kaspace_mmap_protection(KASPACE *space, UQUAD address, UWORD *protection)
{
    MMAP_REGION *region = mmap_find(space, address);
    if (!region || !mapping_live(space,region->scope,region->reservation,region->address,region->length)) return FALSE;
    if (protection) *protection = region->prot;
    return TRUE;
}

static int mmap_back(KASPACE *space, MMAP_REGION *region, VADDR va, UWORD access)
{
    if (!pending_backing_allowed(space,va&~(VADDR)(USER_PAGE_SIZE-1),USER_PAGE_SIZE,region->prot)) return 0;
    if (region->backing)
        return aspace_fault_file(space->backend, va, region->backing,
                region->offset + (ULONG)((va & ~(USER_PAGE_SIZE - 1)) - region->address),
                region->prot, (access & ASPACE_PROT_WRITE) != 0);
    return aspace_fault_in(space->backend, va, region->prot) ? 1 : 2;
}

int kaspace_mmap_fault(KASPACE *space, UQUAD address, UWORD access)
{
    MMAP_REGION *region = mmap_find(space, address);
    if (!region) return -1;
    if (!(region->prot & ASPACE_PROT_USER) || (access & ~region->prot)) return 0;
    return mmap_back(space, region, (VADDR)address, access);
}

int kaspace_mmap_demand_page(KASPACE *space, UQUAD address, UWORD access)
{
    MMAP_REGION *region = mmap_find(space, address);
    VADDR va;
    if (!region) return -1;
    if (!(region->prot & ASPACE_PROT_USER) || (access & ~region->prot)) return 0;
    va = (VADDR)address;
    if (!pending_backing_allowed(space,va&~(VADDR)(USER_PAGE_SIZE-1),USER_PAGE_SIZE,region->prot)) return 0;
#if CONF_WITH_PRIVATE_CLONE
    {
        int result = aspace_cow_fault(space->backend, va, access);
        if (result >= 0) return result;
    }
#endif
    if (aspace_translate(space->backend, va, NULL, NULL) &&
        !((access & ASPACE_PROT_WRITE) && aspace_file_cow(space->backend, va)))
        return 0;
    return mmap_back(space, region, va, access);
}

void kaspace_shutdown_file_mappings(void)
{
    KASPACE *space=spaces?kaspace_share(spaces):NULL;
    while (space) {
        KASPACE *next=space->next?kaspace_share(space->next):NULL;
        MMAP_REGION **link = &space->mmap.regions, *region;
        if (!kaspace_mutation_begin(space)) { kaspace_put(space);space=next;continue; }
        while ((region = *link)) {
            if (!region->backing ||
                !aspace_release_lazy(space->backend, region->address, region->length)) {
                link = &region->next;
                continue;
            }
            *link = region->next;
            space->mmap.bytes -= region->length;
            fileback_put(region->backing);
            kfree(region);
        }
        kaspace_mutation_end(space);
        kaspace_put(space);space=next;
    }
    fileback_reap();
}
#endif

void kaspace_put(KASPACE *space)
{
    if (!space) return;
    if (--space->refs) return;
    {
        KASPACE **link;
        for (link = &spaces; *link; link = &(*link)->next)
            if (*link == space) { *link = space->next; break; }
    }
    aspace_destroy(space->backend);
    kfree(space->heap.blocks);
#if CONF_WITH_MMAP
    while (space->mmap.regions) {
        MMAP_REGION *region = space->mmap.regions;
        space->mmap.regions = region->next;
        fileback_put(region->backing);
        kfree(region);
    }
#endif
    while (space->scopes) {
        KASPACE_SCOPE *scope=space->scopes;
        if (scope->members) panic("last root released with mapping members");
        space->scopes=scope->next;kfree(scope);
    }
    kfree(space);
}

LONG kaspace_clone(const KASPACE *source, KASPACE **result)
{
#if CONF_WITH_PRIVATE_CLONE
    KASPACE *copy;
    LONG error = ENSMEM;
    if (!source || !result || *result) return EINVAL;
    if (kaspace_mutation_busy(source)) return EACCDN;
    if (pending_overlap((KASPACE *)source,0,USER_VA_LIMIT)) return EACCDN;
    copy = kalloc(sizeof(*copy));
    if (!copy) return ENSMEM;
    memset(copy,0,sizeof(*copy));
    copy->refs = 1;
    if (source->heap.count) {
        if (source->heap.count > ~(ULONG)0 / sizeof(*copy->heap.blocks)) goto failed;
        copy->heap.blocks = kalloc(source->heap.count * sizeof(*copy->heap.blocks));
        if (!copy->heap.blocks) goto failed;
        memcpy(copy->heap.blocks, source->heap.blocks, source->heap.count * sizeof(*copy->heap.blocks));
        copy->heap.count = copy->heap.capacity = source->heap.count;
        {
            ULONG i;
            for (i=0;i<copy->heap.count;i++) {
                copy->heap.blocks[i].scope=NULL;
                if (!(copy->heap.blocks[i].reservation=kaspace_reservation_next())) goto failed;
            }
        }
    }
#if CONF_WITH_MMAP
    {
        MMAP_REGION *region, **tail = &copy->mmap.regions;
        for (region = source->mmap.regions; region; region = region->next) {
            MMAP_REGION *cloned = kalloc(sizeof(*cloned));
            if (!cloned) goto failed;
            *cloned = *region; cloned->next = NULL;
            cloned->scope=NULL;
            if (!(cloned->reservation=kaspace_reservation_next())) { kfree(cloned); goto failed; }
            if (cloned->backing && !fileback_try_hold(cloned->backing)) {
                kfree(cloned); error = EAGAIN; goto failed;
            }
            *tail = cloned; tail = &cloned->next;
        }
        copy->mmap.bytes = source->mmap.bytes;
    }
#endif
    /* All generic allocations precede backend source write-protection. */
    error = aspace_clone(source->backend, &copy->backend);
    if (error) goto failed;
    copy->next = spaces; spaces = copy;
    copy->resident_permissions_tightened=source->resident_permissions_tightened;
    *result = copy;
    return E_OK;
failed:
    kfree(copy->heap.blocks);
#if CONF_WITH_MMAP
    while (copy->mmap.regions) {
        MMAP_REGION *region = copy->mmap.regions;
        copy->mmap.regions = region->next;
        fileback_put(region->backing); kfree(region);
    }
#endif
    kfree(copy);
    return error;
#else
    (void)source;
    if (result) *result = NULL;
    return EINVFN;
#endif
}

#if CONF_WITH_NATIVE_SCHED
#include "kaspace_permission.h"
#endif

#if CONF_WITH_SHARED_VM_PEXEC_TEST && CONF_WITH_PRIVATE_CLONE_TEST
#include "shared_pexec_test.h"
void kaspace_shared_snapshot(const ASPACE *current,SHARED_PEXEC_SNAPSHOT *out)
{
    const KASPACE *space;
    for(space=spaces;space;space=space->next) {
        const KASPACE_LEASE *lease;ULONG i;
        out->count[SP_SPACES]++;out->count[SP_SPACE_REFS]+=space->refs;
        aspace_shared_snapshot(space->backend,out);
        for(lease=space->leases;lease;lease=lease->next) {
            out->count[SP_LEASES]++;
            if(lease->reservation_ticket) out->count[SP_RESERVATION_HOLDS]++;
            if(lease->retire_bytes) out->count[SP_FENCES]++;
        }
        out->count[SP_HEAP_RESERVATIONS]+=space->heap.count;
        for(i=0;i<space->heap.count;i++)
            if(space->heap.blocks[i].flags&KASPACE_HEAP_RETIRED) out->count[SP_RETIRED_HEAP]++;
        if(space->heap.blocks) out->count[SP_VECTOR_BLOCKS]++;
        out->count[SP_VECTOR_BYTES]+=(uint64_t)space->heap.capacity*sizeof(*space->heap.blocks);
#if CONF_WITH_MMAP
        {
            const MMAP_REGION *region;
            for(region=space->mmap.regions;region;region=region->next) {
                out->count[SP_MMAP_REGIONS]++;
                if(space->backend==current) out->count[SP_CURRENT_MMAP]++;
            }
        }
#else
        UNUSED(current);
#endif
    }
}
#endif
