/* Production reservation ledger/holds. Backend and file resources are models. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "../bdos/kaspace.c"
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"reservation hold:%d: %s\n",__LINE__,#c); abort(); } } while (0)
struct aspace { unsigned destroyed; };
static unsigned allocations, frees, fail_alloc, walks, backing_puts;
void *kalloc(ULONG bytes) { void *p; if(fail_alloc) return NULL; p=calloc(1,bytes); if(p) allocations++; return p; }
void kfree(void *p) { if(p) { frees++; free(p); } }
void aspace_destroy(ASPACE *as) { CHECK(!as->destroyed); as->destroyed++; }
BOOL aspace_translate(const ASPACE *as,VADDR va,PADDR *phys,UWORD *prot)
{ (void)as;(void)va;(void)phys;(void)prot;walks++;return FALSE; }
#if CONF_WITH_MMAP
void fileback_put(FILE_BACKING *backing) { CHECK(backing==(FILE_BACKING *)1);backing_puts++; }
#endif
int main(void)
{
    ASPACE backend={0};
    KASPACE *space=kaspace_adopt(&backend);
    KASPACE_RESERVATION_INFO info;
    KASPACE_RESERVATION_HOLD heap={0}, file={0}, bad={0}, stale;
    int canonical;
    void *owner=NULL;
    UQUAD birth;
    CHECK(space);
    space->heap.blocks=kalloc(sizeof(*space->heap.blocks));CHECK(space->heap.blocks);
    space->heap.count=space->heap.capacity=1;
    space->heap.blocks[0].va=0x4000;space->heap.blocks[0].pages=2;
    space->heap.blocks[0].reservation=birth=kaspace_reservation_next();
    CHECK(birth && kaspace_reservation_info(space,0x4001,8191,&info) && info.identity==birth);
    CHECK(kaspace_reservation_hold(space,birth+1,0x4000,8192,ASPACE_PROT_USER,7,&canonical,&bad)==EACCDN && !bad.ticket);
    CHECK(kaspace_reservation_hold(space,birth,0x4000,8192,ASPACE_PROT_USER|ASPACE_PROT_WRITE,7,&canonical,&heap)==E_OK);
    CHECK(!walks && space->refs==2); /* absent physical leaves were not consulted */
    stale=heap;
    {
        KASPACE_RESERVATION_INFO captured;
        KASPACE_RESERVATION_HOLD one={0}, two={0};
        UWORD rights=0;
        captured.address=0x20000; captured.bytes=4096;
        captured.identity=kaspace_reservation_next();
        captured.rights=ASPACE_PROT_USER|ASPACE_PROT_WRITE;
        captured.flags=KASPACE_RESERVATION_LAZY;
        CHECK(kaspace_reservation_hold_captured(space,&captured,0x20008,8,captured.rights,7,&canonical,&one)==E_OK);
        captured.identity=kaspace_reservation_next();
        CHECK(kaspace_reservation_hold_captured(space,&captured,0x20010,8,captured.rights,7,&canonical,&two)==E_OK);
        captured.identity=0; captured.rights=0; /* source descriptor is transient */
        CHECK(kaspace_retained_lazy_protection(space,0x20000,&rights) && rights==(ASPACE_PROT_USER|ASPACE_PROT_WRITE));
        CHECK(kaspace_materialization_change(&backend,0x20000,4096,rights));
        CHECK(!kaspace_mapping_can_change(&backend,0x20000,4096));
        CHECK(kaspace_reservation_release(&one,&owner));owner=NULL;
        CHECK(kaspace_reservation_release(&two,&owner));owner=NULL;
        CHECK(!kaspace_retained_lazy_protection(space,0x20000,&rights));
    }
    {
        KASPACE_RESERVATION_HOLD child = {0};
        CHECK(kaspace_reservation_duplicate(&heap,&canonical,&child)==E_OK && child.ticket!=heap.ticket);
        CHECK(kaspace_reservation_owner(&child)==&canonical);
        CHECK(kaspace_reservation_release(&child,&owner) && owner==&canonical);
        owner=NULL;
    }
    fail_alloc=1;
    CHECK(kaspace_reservation_hold(space,birth,0x4000,8192,ASPACE_PROT_USER,7,&canonical,&bad)==ENSMEM && !bad.ticket && space->refs==2);
    fail_alloc=0;space->refs=(ULONG)~0UL;
    CHECK(kaspace_reservation_hold(space,birth,0x4000,8192,ASPACE_PROT_USER,7,&canonical,&bad)==ENSMEM && !bad.ticket);
    space->refs=2;
    CHECK(!kaspace_mapping_can_change(&backend,0x1000,0x20000));
    CHECK(!kaspace_mapping_change(&backend,0x4000,1));
    CHECK(kaspace_materialization_change(&backend,0x4000,4096,ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    CHECK(!kaspace_materialization_change(&backend,0x4000,4096,ASPACE_PROT_USER|ASPACE_PROT_EXEC));
    CHECK(!kaspace_materialization_change(&backend,0x4000,4096,ASPACE_PROT_USER));

    CHECK(kaspace_mapping_can_change(&backend,0x6000,4096));
#if CONF_WITH_PRIVATE_CLONE
    CHECK(kaspace_cow_change(&backend,0x4000,4096));
#endif
#if CONF_WITH_MMAP
    {
        MMAP_REGION *region=kalloc(sizeof(*region));CHECK(region);
        region->address=0x10000;region->length=3*USER_PAGE_SIZE;
        region->prot=ASPACE_PROT_USER;region->backing=(FILE_BACKING *)1;
        region->reservation=kaspace_reservation_next();
        space->mmap.regions=region;space->mmap.bytes=region->length;
        CHECK(kaspace_reservation_hold(space,region->reservation,0x10000,4096,
              ASPACE_PROT_USER|ASPACE_PROT_WRITE,7,&canonical,&bad)==EACCDN && !bad.ticket);
        reservation_tickets=~(UQUAD)0-1;
        CHECK(kaspace_reservation_hold(space,region->reservation,0x10000,4096,
              ASPACE_PROT_USER,7,&canonical,&file)==E_OK && file.ticket==~(UQUAD)0);
        CHECK(kaspace_reservation_hold(space,birth,0x4000,1,ASPACE_PROT_USER,7,&canonical,&bad)==ENSMEM && !bad.ticket);
        CHECK(kaspace_materialization_change(&backend,0x10000,4096,ASPACE_PROT_USER));
        CHECK(!kaspace_materialization_change(&backend,0x10000,4096,ASPACE_PROT_USER|ASPACE_PROT_WRITE));

    }
#endif
    kaspace_put(space); /* parent drops root; holders own it independently */
    CHECK(!backend.destroyed && !walks);
    CHECK(kaspace_reservation_release(&heap,&owner) && owner==&canonical && !heap.ticket);
    owner=NULL;
    CHECK(!kaspace_reservation_release(&stale,&owner) && !owner);
#if CONF_WITH_MMAP
    CHECK(!backend.destroyed && !backing_puts);
    CHECK(kaspace_reservation_release(&file,&owner) && owner==&canonical && backing_puts==1);
#else
    (void)file;
#endif
    CHECK(backend.destroyed==1 && allocations==frees && !walks);
    reservation_ids=~(UQUAD)0-1;
    CHECK(kaspace_reservation_next()==~(UQUAD)0 && !kaspace_reservation_next());
    puts("Production reservation holds: lazy heap/file ownership without backing, exact birth/rights, stale tickets, partial failure, saturation, mutation barrier and final reclaim: PASS (host)");
    return 0;
}
