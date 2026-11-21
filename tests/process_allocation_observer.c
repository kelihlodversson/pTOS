/* Production allocation metadata/pins; no guest mappings are dereferenced. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "../bios/machine/pc-x86_64/procmem.c"
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"allocation observer:%d: %s\n",__LINE__,#c); abort(); } } while (0)
static unsigned allocations, frees, notifications;
static void *watched;
void *kalloc(ULONG bytes) { void *p=calloc(1,bytes); if(p) allocations++; return p; }
void kfree(void *p) { CHECK(p); frees++; free(p); }
static void notify(void *token)
{
    CHECK(token == &notifications && watched && !procmem_size(watched) && !procmem_gen(watched));
    notifications++;
}
int main(void)
{
    unsigned i;
    int owner=0;
    window_phys=0x200000; free_pages=WINDOW_PAGES;
    CHECK(!procmem_on_free(NULL,notify,&notifications));
    for (i=0;i<3;i++) {
        watched=procmem_alloc(4096,0); CHECK(watched);
        procmem_set_owner(watched,&owner);
        CHECK(procmem_on_free(watched,notify,&notifications));
        CHECK(!procmem_on_free(watched,notify,&notifications));
        if (!i) CHECK(procmem_free(watched));
        else {
            procmem_pin((VADDR)(uintptr_t)watched,4096,1);
            CHECK(!procmem_free(watched) && notifications==i);
            procmem_free_owned(&owner,NULL);
            CHECK(procmem_size(watched) && notifications==i);
            if (i==2) {
                /* Reassignment postpones reclamation but never substitutes
                 * a new token for the already-retained identity. */
                procmem_set_owner(watched,NULL);
                procmem_pin((VADDR)(uintptr_t)watched,4096,-1);
                CHECK(procmem_size(watched) && notifications==i);
                CHECK(procmem_free(watched));
            } else procmem_pin((VADDR)(uintptr_t)watched,4096,-1);
        }
        CHECK(notifications==i+1 && allocations==frees && free_pages==WINDOW_PAGES);
    }
    puts("Production allocation observer: actual release, deferred pins, ownership transfer and exactly-once finalization: PASS");
    return 0;
}
