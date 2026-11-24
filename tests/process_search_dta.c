/* Production Fsfirst/Fsnext staging and KASPACE leases with a counted MMU
 * model. Native private-root tests separately exercise hardware COW. */
#define main private_capture_fixture_main
#include "private_snapshot.c"
#undef main
#undef LITTLE_ENDIAN
#undef BIG_ENDIAN
#undef BYTE_ORDER
#include "../bdos/fsdir.c"

struct kproc { PD *pd;ULONG references;PROCESS_FILES files; };
static struct kproc process;
static ASPACE backend;
static KASPACE *space;
static PD pd;
PD *run=&pd;
#if CONF_WITH_MULTITHREADING
static FS_ERROR_STATE search_error;
FS_ERROR_STATE *fs_error_current(void) { return &search_error; }
void ksched_cleanup_begin(KSCHED_CLEANUP_SCOPE *scope)
{ CHECK(!scope->active); scope->active=TRUE; }
void ksched_cleanup_end(KSCHED_CLEANUP_SCOPE *scope)
{ CHECK(scope->active); scope->active=FALSE; }
#else
FS_ERROR_STATE fs_legacy_error;
#endif
static UBYTE user_bytes[3*USER_PAGE_SIZE] __attribute__((aligned(USER_PAGE_SIZE)));
static DTAINFO *logical;
static UBYTE trusted[offsetof(DTAINFO,dt_fattr)];
static BOOL saved;
static unsigned callbacks,copyouts,action,back_error;
static WORD search_cursor;

