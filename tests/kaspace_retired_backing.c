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
    CHECK(kaspace_reservation_release(&outside,&owner) && owner==&canonical);owner=NULL;
    CHECK(retired_private==1 && !kaspace_reservation_release(&narrow,&owner));
    CHECK(!kaspace_retained_lazy_protection(space,0x90000,&rights));
    kaspace_put(space);
    CHECK(backend.destroyed==1 && allocations==frees && !walks);
    puts("Production retired reservation: exact backing bounds, independent rights, consumed fence, once-only cleanup: PASS (host)");
    return 0;
}
