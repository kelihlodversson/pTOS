/* Production reservation ledger/holds. Backend and file resources are models. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "biosext.h"
#include "../bdos/kaspace.c"
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"reservation hold:%d: %s\n",__LINE__,#c); abort(); } } while (0)
struct aspace { unsigned destroyed; };
static unsigned allocations, frees, fail_alloc, walks, backing_puts, retired_private, retired_borrows, deferred_pins, inactive_retired;
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
        CHECK(!kaspace_reservation_info(space,0x20ff0,16,&info));
        CHECK(kaspace_reservation_info(space,0x20008,8,&info) && info.address==0x20008 && info.bytes==8);
        CHECK(kaspace_retained_lazy_protection(space,0x20000,&rights) && rights==(ASPACE_PROT_USER|ASPACE_PROT_WRITE));
        CHECK(kaspace_materialization_change(&backend,0x20000,4096,rights));
        CHECK(!kaspace_mapping_can_change(&backend,0x20000,4096));
        CHECK(kaspace_reservation_release(&one,&owner));owner=NULL;
        CHECK(kaspace_reservation_release(&two,&owner));owner=NULL;
        CHECK(!kaspace_retained_lazy_protection(space,0x20000,&rights));
    }
    {
        KASPACE_RESERVATION_INFO captured={0x30000,8192,kaspace_reservation_next(),ASPACE_PROT_USER,0};
        KASPACE_RESERVATION_HOLD one={0},two={0},three={0},spent;
        CHECK(kaspace_reservation_hold_captured(space,&captured,0x30000,16,ASPACE_PROT_USER,7,&canonical,&one)==E_OK);
        CHECK(kaspace_reservation_duplicate(&one,&canonical,&two)==E_OK);
        CHECK(kaspace_retire_private(space,0x30000,8192));
        CHECK(!kaspace_mapping_can_change(&backend,0x31000,4096));
        {
            VADDR first=0,end=0;
            CHECK(kaspace_next_held(space,0x30000,0x40000,&first,&end) && first==0x30000 && end==0x32000);
            CHECK(!kaspace_next_held(space,0x32000,0x40000,&first,&end));
        }
        spent=one;
        CHECK(kaspace_reservation_release(&one,&owner) && !retired_private);owner=NULL;
        CHECK(!kaspace_reservation_release(&spent,&owner));
        CHECK(kaspace_reservation_duplicate(&two,&canonical,&three)==E_OK);
        CHECK(kaspace_reservation_release(&two,&owner) && !retired_private);owner=NULL;
        CHECK(kaspace_reservation_release(&three,&owner) && retired_private==1);owner=NULL;
        CHECK(kaspace_mapping_can_change(&backend,0x30000,8192));
        captured.address=0x40000;captured.bytes=13;captured.identity=kaspace_reservation_next();
        CHECK(kaspace_reservation_hold_captured(space,&captured,0x40000,2,ASPACE_PROT_USER,7,&canonical,&one)==E_OK);
        CHECK(kaspace_retire_borrow(space,0x40000,13));
        CHECK(kaspace_retire_borrow(space,0x40000,4096));
        CHECK(kaspace_retire_borrow(space,0x40000,8));
        CHECK(kaspace_reservation_release(&one,&owner) && retired_borrows==3);owner=NULL;
        CHECK(kaspace_mapping_can_change(&backend,0x40000,4096));
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
        MMAP_REGION *region=kalloc(sizeof(*region));
        KASPACE_RESERVATION_HOLD one={0},two={0},spent;
        KASPACE_RESERVATION_INFO inactive;
        UWORD rights=0xffff;unsigned before_walks=walks;
        CHECK(region);region->address=0x190000;region->length=2*USER_PAGE_SIZE;
        region->reservation=kaspace_reservation_next();
        space->mmap.regions=region;space->mmap.bytes=region->length;
        CHECK(kaspace_reservation_info(space,region->address,region->length,&inactive) &&
              !inactive.rights && (inactive.flags&KASPACE_RESERVATION_LAZY) &&
              (inactive.flags&KASPACE_RESERVATION_INACCESSIBLE));
        CHECK(kaspace_reservation_hold_captured(space,&inactive,region->address,
                    region->length,ASPACE_PROT_USER,7,&canonical,&bad)==EACCDN && !bad.ticket);
        CHECK(kaspace_reservation_hold_captured(space,&inactive,region->address,
                    region->length,0,7,&canonical,&one)==E_OK);
        CHECK(kaspace_reservation_duplicate(&one,&canonical,&two)==E_OK);
        CHECK(walks==before_walks && !kaspace_retained_lazy_protection(space,region->address,&rights) && rights==0xffff);
        CHECK(!kaspace_mapping_can_change(&backend,region->address,region->length));
        CHECK(!kaspace_materialization_change(&backend,region->address,USER_PAGE_SIZE,0) &&
              !kaspace_materialization_change(&backend,region->address,USER_PAGE_SIZE,ASPACE_PROT_USER));
        spent=one;CHECK(kaspace_reservation_release(&one,&owner) && owner==&canonical);owner=NULL;
        CHECK(!kaspace_reservation_release(&spent,&owner) && !owner);
        CHECK(!kaspace_mapping_can_change(&backend,region->address,region->length));
        CHECK(kaspace_reservation_info(space,region->address,region->length,&info) &&
              info.identity==inactive.identity && !info.rights && info.flags==inactive.flags);
        CHECK(kaspace_reservation_release(&two,&owner) && owner==&canonical);owner=NULL;
        CHECK(kaspace_mapping_can_change(&backend,region->address,region->length) && walks==before_walks);
        CHECK(kaspace_reservation_hold_captured(space,&inactive,region->address,
                    region->length,0,7,&canonical,&one)==E_OK);
        CHECK(kaspace_retire_private(space,region->address,region->length) && !inactive_retired);
        space->mmap.regions=NULL;space->mmap.bytes=0;kfree(region);
        CHECK(kaspace_reservation_info(space,inactive.address,inactive.bytes,&info) &&
              info.identity==inactive.identity && !info.rights &&
              (info.flags&KASPACE_RESERVATION_LAZY) && (info.flags&KASPACE_RESERVATION_INACCESSIBLE));
        CHECK(!kaspace_retained_lazy_protection(space,inactive.address,&rights));
        CHECK(!kaspace_materialization_change(&backend,inactive.address,USER_PAGE_SIZE,0));
        CHECK(kaspace_reservation_release(&one,&owner) && owner==&canonical && inactive_retired==1);owner=NULL;
        CHECK(!kaspace_reservation_info(space,inactive.address,inactive.bytes,&info) &&
              kaspace_mapping_can_change(&backend,inactive.address,inactive.bytes) && walks==before_walks);
    }
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

void panic(const char *format, ...) { (void)format; abort(); }

BOOL aspace_release_lazy(ASPACE *as,VADDR va,VADDR bytes)
{ CHECK((va==0x30000 || va==0x4000) && bytes==8192 && kaspace_mapping_can_change(as,va,bytes)); retired_private++;return TRUE; }

void aspace_unborrow(ASPACE *as,VADDR va,VADDR bytes)
{ CHECK(va==0x40000 && bytes==13 && kaspace_mapping_can_change(as,va,bytes));retired_borrows++; }

BOOL aspace_release_lazy_retired(ASPACE *as,VADDR va,VADDR bytes)
{ CHECK(kaspace_retirement_change(as,va,bytes,TRUE));CHECK(!kaspace_retirement_change(as,va,bytes,TRUE));CHECK(!kaspace_mapping_can_change(as,va,bytes));
  if(va==0x190000) {CHECK(bytes==8192);inactive_retired++;}else retired_private++;
  return TRUE; }
BOOL aspace_defer_unborrow(ASPACE *as,VADDR va,VADDR bytes)
{ (void)as;CHECK(va==0x40000 && (bytes==13 || bytes==4096 || bytes==8));deferred_pins++;return TRUE; }
void aspace_unborrow_retired(ASPACE *as,VADDR va,VADDR bytes)
{ CHECK(va==0x40000 && bytes==4096); CHECK(kaspace_retirement_change(as,va,bytes,FALSE));CHECK(!kaspace_retirement_change(as,va,bytes,FALSE));CHECK(!kaspace_mapping_can_change(as,va,bytes));retired_borrows+=deferred_pins;deferred_pins=0; }

BOOL aspace_release_lazy_retired_allowed(const ASPACE *as,VADDR va,VADDR bytes)
{ (void)as;(void)va;(void)bytes;return TRUE; }