PROCESS_FILES *kproc_files(PD *p) { return p==&pd?&process.files:NULL; }
PROCESS_FILES *kproc_ref_files(KPROC *p) { return p?&p->files:NULL; }
UQUAD kproc_ref_number(const KPROC *p) { return p?1:0; }
PD *kproc_live_pd(const KPROC *p) { return p?p->pd:NULL; }
KPROC *kproc_retain_live(KPROC *p) { CHECK(p && p->references);p->references++;return p; }
void kproc_release(KPROC *p) { CHECK(p && p->references>1);p->references--; }
BOOL kproc_file_references_allowed(PD *p) { CHECK(p==&pd);return TRUE; }
ASPACE *kproc_user_aspace(PD *p) { CHECK(p==&pd);return &backend; }
KASPACE *kproc_active_space(const KPROC *p) { CHECK(p==&process);return space; }
DTAINFO *kproc_get_dta(PD *p) { CHECK(p==&pd);return logical; }
static BOOL writable(VADDR address,ULONG size)
{
    VADDR page,last=(address+size-1)&~(VADDR)(USER_PAGE_SIZE-1);
    for(page=address&~(VADDR)(USER_PAGE_SIZE-1);;page+=USER_PAGE_SIZE) {
        struct model_page *entry=page_find(&backend,page);
        if(!entry || (entry->rights&(ASPACE_PROT_USER|ASPACE_PROT_WRITE))!=(ASPACE_PROT_USER|ASPACE_PROT_WRITE)) return FALSE;
        if(page==last) return TRUE;
    }
}
LONG kproc_back_user_write_checked(VADDR address,ULONG bytes)
{ return back_error?(LONG)back_error:writable(address,bytes)?E_OK:EIMBA; }
BOOL kproc_copy_from_user_resident(void *out,VADDR address,ULONG bytes)
{ CHECK(writable(address,bytes));memcpy(out,(void *)(uintptr_t)address,bytes);return TRUE; }
LONG kproc_copy_to_user_checked(VADDR address,const void *in,ULONG bytes)
{
    CHECK(writable(address,bytes));copyouts++;memcpy((void *)(uintptr_t)address,in,bytes);return E_OK;
}
void kproc_dta_forget(PD *p,const DTAINFO *address)
{ CHECK(p==&pd && address==logical);saved=FALSE; }
BOOL kproc_dta_restore_to(PD *p,const DTAINFO *address,DTAINFO *out)
{ CHECK(p==&pd && address==logical);if(saved)memcpy(out,trusted,sizeof(trusted));return saved; }
void kproc_dta_save_from(PD *p,const DTAINFO *address,const DTAINFO *in)
{ CHECK(p==&pd && address==logical);memcpy(trusted,in,sizeof(trusted));saved=TRUE; }
void mark_bcbs_invalid(int drive) { CHECK(drive==2); }
static LONG provider(DTAINFO *address,DTAINFO *out)
{
    VADDR first=(VADDR)(uintptr_t)address&~(VADDR)(USER_PAGE_SIZE-1);
    CHECK(address==logical && out!=address && process.files.operations==1 && process.files.search_busy);
    CHECK(space->leases && !space->leases->sealed);callbacks++;
    memset(out,0,sizeof(*out));out->dt_clnum=++search_cursor;strcpy(out->dt_fname,"STAGED.TXT");
    if(action==1) {
        CHECK(aspace_unmap_private(&backend,first,2*USER_PAGE_SIZE));
        CHECK(aspace_fault_in(&backend,first,ASPACE_PROT_USER|ASPACE_PROT_WRITE));
        CHECK(aspace_fault_in(&backend,first+USER_PAGE_SIZE,ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    }
    if(action==2) {
        CHECK(kaspace_mapping_change(&backend,first,2*USER_PAGE_SIZE));
        page_find(&backend,first)->rights=ASPACE_PROT_USER;
    }
    if(action==3) {errcode=E_CHNG;errdrv=2;rwerr=E_CHNG;longjmp(errbuf,1);}
    return E_OK;
}
LONG pfs_do_sfirst_to(char *name,WORD attr,DTAINFO *address,DTAINFO *out)
{ (void)name;(void)attr;return provider(address,out); }
LONG pfs_do_snext_to(DTAINFO *address,DTAINFO *out)
{ return provider(address,out); }
int main(void)
{
    unsigned mode;
    VADDR first;
    logical=(DTAINFO *)(user_bytes+USER_PAGE_SIZE-16);first=(VADDR)(uintptr_t)user_bytes;
    process.pd=&pd;process.references=1;process.files.owner=&process;process.files.number=1;process.files.epoch=1;
    space=kaspace_adopt(&backend);CHECK(space);
    for(mode=0;mode<6;mode++) {
        unsigned before=copyouts,baseline_callbacks=callbacks;
        LONG result;
        DISK_ERROR disk={0};
        CHECK(aspace_fault_in(&backend,first,ASPACE_PROT_USER|ASPACE_PROT_WRITE));
        CHECK(aspace_fault_in(&backend,first+USER_PAGE_SIZE,ASPACE_PROT_USER|ASPACE_PROT_WRITE));
        memset(logical,0x57,sizeof(*logical));saved=FALSE;
        action=mode<4?mode:0;back_error=mode==4?(unsigned)ENSMEM:0;
        if(mode==5) page_find(&backend,first+USER_PAGE_SIZE)->rights=ASPACE_PROT_USER;
        result=DISK_CALL_ERROR(xsfirst("C:\\*.TXT",0),&disk);
        if(mode==0) CHECK(result==E_OK && saved && copyouts==before+1 && !strcmp(logical->dt_fname,"STAGED.TXT"));
        else {
            CHECK(result==(mode==3?E_CHNG:mode==4?ENSMEM:EIMBA));
            CHECK(!saved && copyouts==before);
            { unsigned i;for(i=0;i<sizeof(*logical);i++) CHECK(((UBYTE *)logical)[i]==0x57); }
        }
        CHECK(callbacks==baseline_callbacks+(mode<4?1:0));
        CHECK(process.references==1 && !process.files.operations && !process.files.search_busy && !space->leases);
    }
    kaspace_put(space);CHECK(allocations==frees);
    puts("Production Fsfirst staged DTA: cross-page copyout, same-VA remap/protection revocation, disk unwind and checked backing-failure conservation: PASS");
    return 0;
}
