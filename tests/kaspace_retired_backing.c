/* Production KASPACE ownership; the reused fixture models backend resources.
 * A destruction fence is deliberately wider than its surviving exact owner. */
#define main reservation_fixture_main
#include "kaspace_reservation.c"
#undef main

int main(void)
{
    ASPACE backend={0};
    KASPACE *space=kaspace_adopt(&backend);
    int canonical;
    KASPACE_RESERVATION_INFO captured={0x90000,12288,0,
        ASPACE_PROT_USER|ASPACE_PROT_WRITE,KASPACE_RESERVATION_LAZY};
    KASPACE_RESERVATION_INFO info;
    KASPACE_RESERVATION_HOLD narrow={0}, outside={0};
    UWORD rights=0;
    void *owner=NULL;
    CHECK(space);
    captured.identity=kaspace_reservation_next();
    captured.rights=ASPACE_PROT_USER;
    /* Different logical owner, same backing allocation, different rights. */
    CHECK(kaspace_reservation_hold_captured(space,&captured,0x90008,8,
          ASPACE_PROT_USER,8,&canonical,&outside)==E_OK);
    captured.identity=kaspace_reservation_next();
    captured.rights=ASPACE_PROT_USER|ASPACE_PROT_WRITE;
    CHECK(kaspace_reservation_hold_captured(space,&captured,0x91008,8,
          captured.rights,7,&canonical,&narrow)==E_OK);
    CHECK(!kaspace_retained_lazy_protection(space,0x92000,&rights));
    CHECK(kaspace_retire_private(space,0x90000,12288));
    /* The middle hold now carries a three-page cleanup fence. Its captured
     * rights must neither authorize the third page nor override the first. */
    CHECK(!kaspace_retained_lazy_protection(space,0x92000,&rights));
    CHECK(!kaspace_materialization_change(&backend,0x92000,4096,captured.rights));
    CHECK(kaspace_retained_lazy_protection(space,0x91000,&rights) && rights==captured.rights);
    CHECK(kaspace_materialization_change(&backend,0x91000,4096,rights));
    CHECK(kaspace_retained_lazy_protection(space,0x90000,&rights) && rights==ASPACE_PROT_USER);
    CHECK(kaspace_materialization_change(&backend,0x90000,4096,rights));
    CHECK(!kaspace_materialization_change(&backend,0x90000,4096,captured.rights));
    CHECK(!kaspace_mapping_can_change(&backend,0x92000,4096));
    CHECK(kaspace_reservation_release(&narrow,&owner) && owner==&canonical);owner=NULL;
    /* The consumed cleanup record remains until the independent owner exits.
     * It is not a reusable logical reservation or backing authority. */
    CHECK(!retired_private && !kaspace_reservation_info(space,0x91008,8,&info));
    CHECK(!kaspace_retained_lazy_protection(space,0x91000,&rights));
    CHECK(!kaspace_materialization_change(&backend,0x91000,4096,captured.rights));
    CHECK(kaspace_retained_lazy_protection(space,0x90000,&rights) && rights==ASPACE_PROT_USER);
    CHECK(kaspace_materialization_change(&backend,0x90000,4096,rights));
    CHECK(!kaspace_mapping_can_change(&backend,0x92000,4096));
#if CONF_WITH_MMAP
    {
        /* Authoritative ordinary metadata retains its real backing. Unlike
         * captured fallback it need not be anonymous/zero-fill LAZY. */
        MMAP_REGION *region=kalloc(sizeof(*region));CHECK(region);
        region->address=0x91000;region->length=4096;
        region->reservation=kaspace_reservation_next();
        region->prot=ASPACE_PROT_USER;region->backing=(FILE_BACKING *)1;
        region->next=NULL;space->mmap.regions=region;
        CHECK(kaspace_reservation_info(space,0x91000,4096,&info) && !info.flags);
        CHECK(kaspace_materialization_change(&backend,0x91000,4096,ASPACE_PROT_USER));
        CHECK(!kaspace_materialization_change(&backend,0x91000,4096,captured.rights));
        CHECK(!kaspace_retained_lazy_protection(space,0x91000,&rights));
        space->mmap.regions=NULL;fileback_put(region->backing);kfree(region);
        CHECK(backing_puts==1 && !kaspace_materialization_change(&backend,0x91000,4096,ASPACE_PROT_USER));
    }
#endif
    CHECK(kaspace_reservation_release(&outside,&owner) && owner==&canonical);owner=NULL;
    CHECK(retired_private==1 && !kaspace_reservation_release(&narrow,&owner));
    CHECK(!kaspace_retained_lazy_protection(space,0x90000,&rights));
    {
        KASPACE_RESERVATION_HOLD parent={0},child={0},grandchild={0};
        KASPACE_HEAP_BLOCK *block;
        unsigned before=retired_private;UQUAD birth;
        space->heap.blocks=block=kalloc(sizeof(*block));CHECK(block);
        space->heap.count=space->heap.capacity=1;
        block->va=0x30000;block->pages=2;block->flags=0;
        block->reservation=birth=kaspace_reservation_next();
        CHECK(kaspace_reservation_hold(space,birth,block->va,8192,
              ASPACE_PROT_USER|ASPACE_PROT_WRITE,7,&canonical,&parent)==E_OK);
        CHECK(kaspace_reservation_duplicate(&parent,&canonical,&child)==E_OK);
        CHECK(kaspace_retire_heap(space,block->va,birth));
        CHECK(!kaspace_retire_heap(space,block->va,birth) && space->heap.count==1);
        CHECK(kaspace_reservation_release(&parent,&owner));owner=NULL;
        CHECK(kaspace_reservation_duplicate(&child,&canonical,&grandchild)==E_OK);
        CHECK(kaspace_reservation_info(space,block->va,8192,&info) && info.identity==birth);
        CHECK(info.rights==(ASPACE_PROT_USER|ASPACE_PROT_WRITE));
        CHECK(kaspace_reservation_release(&child,&owner));owner=NULL;
        CHECK(retired_private==before && space->heap.count==1);
        CHECK(kaspace_reservation_release(&grandchild,&owner));owner=NULL;
        CHECK(retired_private==before+1 && !space->heap.count);
        CHECK(!kaspace_reservation_info(space,0x30000,8192,&info));
        /* Failure before the first hold takes the immediate cleanup path. */
        space->heap.count=1;block->flags=0;
        block->reservation=kaspace_reservation_next();
        CHECK(kaspace_retire_heap(space,block->va,block->reservation));
        CHECK(retired_private==before+2 && !space->heap.count);
    }
    kaspace_put(space);
    CHECK(backend.destroyed==1 && allocations==frees && !walks);
    puts("Production retired reservation: exact backing bounds, independent rights, consumed fence, once-only cleanup: PASS (host)");
    return 0;
}
