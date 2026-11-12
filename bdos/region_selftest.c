/* Actual owner/MMU tests entered from a managed user Syield. No user ABI. */
#include "emutos.h"
#include "biosext.h"
#include "ksched.h"
#include "kproc.h"
#include "kaspace.h"
#include "proc_clone.h"
#include "mem.h"
#include "procmem.h"
#include "mman.h"
#include "fs.h"
#include "proc.h"
#include "region_selftest.h"
#include "kprint.h"
#include "gemerror.h"
#include "string.h"
#include "progimage.h"
extern PD *run;
#define REQUIRE(c) do { if (!(c)) panic("native region ownership: %s",#c); } while (0)
#define PAGE USER_PAGE_SIZE

void kproc_region_selftest(void)
{
    static BOOL tested;
    KSCHED_CLEANUP_SCOPE scope={0};
    FS_ERROR_STATE error;
    KPROC *source, *owner=NULL;
    PD *pd;
    void *env;
    ASPACE *backend;
    KASPACE *space;
    PROCMEM_STATS before, after;
    KPROC_ALLOCATION_HOLD raw={{0}}, raw_copy={{0}};
    KASPACE_RESERVATION_INFO info;
    KASPACE_RESERVATION_HOLD base={0}, environment={0}, stack={0}, copy={0}, image={0};
    KASPACE_RESERVATION_HOLD staged={0};
    PROG_LAYOUT layout;
    VADDR image_address;
    ULONG records, value=0x8941234UL;
    VADDR stack_page=USER_STACK_TOP-PAGE;
#if CONF_WITH_MMAP
    LONG a,b;
    ULONG actual=0;
    KASPACE_RESERVATION_HOLD mapping={0};
#endif
    if (tested || !ksched_exit_thread(run) || ksched_cleanup_active() || ksched_cancellation_pending()) return;
    tested=TRUE; /* prevent reentry from any future teardown callback */
    error=*fs_error_current();
    ksched_cleanup_begin(&scope);
    procmem_stats(&before); records=kproc_count();
    source=kproc_retain(run); REQUIRE(source);
    REQUIRE(proc_create_clone_process(source,0,&owner)==E_OK && owner);
    pd=kproc_live_pd(owner); REQUIRE(pd);
    /* Factory output is intentionally absent from the caller root. Reuse
     * the loader's explicit temporary mappings before reading PD fields. */
    REQUIRE(kproc_borrow(run,pd));
    env=USERPTR_TO_PTR(pd->p_env);
    REQUIRE(kproc_borrow(run,env));
    /* Exercise the real staged-loader allocation/move primitives. These zero
     * pages are NEVER executed; this is ownership proof, not Pexec4 evidence. */
    image_address=kproc_load_alloc(run,pd,2*PAGE); REQUIRE(image_address);
    memset(&layout,0,sizeof(layout));
    layout.nseg=2; layout.entry=image_address; layout.end=image_address+2*PAGE;
    layout.seg[0].vaddr=image_address; layout.seg[0].memsz=PAGE; layout.seg[0].flags=5;
    layout.seg[1].vaddr=image_address+PAGE; layout.seg[1].memsz=PAGE; layout.seg[1].flags=6;
    REQUIRE(kproc_set_moved_image(pd,&layout,run,image_address,2));
    kproc_set_creator(pd,run);
    REQUIRE(kproc_reservation_info(run,image_address,PAGE,&info)==E_OK);
    REQUIRE(kproc_reservation_hold(run,image_address,PAGE,info.identity,ASPACE_PROT_USER,&staged)==E_OK);
    REQUIRE(!kproc_allocation_disposal_allowed(pd) && kproc_check_launch(pd,run)==EACCDN);
    REQUIRE(!kproc_prepare_user(pd,run) && !kproc_user_aspace(pd) && kproc_live_pd(owner)==pd);
    REQUIRE(kproc_reservation_release(&staged));
    REQUIRE(kproc_prepare_user(pd,run));
    kproc_unborrow(env); kproc_unborrow(pd);
    space=kproc_active_space(owner); backend=kaspace_backend(space);
    REQUIRE(kproc_reservation_info(pd,(VADDR)(uintptr_t)pd,sizeof(PD),&info)==E_OK);
    REQUIRE(kproc_reservation_hold(pd,(VADDR)(uintptr_t)pd,sizeof(PD),info.identity,ASPACE_PROT_USER,&base)==E_OK);
    REQUIRE(kproc_reservation_info(pd,(VADDR)(uintptr_t)env,2,&info)==E_OK);
    REQUIRE(kproc_reservation_hold(pd,(VADDR)(uintptr_t)env,2,info.identity,ASPACE_PROT_USER,&environment)==E_OK);
    REQUIRE(kproc_allocation_hold(pd,env,procmem_gen(env),0,2,ASPACE_PROT_USER,&raw)==E_OK);
    REQUIRE(!procmem_free(env));
    REQUIRE(kproc_reservation_info(pd,image_address,PAGE,&info)==E_OK);
    REQUIRE(kproc_reservation_hold(pd,image_address,PAGE,info.identity,ASPACE_PROT_USER|ASPACE_PROT_EXEC,&image)==E_OK);
    REQUIRE(kproc_reservation_info(pd,stack_page,PAGE,&info)==E_OK);
    REQUIRE(kproc_reservation_hold(pd,stack_page,PAGE,info.identity,ASPACE_PROT_USER|ASPACE_PROT_WRITE,&stack)==E_OK);
#if CONF_WITH_MMAP
    a=kproc_mmap(pd,0,PAGE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    b=kproc_mmap(pd,0,PAGE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    REQUIRE(a>0 && b==a+PAGE);
    REQUIRE(kproc_demand_page(pd,a,ASPACE_PROT_WRITE)==1);
    REQUIRE(aspace_copy_to_user(backend,a,&value,sizeof(value)));
    REQUIRE(kproc_reservation_info(pd,b,PAGE,&info)==E_OK);
    REQUIRE(kproc_reservation_hold(pd,b,PAGE,info.identity,ASPACE_PROT_USER|ASPACE_PROT_WRITE,&mapping)==E_OK);
    REQUIRE(!aspace_translate(backend,b,NULL,NULL));
    REQUIRE(kproc_munmap(pd,a,2*PAGE)==EACCDN && kproc_mprot(pd,a,2*PAGE,PROT_READ)==EACCDN);
    REQUIRE(aspace_copy_from_user(backend,&actual,a,sizeof(actual)) && actual==value);
#endif
    proc_dispose_process(owner);
    REQUIRE(!kproc_live_pd(owner));
    REQUIRE(kproc_allocation_duplicate(&raw,&raw_copy)==E_OK);
    REQUIRE(kproc_allocation_release(&raw));
    REQUIRE(kproc_reservation_duplicate(&stack,&copy)==E_OK);
    REQUIRE(kproc_reservation_release(&stack));
#if CONF_WITH_DEMAND_PAGING
    REQUIRE(aspace_fault_in(backend,stack_page,ASPACE_PROT_USER|ASPACE_PROT_WRITE));
#endif
    REQUIRE(aspace_copy_to_user(backend,stack_page,&value,sizeof(value)));
#if CONF_WITH_MMAP
    REQUIRE(kaspace_mmap_demand_page(space,b,ASPACE_PROT_WRITE)==1);
    REQUIRE(kproc_reservation_release(&mapping));
#endif
    REQUIRE(kproc_allocation_release(&raw_copy));
    REQUIRE(procmem_size(env)); /* mapping pin still owns the retired allocation */
    REQUIRE(kproc_reservation_release(&base));
    REQUIRE(kproc_reservation_release(&environment));
    REQUIRE(kproc_reservation_release(&copy));
    REQUIRE(kproc_reservation_release(&image));
    kproc_release(owner); kproc_release(source);
    procmem_stats(&after);
    REQUIRE(after.live_allocs==before.live_allocs && after.free_pages==before.free_pages &&
            after.bad_frees==before.bad_frees && kproc_count()==records);
    *fs_error_current()=error;
    ksched_cleanup_end(&scope); /* enclosing Syield owns the safe checkpoint */
    kcprintf("native allocation/reservation ownership and orphan reclamation: PASS\n");
}
