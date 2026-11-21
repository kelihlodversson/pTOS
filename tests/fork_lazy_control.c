/* Production KASPACE, empty modeled MMU roots: a private clone inherits the
 * source's captured lazy reservation authority as its own record (#971,
 * #974/#1000), which survives the source's own release. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "biosext.h"
#include "procmem.h"
#include "gemerror.h"
#include "kaspace.h"
struct aspace { int marker; };
static ASPACE child_backend = { 43 };
BOOL aspace_logical_protection(const ASPACE *as,VADDR va,UWORD *rights) { (void)as;(void)va;(void)rights;return FALSE; }
void *kalloc(ULONG bytes) { return calloc(1,bytes); }
void kfree(void *p) { free(p); }
void panic(const char *fmt, ...) { (void)fmt; abort(); }
void aspace_destroy(ASPACE *as) { (void)as; }
LONG aspace_clone(ASPACE *source,ASPACE **out) { (void)source; *out=&child_backend;return E_OK; }
BOOL aspace_translate(const ASPACE *as,VADDR va,PADDR *p,UWORD *r)
{ (void)as;(void)va;(void)p;(void)r;return FALSE; }
BOOL aspace_release_lazy(ASPACE *as,VADDR va,VADDR bytes) { (void)as;(void)va;(void)bytes;return TRUE; }
void aspace_unborrow(ASPACE *as,VADDR va,VADDR bytes) { (void)as;(void)va;(void)bytes; }
BOOL aspace_release_lazy_retired(ASPACE *as,VADDR va,VADDR bytes) { (void)as;(void)va;(void)bytes;return TRUE; }
void aspace_unborrow_retired(ASPACE *as,VADDR va,VADDR bytes) { (void)as;(void)va;(void)bytes; }
BOOL aspace_defer_unborrow(ASPACE *as,VADDR va,VADDR bytes) { (void)as;(void)va;(void)bytes;return TRUE; }
BOOL aspace_release_lazy_retired_allowed(const ASPACE *as,VADDR va,VADDR bytes) { (void)as;(void)va;(void)bytes;return TRUE; }
int main(int argc,char **argv) {
    ASPACE source_backend={42};
    KASPACE *source=kaspace_adopt(&source_backend),*child=NULL;
    KASPACE_RESERVATION_INFO captured={0x20000,4096,0,ASPACE_PROT_USER|ASPACE_PROT_WRITE,KASPACE_RESERVATION_LAZY};
    KASPACE_RESERVATION_HOLD hold={0};int identity;void *released=NULL;UWORD source_rights=0,child_rights=0;
    (void)argc;(void)argv;
    captured.identity=kaspace_reservation_next();
    if(kaspace_reservation_hold_captured(source,&captured,0x20000,4096,captured.rights,7,&identity,&hold)!=E_OK)abort();
    if(!kaspace_has_captured_lazy(source))abort();
    if(kaspace_clone(source,&child)!=E_OK)abort();
    if(!kaspace_retained_lazy_protection(source,0x20000,&source_rights) ||
       !kaspace_retained_lazy_protection(child,0x20000,&child_rights) ||
       child_rights!=source_rights || !kaspace_has_captured_lazy(child))abort();
    if(!kaspace_reservation_release(&hold,&released) || released!=&identity ||
       kaspace_has_captured_lazy(source) || !kaspace_has_captured_lazy(child))abort();
    printf("source and child retained lazy authority; child record survives source release\n");
    kaspace_put(child);kaspace_put(source);
    return 0;
}
