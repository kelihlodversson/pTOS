/* Production startup draft/backends, modeled mappings and stopped CPU root. */
#include "emutos.h"
#include "biosext.h"
#include "procmem.h"
#include "kaspace.h"
#include "thread_startup.h"
#include "thread_startup_backend.h"
#include "thread_return.h"
#include "gemerror.h"
#include "setjmp.h"
#include "fs_error.h"
#if ARCH_X86_64
#include "thread_startup_x32.h"
#else
#include "thread_startup32.h"
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define CODE 0x400000UL
#define STACK 0x10000000UL
#if ARCH_X86_64
#define BRIDGE (USER_VA_LIMIT-USER_PAGE_SIZE)
#else
#define BRIDGE USER_THREAD_RETURN_VA
#endif
struct aspace { int marker; };
static unsigned allocated,stacks,aliases,observed,fail_after;
static UBYTE stack_data[8192] __attribute__((aligned(16))),bridge_data[4096];
static BOOL bridge_live,stack_live=TRUE;
static FS_ERROR_STATE disk_error;
FS_ERROR_STATE *fs_error_current(void) { return &disk_error; }
UQUAD ksched_context_id(void) { return 1; } /* one modeled CPU continuation */
BOOL ksched_startup_reclaimable(const THREAD_STARTUP_DRAFT *draft) { return draft!=NULL; }
int fixture_setjmp(jmp_buf state) { (void)state;return 0; }
void panic(const char *format,...) { fprintf(stderr,"panic: %s\n",format);abort(); }
void *kalloc(ULONG bytes) { void *p;if(fail_after && !--fail_after)return NULL;p=malloc(bytes);if(p)allocated++;return p; }
void kfree(void *p) { if(p){assert(allocated);allocated--;free(p);} }
PADDR kstack_alloc(uintptr_t *top) { stacks++;*top=stacks*16384;return stacks*4096; }
void kstack_free(PADDR physical) { if(physical){assert(stacks);stacks--;} }
void aspace_destroy(ASPACE *as) { assert(as->marker==42); }
ASPACE_ROOT aspace_root(const ASPACE *as) { (void)as;return 0xabc000; }
BOOL aspace_translate(const ASPACE *as,VADDR va,PADDR *physical,UWORD *prot)
{
    UWORD rights;(void)as;(void)physical;
    if(va>=CODE && va<CODE+4096)rights=ASPACE_PROT_USER|ASPACE_PROT_EXEC;
    else if(stack_live && va>=STACK && va<STACK+sizeof(stack_data))rights=ASPACE_PROT_USER|ASPACE_PROT_WRITE;
    else if(bridge_live && va>=BRIDGE && va<BRIDGE+4096)rights=ASPACE_PROT_USER|ASPACE_PROT_EXEC;
    else return FALSE;
    if(prot)*prot=rights;
    return TRUE;
}
BOOL aspace_user_range_ok(const ASPACE *as,VADDR va,VADDR bytes,BOOL write)
{ (void)as;(void)write;return stack_live && va>=STACK && va<STACK+sizeof(stack_data) && bytes<=STACK+sizeof(stack_data)-va; }
BOOL aspace_private_range_ok(const ASPACE *as,VADDR va,VADDR bytes)
{ UWORD rights;return bytes && aspace_translate(as,va,NULL,&rights); }
BOOL aspace_load_private(ASPACE *as,VADDR va,VADDR bytes,UWORD rights,const void *data,ULONG size)
{ (void)as;(void)rights;assert(va==BRIDGE && bytes==4096 && size<=4096 && !bridge_live);memcpy(bridge_data,data,size);bridge_live=TRUE;return TRUE; }
BOOL aspace_unmap_private(ASPACE *as,VADDR va,VADDR bytes)
{ assert(va==BRIDGE && bytes==4096);if(!kaspace_mapping_change(as,va,bytes))return FALSE;bridge_live=FALSE;return TRUE; }
BOOL aspace_copy_from_user(const ASPACE *as,void *dst,VADDR va,ULONG bytes)
{ (void)as;if(!bridge_live || va!=BRIDGE || bytes>4096)return FALSE;memcpy(dst,bridge_data,bytes);return TRUE; }
BOOL aspace_copy_to_user_ok(const ASPACE *as,VADDR va,ULONG bytes)
{ return aspace_user_range_ok(as,va,bytes,TRUE); }
void *aspace_user_write_alias(const ASPACE *as,VADDR va,ULONG bytes)
{ aliases++;return aspace_user_range_ok(as,va,bytes,TRUE)?stack_data+va-STACK:NULL; }
BOOL aspace_release_lazy(ASPACE *as,VADDR va,VADDR bytes) { (void)as;(void)va;(void)bytes;abort(); }
BOOL aspace_release_lazy_retired(ASPACE *as,VADDR va,VADDR bytes) { (void)as;(void)va;(void)bytes;abort(); }
BOOL aspace_release_lazy_retired_allowed(const ASPACE *as,VADDR va,VADDR bytes)
{ (void)as;(void)va;(void)bytes;abort(); } /* this fixture has no ordinary reservations */
void aspace_unborrow(ASPACE *as,VADDR va,VADDR bytes) { (void)as;(void)va;(void)bytes;abort(); }
void aspace_unborrow_retired(ASPACE *as,VADDR va,VADDR bytes) { (void)as;(void)va;(void)bytes;abort(); }
BOOL aspace_defer_unborrow(ASPACE *as,VADDR va,VADDR bytes) { (void)as;(void)va;(void)bytes;abort(); }
#if CONF_WITH_MMAP
void fileback_put(FILE_BACKING *backing) { (void)backing;abort(); }
#endif
static LONG acquire(void *context) { (void)context;observed++;return E_OK; }
static void release(void *context) { (void)context;assert(observed);observed--; }
static VADDR image_sp(const THREAD_STARTUP_DRAFT *draft)
{
#if ARCH_X86_64
    return ((const X32_THREAD_STARTUP_DRAFT *)thread_startup_image(draft))->user.rsp;
#else
    return ((const THREAD_STARTUP32 *)thread_startup_image(draft))->sp;
#endif
}
int main(void)
{
    ASPACE as={42};KASPACE *space=kaspace_adopt(&as);
    THREAD_STARTUP_ARGS args={CODE,0x12345678,STACK,8192};
    THREAD_STARTUP_DRAFT *draft=NULL;
    THREAD_STARTUP_OBSERVER observer={acquire,release};
    VADDR initial=STACK+4096;UBYTE before[8192];unsigned count,i;
    assert(space);memset(stack_data,0xa5,sizeof(stack_data));stack_data[4096]=3;
    memcpy(before,stack_data,sizeof(before));count=allocated;
    assert(thread_startup_prepare_elf(space,&args,initial+1,&draft)==EINVAL && !draft);
    assert(thread_startup_prepare_elf(space,&args,STACK-16,&draft)==EINVAL && !draft);
    assert(thread_startup_prepare_elf(space,&args,STACK+8192,&draft)==EINVAL && !draft);
#if ARCH_X86_64
    assert(thread_startup_prepare_elf(space,&args,STACK+112,&draft)==EINVAL && !draft);
#endif
    assert(allocated==count && !stacks && !bridge_live);
    /* Backend override rejects bad SP without changing the existing image. */
    assert(thread_startup_prepare(space,&args,&draft)==E_OK);
    {
#if ARCH_X86_64
        X32_THREAD_STARTUP_DRAFT expected=*(const X32_THREAD_STARTUP_DRAFT *)thread_startup_image(draft);
        assert(expected.user.rdi==args.argument);
#else
        THREAD_STARTUP32 expected=*(const THREAD_STARTUP32 *)thread_startup_image(draft);
        assert(expected.argument==args.argument);
#endif
        assert(thread_startup_arch_elf_entry(&as,&args,initial+1,(void *)thread_startup_image(draft))==EINVAL);
        assert(!memcmp(&expected,thread_startup_image(draft),sizeof(expected)));
        assert(thread_startup_arch_elf_entry(&as,&args,initial,(void *)thread_startup_image(draft))==E_OK);
#if ARCH_X86_64
        expected.user.rsp=initial;
        expected.user.rdi=0;
#else
        expected.sp=initial;
#if ARCH_ARM
        expected.argument=0;
#endif
#endif
        assert(!memcmp(&expected,thread_startup_image(draft),sizeof(expected)));
    }
    thread_startup_abort(&draft);
    assert(thread_startup_prepare(space,&args,&draft)==E_OK);
#if ARCH_X86_64
    assert(image_sp(draft)==STACK+8192-8);
#elif ARCH_ARM
    assert(image_sp(draft)==STACK+8192);
#else
    assert(image_sp(draft)==STACK+8192-8);
#endif
    assert(thread_startup_observe(draft,&observer,NULL)==E_OK);
    aliases=0;assert(thread_startup_run_preflight(draft)==E_OK && aliases);
    assert(thread_startup_elf_initial_sp(draft)==0);
    assert(thread_startup_seal(draft)==E_OK);thread_startup_commit_stores(draft,0,0);
    thread_startup_publish(draft);assert(thread_startup_elf_initial_sp(draft)==0);
    thread_startup_entering(draft);thread_startup_reclaim(&draft);
    assert(!stacks && !observed && allocated==count);memcpy(stack_data,before,sizeof(before));
    /* Every allocation prefix must unwind independent stacks/bridge/leases. */
    for(i=1;i<=9;i++) {
        LONG error;fail_after=i;
        error=thread_startup_prepare_elf(space,&args,initial,&draft);fail_after=0;
        assert(error==ENSMEM || error==E_OK);
        thread_startup_abort(&draft);
        assert(allocated==count && !stacks && !bridge_live);
    }
    assert(thread_startup_prepare_elf(space,&args,initial,&draft)==E_OK && image_sp(draft)==initial);
    assert(thread_startup_elf_initial_sp(draft)==0);
    assert(thread_startup_observe(draft,&observer,NULL)==E_OK);
    aliases=0;assert(thread_startup_run_preflight(draft)==E_OK && !aliases);
    assert(thread_startup_seal(draft)==E_OK);
    thread_startup_commit_stores(draft,0,0);
    assert(!aliases && !memcmp(stack_data,before,sizeof(before)) && image_sp(draft)==initial);
    thread_startup_publish(draft);
    assert(thread_startup_elf_initial_sp(draft)==initial && image_sp(draft)==initial);
    thread_startup_entering(draft);thread_startup_reclaim(&draft);
    assert(!draft && !stacks && !observed && allocated==count && !bridge_live);
    assert(thread_startup_prepare_elf(space,&args,initial,&draft)==E_OK);
    assert(kaspace_mapping_change(&as,STACK,8192));
    assert(thread_startup_validate(draft)==EIMBA);thread_startup_abort(&draft);
    kaspace_put(space);assert(!allocated && !stacks && !bridge_live);
    puts("Production ELF startup: exact SP, standard registers, preserved argc, no frame aliases/stores, C path, failure prefixes and revoked leases: PASS (modeled CPU)");
    return 0;
}
