/* Production PROCESS and mapping policy, with counted hosted MMU/file models.
 * Keeping an observer reference to a root must not keep dead PROCESS mappings.
 * This is not a CPU launcher or public shared-Pexec execution test. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "../bdos/kproc.c"
#include "../bdos/kaspace.c"

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"shared-root:%d: %s\n",__LINE__,#c); exit(1); } } while (0)
struct aspace { unsigned destroyed, releases; };
struct file_backing { unsigned references; };
static struct file_backing file;
static unsigned allocations, frees;
static PD child;
PD *run = &child;
volatile LONG hz_200;

void *kalloc(ULONG bytes) { void *p=calloc(1,bytes); if(p) allocations++; return p; }
void kfree(void *p) { if(p) { frees++; free(p); } }
void bzero(void *p,size_t n) { memset(p,0,n); }
void panic(const char *format,...) { fprintf(stderr,"panic: %s\n",format); abort(); }
void doassert(const char *f,long l,const char *fn,const char *e)
{ fprintf(stderr,"%s:%ld %s: %s\n",f,l,fn,e); abort(); }
SBYTE get_default_handle(int i) { (void)i; return -1; }
UQUAD procmem_gen(const void *p) { return p?1:0; }
ULONG procmem_size(const void *p) { (void)p; return 0; }
void kstack_free(PADDR p) { CHECK(!p); }
void aspace_leave(ASPACE_ROOT r) { CHECK(!r); }
void aspace_destroy(ASPACE *as) { CHECK(!as->destroyed); as->destroyed++; }
BOOL aspace_translate(const ASPACE *as,VADDR va,PADDR *pa,UWORD *prot)
{ (void)as;(void)va;(void)pa;(void)prot;return FALSE; }
ULONG aspace_mapped_bytes(const ASPACE *as) { (void)as;return 0; }
BOOL aspace_release_lazy_allowed(const ASPACE *as,VADDR va,VADDR bytes)
{ (void)as;(void)va;(void)bytes;return TRUE; }
BOOL aspace_release_lazy(ASPACE *as,VADDR va,VADDR bytes)
{ CHECK(kaspace_mapping_change(as,va,bytes));as->releases++;return TRUE; }
BOOL aspace_release_lazy_retired(ASPACE *as,VADDR va,VADDR bytes)
{ CHECK(kaspace_retirement_change(as,va,bytes,TRUE));as->releases++;return TRUE; }
void aspace_unborrow(ASPACE *as,VADDR va,VADDR bytes)
{ (void)as;(void)va;(void)bytes;abort(); }
void aspace_unborrow_retired(ASPACE *as,VADDR va,VADDR bytes)
{ (void)as;(void)va;(void)bytes;abort(); }
BOOL aspace_defer_unborrow(ASPACE *as,VADDR va,VADDR bytes)
{ (void)as;(void)va;(void)bytes;abort();return FALSE; }
BOOL aspace_move_private(ASPACE *a,ASPACE *b,VADDR va,VADDR bytes,UWORD prot)
{ (void)a;(void)b;(void)va;(void)bytes;(void)prot;abort();return FALSE; }
BOOL aspace_protect_lazy(ASPACE *as,VADDR va,VADDR bytes,UWORD prot)
{ (void)prot;return kaspace_mapping_change(as,va,bytes); }
LONG fileback_get(LONG fd,FILE_BACKING **result)
{ CHECK(fd==0 && result && !*result);file.references++;*result=&file;return E_OK; }
ULONG fileback_size(const FILE_BACKING *f) { CHECK(f==&file);return 65536; }
void fileback_hold(FILE_BACKING *f) { if(f) { CHECK(f==&file && f->references);f->references++; } }
void fileback_put(FILE_BACKING *f) { if(f) { CHECK(f==&file && f->references);f->references--; } }

static unsigned reservations(KASPACE *space)
{
    MMAP_REGION *r;
    unsigned count=kaspace_heap(space)->count;
    for(r=kaspace_mmap(space)->regions;r;r=r->next) count++;
    return count;
}
int main(void)
{
    ASPACE backend={0};
    KASPACE *observer=kaspace_adopt(&backend);
    LONG anonymous,backed;
    unsigned before,live;
    CHECK(observer && kproc_create(&child));
    CHECK(kproc_test_share_active(&child,observer));
    before=reservations(observer);
    anonymous=kproc_mmap(&child,0,3*USER_PAGE_SIZE,PROT_NONE,
                        MAP_PRIVATE|MAP_ANONYMOUS,0,0);
    backed=kproc_mmap(&child,0,3*USER_PAGE_SIZE,PROT_READ,MAP_PRIVATE,0,0);
    CHECK(anonymous>0 && backed>0 && file.references==1);
    CHECK(kproc_mprot(&child,anonymous+USER_PAGE_SIZE,USER_PAGE_SIZE,PROT_READ)==E_OK);
    CHECK(kproc_munmap(&child,backed+USER_PAGE_SIZE,USER_PAGE_SIZE)==E_OK);
    live=reservations(observer);
    CHECK(live==before+5 && file.references==2);
    CHECK(kproc_begin_exit(&child));
    kproc_destroy(&child);
    printf("observer-root reservations: before=%u live=%u completed=%u; file refs=%u; root destroyed=%u\n",
           before,live,reservations(observer),file.references,backend.destroyed);
    fflush(stdout);
    CHECK(!backend.destroyed && !kproc_find(&child));
    CHECK(reservations(observer)==before && !kaspace_mmap(observer)->bytes && !file.references);
    kaspace_put(observer);
    CHECK(backend.destroyed==1 && allocations==frees);
    puts("Shared-root final PROCESS mapping retirement: PASS (production policy, hosted backend)");
    return 0;
}
