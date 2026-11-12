/* Actual owner/MMU tests entered from a managed user Syield. No user ABI. */
#include "emutos.h"
#include "biosext.h"
#include "ksched.h"
#include "kproc.h"
#include "kaspace.h"
#include "proc_clone.h"
#include "kthread.h"
#include "mem.h"
#include "procmem.h"
#include "mman.h"
#include "fs.h"
#include "proc.h"
#include "bdosstub.h"
#include "region_selftest.h"
#include "kprint.h"
#include "gemerror.h"
#include "string.h"
#include "progimage.h"
extern PD *run;
#define REQUIRE(c) do { if (!(c)) panic("native region ownership: %s",#c); } while (0)
#define PAGE USER_PAGE_SIZE
#define REGION_FILE_TEST (CONF_WITH_MMAP && !CONF_WITH_PLUGGABLE_FS && \
    (CONF_WITH_KCALL || CONF_WITH_VDI_SCALABLE_FONTS || CONF_WITH_ELF_SHARED))
#if REGION_FILE_TEST
static UQUAD file_reference_count(void)
{
    UQUAD count=0;
    ULONG i;
    for (i=0;i<OPNFILES;i++) count+=sft[i].f_use;
    return count;
}
#endif


static void held_staging_discard(KPROC *source)
{
    KPROC *child=NULL;
    PD *pd;
    void *environment;
    KPROC_ALLOCATION_HOLD bp_hold={{0}},env_hold={{0}};
    KASPACE_RESERVATION_INFO info;
    KASPACE_RESERVATION_HOLD first={0},last={0},bp_view={0},env_view={0};
    ASPACE *backend=kaspace_backend(kproc_active_space(source));
    VADDR va,independent;
    ULONG sentinel=0x894beefUL,readback=0;
    REQUIRE(proc_create_clone_process(source,0,&child)==E_OK && child);
    pd=kproc_live_pd(child); REQUIRE(pd);
    REQUIRE(kproc_borrow(run,pd));
    environment=USERPTR_TO_PTR(pd->p_env); REQUIRE(kproc_borrow(run,environment));
    set_owner(pd,run); set_owner(environment,run);
    set_owner(environment,NULL);
    REQUIRE(!kproc_prepare_discard(child) && procmem_owner(pd)==run);
    set_owner(environment,run); /* failed pair preflight changed neither owner */
    REQUIRE(kproc_reservation_info(run,(VADDR)(uintptr_t)pd,sizeof(PD),&info)==EACCDN);
    kproc_hand_over(run,pd);
    REQUIRE(kproc_reservation_info(run,(VADDR)(uintptr_t)pd,sizeof(PD),&info)==E_OK);
    REQUIRE(kproc_reservation_hold(run,(VADDR)(uintptr_t)pd,sizeof(PD),info.identity,ASPACE_PROT_USER,&bp_view)==E_OK);
    REQUIRE(kproc_reservation_info(run,(VADDR)(uintptr_t)environment,2,&info)==E_OK);
    REQUIRE(kproc_reservation_hold(run,(VADDR)(uintptr_t)environment,2,info.identity,ASPACE_PROT_USER,&env_view)==E_OK);
    REQUIRE(kproc_allocation_hold(run,pd,procmem_gen(pd),0,sizeof(PD),ASPACE_PROT_USER,&bp_hold)==E_OK);
    REQUIRE(kproc_allocation_hold(run,environment,procmem_gen(environment),0,2,ASPACE_PROT_USER,&env_hold)==E_OK);
    va=kproc_load_alloc(run,pd,2*PAGE); REQUIRE(va);
    kproc_set_creator(pd,run);
    REQUIRE(kproc_reservation_info(run,va,16,&info)==E_OK);
    REQUIRE(kproc_reservation_hold(run,va,16,info.identity,ASPACE_PROT_USER,&first)==E_OK);
    REQUIRE(kproc_reservation_duplicate(&first,&last)==E_OK);
    pd->p_env=0; /* caller-writable PD cannot replace the recorded allocation owner */
    proc_discard_scheduled(pd);
    REQUIRE(!kproc_live_pd(child) && aspace_translate(backend,va,NULL,NULL));
    REQUIRE(procmem_size(pd) && procmem_size(environment) && !procmem_owner(pd) && !procmem_owner(environment));
    REQUIRE(!procmem_set_owner(pd,run));
    REQUIRE(kproc_allocation_release(&bp_hold) && procmem_size(pd));
    REQUIRE(kproc_allocation_release(&env_hold) && procmem_size(environment));
    REQUIRE(kproc_reservation_release(&bp_view) && !procmem_size(pd));
    REQUIRE(kproc_reservation_release(&env_view) && !procmem_size(environment));
    independent=kproc_uheap_alloc(run,2*PAGE);
    REQUIRE(independent && (independent+2*PAGE<=va || independent>=va+2*PAGE));
#if CONF_WITH_DEMAND_PAGING
    REQUIRE(kproc_demand_page(run,independent,ASPACE_PROT_WRITE)==1);
#else
    {
        UWORD protection;
        REQUIRE(kproc_demand_page(run,independent,ASPACE_PROT_WRITE)==0 &&
                aspace_translate(backend,independent,NULL,&protection) &&
                (protection&(ASPACE_PROT_USER|ASPACE_PROT_WRITE))==(ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    }
#endif
    REQUIRE(aspace_copy_to_user(backend,independent,&sentinel,sizeof(sentinel)));
    REQUIRE(!kproc_ref_has_basepage(child));
    REQUIRE(kproc_reservation_release(&first) && aspace_translate(backend,va,NULL,NULL));
    REQUIRE(kproc_reservation_release(&last) && !aspace_translate(backend,va,NULL,NULL) &&
            !aspace_translate(backend,va+PAGE,NULL,NULL));
    REQUIRE(aspace_copy_from_user(backend,&readback,independent,sizeof(readback)) && readback==sentinel);
    REQUIRE(kproc_uheap_alloc(run,2*PAGE)==va && kproc_uheap_free(run,va)==E_OK &&
            kproc_uheap_free(run,independent)==E_OK);
    kproc_release(child);
}

static void held_clone_compat_retirement(KPROC *source)
{
    KPROC *child=NULL;
    KTHREAD *thread;
    PD *pd;
    KASPACE *space=kproc_active_space(source), *adopted;
    ASPACE *backend=kaspace_backend(space);
    KASPACE_RESERVATION_INFO info;
    KASPACE_RESERVATION_HOLD hold={0};
    VADDR address;
    REQUIRE(proc_create_clone_process(source,0,&child)==E_OK && child);
    pd=kproc_live_pd(child); address=(VADDR)(uintptr_t)pd;
    thread=kthread_construct_identity(child,space); REQUIRE(thread);
    REQUIRE(kproc_clone_prepare_compat(child,thread,space));
    REQUIRE(aspace_regrant(backend,address,sizeof(PD),ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    adopted=kaspace_share(space); REQUIRE(adopted);
    REQUIRE(kproc_clone_adopt_space(child,thread,adopted));
    REQUIRE(kproc_reservation_info(pd,address,sizeof(PD),&info)==E_OK);
    REQUIRE(kproc_reservation_hold(pd,address,sizeof(PD),info.identity,ASPACE_PROT_USER,&hold)==E_OK);
    proc_discard_scheduled(pd); /* canonical finalizer survives pending membership */
    REQUIRE(kproc_ref_has_basepage(child) && procmem_owner(pd)==child);
    REQUIRE(kthread_stop(thread) && kthread_reap(thread));
    REQUIRE(!kproc_ref_has_basepage(child) && aspace_translate(backend,address,NULL,NULL));
    REQUIRE(kproc_reservation_release(&hold) && !aspace_translate(backend,address,NULL,NULL));
    kproc_release(child);
}

#if CONF_WITH_DEMAND_PAGING
/* A discarded staged image has lost its live heap entry, but its exact
 * retirement fence must still exclude a new stack reservation and guard. */
static void held_stack_growth(KPROC *source,PD *pd,VADDR image_address)
{
    KPROC *staging=NULL;
    KASPACE_RESERVATION_INFO info;
    KASPACE_RESERVATION_HOLD hold={0};
    ASPACE *backend=kproc_user_aspace(pd);
    VADDR va,prefix=0;
    LONG old,grown,guard_only;
    /* First-fit staging must lie ABOVE the existing image; otherwise its
     * live image metadata correctly refuses growth before the fence test. */
    if (image_address>USER_HEAP_BASE) {
        prefix=kproc_uheap_alloc(pd,(LONG)(image_address-USER_HEAP_BASE));
        REQUIRE(prefix==USER_HEAP_BASE);
    }
    REQUIRE(proc_create_clone_process(source,0,&staging)==E_OK && staging);
    va=kproc_load_alloc(pd,kproc_live_pd(staging),2*PAGE);REQUIRE(va>=image_address+2*PAGE);
    kproc_set_creator(kproc_live_pd(staging),pd);
    REQUIRE(kproc_reservation_info(pd,va,2*PAGE,&info)==E_OK);
    REQUIRE(kproc_reservation_hold(pd,va,2*PAGE,info.identity,ASPACE_PROT_USER|ASPACE_PROT_WRITE,&hold)==E_OK);
    proc_dispose_process(staging);kproc_release(staging);
    old=kproc_psetlimit(pd,PSETLIMIT_STACK,-1);
    grown=(LONG)(USER_STACK_TOP-va-PAGE);
    guard_only=(LONG)(USER_STACK_TOP-va-2*PAGE);
    REQUIRE(old>0 && guard_only>old && grown>guard_only);
    {
        LONG result=kproc_psetlimit(pd,PSETLIMIT_STACK,grown);
        if (result!=EACCDN) kcprintf("held stack growth va=%lx old=%lx grown=%lx result=%ld\n",
                (unsigned long)va,(unsigned long)old,(unsigned long)grown,(long)result);
        REQUIRE(result==EACCDN && kproc_psetlimit(pd,PSETLIMIT_STACK,-1)==old);
    }
    REQUIRE(kproc_psetlimit(pd,PSETLIMIT_STACK,guard_only)==EACCDN &&
            kproc_psetlimit(pd,PSETLIMIT_STACK,-1)==old);
    REQUIRE(aspace_translate(backend,va+PAGE,NULL,NULL));
    REQUIRE(kproc_reservation_release(&hold) && !aspace_translate(backend,va+PAGE,NULL,NULL));
    /* Successful growth after release proves this was the retired fence,
     * rather than a live heap/stack collision. No page is materialized. */
    REQUIRE(kproc_psetlimit(pd,PSETLIMIT_STACK,grown)==old &&
            kproc_psetlimit(pd,PSETLIMIT_STACK,-1)==grown &&
            !aspace_translate(backend,va+PAGE,NULL,NULL));
    REQUIRE(kproc_psetlimit(pd,PSETLIMIT_STACK,old)==grown &&
            kproc_psetlimit(pd,PSETLIMIT_STACK,-1)==old);
    if (prefix) REQUIRE(kproc_uheap_free(pd,prefix)==E_OK);
}
#endif

void kproc_region_selftest(void)
{
    static BOOL tested;
    KSCHED_CLEANUP_SCOPE scope={0};
    FS_ERROR_STATE error;
    KPROC *source, *owner=NULL, *orphan_stage=NULL;
    PD *pd;
    void *env;
    ASPACE *backend;
    KASPACE *space;
    PROCMEM_STATS before, after;
    KPROC_ALLOCATION_HOLD raw={{0}}, raw_copy={{0}};
    KASPACE_RESERVATION_INFO info;
    KASPACE_RESERVATION_HOLD base={0}, environment={0}, environment_other={0}, stack={0}, copy={0}, image={0};
    KASPACE_RESERVATION_HOLD staged={0},broad={0},data={0},orphan_hold={0};
    PROG_LAYOUT layout;
    VADDR image_address,orphan_address;
    ULONG records, value=0x8941234UL;
    VADDR stack_page=USER_STACK_TOP-PAGE;
#if CONF_WITH_PRIVATE_CLONE
    ULONG cow_frames=aspace_cow_frames(),cow_references=aspace_cow_references();
#endif
#if CONF_WITH_MMAP
    LONG a,b;
    ULONG actual=0;
    KASPACE_RESERVATION_HOLD mapping={0};
#endif
#if REGION_FILE_TEST
    LONG file,descriptor;
    UWORD file_magic=0;
    UQUAD file_references;
    KASPACE_RESERVATION_HOLD file_hold={0};
#endif
    if (tested || !ksched_exit_thread(run) || ksched_cleanup_active() || ksched_cancellation_pending()) return;
    tested=TRUE; /* prevent reentry from any future teardown callback */
    error=*fs_error_current();
    ksched_cleanup_begin(&scope);
#if REGION_FILE_TEST
    fileback_reap();file_references=file_reference_count();
#endif
    procmem_stats(&before); records=kproc_count();
    source=kproc_retain(run); REQUIRE(source);
    held_staging_discard(source);
    held_clone_compat_retirement(source);
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
    REQUIRE(aspace_map_procmem(backend,(VADDR)(uintptr_t)env,2,ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    REQUIRE(aspace_map_procmem(backend,(VADDR)(uintptr_t)env,8,ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    REQUIRE(kproc_reservation_info(pd,(VADDR)(uintptr_t)pd,sizeof(PD),&info)==E_OK);
    REQUIRE(kproc_reservation_hold(pd,(VADDR)(uintptr_t)pd,sizeof(PD),info.identity,ASPACE_PROT_USER,&base)==E_OK);
    REQUIRE(kproc_reservation_info(pd,(VADDR)(uintptr_t)env,2,&info)==E_OK);
    REQUIRE(kproc_reservation_hold(pd,(VADDR)(uintptr_t)env,2,info.identity,ASPACE_PROT_USER,&environment)==E_OK);
    REQUIRE(kproc_reservation_duplicate(&environment,&environment_other)==E_OK);
    REQUIRE(kproc_allocation_hold(pd,env,procmem_gen(env),0,2,ASPACE_PROT_USER,&raw)==E_OK);
    REQUIRE(!procmem_free(env));
    /* Broad READ first must not hide RX/RW subrange permissions. */
    REQUIRE(kproc_reservation_info(pd,image_address,2*PAGE,&info)==E_OK);
    REQUIRE(kproc_reservation_hold(pd,image_address,2*PAGE,info.identity,ASPACE_PROT_USER,&broad)==E_OK);
    REQUIRE(kproc_reservation_info(pd,image_address,PAGE,&info)==E_OK);
    REQUIRE(kproc_reservation_hold(pd,image_address,PAGE,info.identity,ASPACE_PROT_USER|ASPACE_PROT_EXEC,&image)==E_OK);
    REQUIRE(kproc_reservation_info(pd,image_address+PAGE,PAGE,&info)==E_OK);
    REQUIRE(kproc_reservation_hold(pd,image_address+PAGE,PAGE,info.identity,ASPACE_PROT_USER|ASPACE_PROT_WRITE,&data)==E_OK);
    REQUIRE(kproc_reservation_release(&broad));
    /* And the opposite order must preserve the same rights. */
    REQUIRE(kproc_reservation_info(pd,image_address,2*PAGE,&info)==E_OK);
    REQUIRE(kproc_reservation_hold(pd,image_address,2*PAGE,info.identity,ASPACE_PROT_USER,&broad)==E_OK);
    REQUIRE(kproc_reservation_release(&broad));
    REQUIRE(kproc_reservation_info(pd,stack_page,PAGE,&info)==E_OK);
    REQUIRE(kproc_reservation_hold(pd,stack_page,PAGE,info.identity,ASPACE_PROT_USER|ASPACE_PROT_WRITE,&stack)==E_OK);
#if CONF_WITH_DEMAND_PAGING
    held_stack_growth(source,pd,image_address);
#endif
#if CONF_WITH_MMAP
    a=kproc_mmap(pd,0,PAGE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    b=kproc_mmap(pd,0,PAGE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    REQUIRE(a>0 && b==a+PAGE);
    {
        LONG old=kproc_psetlimit(pd,PSETLIMIT_STACK,-1);
        LONG overlap=(LONG)(USER_STACK_TOP-(VADDR)b);
        REQUIRE(overlap>old && kproc_psetlimit(pd,PSETLIMIT_STACK,overlap)==ENSMEM &&
                kproc_psetlimit(pd,PSETLIMIT_STACK,-1)==old);
    }
    REQUIRE(kproc_demand_page(pd,a,ASPACE_PROT_WRITE)==1);
    REQUIRE(aspace_copy_to_user(backend,a,&value,sizeof(value)));
    REQUIRE(kproc_reservation_info(pd,b,PAGE,&info)==E_OK);
    REQUIRE(kproc_reservation_hold(pd,b,PAGE,info.identity,ASPACE_PROT_USER|ASPACE_PROT_WRITE,&mapping)==E_OK);
    REQUIRE(!aspace_translate(backend,b,NULL,NULL));
    REQUIRE(kproc_munmap(pd,a,2*PAGE)==EACCDN && kproc_mprot(pd,a,2*PAGE,PROT_READ)==EACCDN);
    REQUIRE(aspace_copy_from_user(backend,&actual,a,sizeof(actual)) && actual==value);
#endif
#if REGION_FILE_TEST
    descriptor=kdos_fopen((long)(uintptr_t)
#if CONF_WITH_X86_64_MEMTEST
                         "C:\\X32HELLO.TOS",
#else
                         "C:\\RUNTESTS.TOS",
#endif
                         0);
#if CONF_WITH_X86_64_MEMTEST
    if (descriptor==EFILNF) descriptor=kdos_fopen((long)(uintptr_t)"C:\\RUNTESTS.TOS",0);
#endif
    REQUIRE(descriptor>=NUMSTD);
    file=kproc_mmap(pd,0,PAGE,PROT_READ,MAP_PRIVATE,descriptor,0);REQUIRE(file>0);
    REQUIRE(kproc_reservation_info(pd,file,PAGE,&info)==E_OK);
    REQUIRE(kproc_reservation_hold(pd,file,PAGE,info.identity,ASPACE_PROT_USER,&file_hold)==E_OK);
    REQUIRE(!aspace_translate(backend,file,NULL,NULL) && kdos_fclose(descriptor)==E_OK);
#endif
#if CONF_WITH_MMAP && CONF_WITH_PRIVATE_CLONE_TEST && (defined(MACHINE_PC_X86_64) || defined(MACHINE_VIRT_ARM) || defined(MACHINE_VIRT_M68K))
    {
        KASPACE *child_space=NULL;
        ULONG changed=0x98765432UL;
        REQUIRE(kaspace_clone(space,&child_space)==E_OK && child_space);
        aspace_cow_test_fail_allocation(TRUE);
        REQUIRE(aspace_cow_fault(kaspace_backend(child_space),a,ASPACE_PROT_WRITE)==2);
        aspace_cow_test_fail_allocation(FALSE);
        REQUIRE(aspace_cow_fault(kaspace_backend(child_space),a,ASPACE_PROT_WRITE)==1 &&
                aspace_copy_to_user(kaspace_backend(child_space),a,&changed,sizeof(changed)) &&
                aspace_copy_from_user(backend,&actual,a,sizeof(actual)) && actual==value);
        kaspace_put(child_space);
        {
            PADDR before_promotion,after_promotion;
            UWORD protection;
            int promoted;
            REQUIRE(aspace_translate(backend,a,&before_promotion,NULL));
            promoted=aspace_cow_fault(backend,a,ASPACE_PROT_WRITE);
            REQUIRE((promoted==0 || promoted==1) && aspace_translate(backend,a,&after_promotion,&protection) &&
                    (protection&ASPACE_PROT_WRITE) && before_promotion==after_promotion);
        }
    }
#endif
    REQUIRE(proc_create_clone_process(source,0,&orphan_stage)==E_OK && orphan_stage);
    orphan_address=kproc_load_alloc(pd,kproc_live_pd(orphan_stage),2*PAGE); REQUIRE(orphan_address);
    kproc_set_creator(kproc_live_pd(orphan_stage),pd);
    REQUIRE(kproc_reservation_info(pd,orphan_address,16,&info)==E_OK);
    REQUIRE(kproc_reservation_hold(pd,orphan_address,16,info.identity,ASPACE_PROT_USER,&orphan_hold)==E_OK);
    REQUIRE(kaspace_retire_borrow(space,(VADDR)(uintptr_t)env,procmem_size(env)));
    REQUIRE(kaspace_retire_borrow(space,(VADDR)(uintptr_t)env,2));
    REQUIRE(kaspace_retire_borrow(space,(VADDR)(uintptr_t)env,8));
    proc_dispose_process(owner);
    REQUIRE(!kproc_live_pd(owner));
    REQUIRE(aspace_translate(backend,orphan_address,NULL,NULL));
    proc_dispose_process(orphan_stage); kproc_release(orphan_stage);
    REQUIRE(kproc_reservation_release(&orphan_hold) &&
            !aspace_translate(backend,orphan_address,NULL,NULL) &&
            !aspace_translate(backend,orphan_address+PAGE,NULL,NULL));
    REQUIRE(!kaspace_reservation_info(space,(VADDR)(uintptr_t)env+PAGE-2,2,&info));
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
#if REGION_FILE_TEST
    REQUIRE(kaspace_mmap_demand_page(space,file,ASPACE_PROT_USER)==1 &&
            aspace_copy_from_user(backend,&file_magic,file,sizeof(file_magic)) && file_magic);
    REQUIRE(kproc_reservation_release(&file_hold));
#endif
    REQUIRE(kproc_allocation_release(&raw_copy));
    REQUIRE(procmem_size(env)); /* mapping pin still owns the retired allocation */
    REQUIRE(kproc_reservation_release(&base));
    REQUIRE(kproc_reservation_release(&environment) && procmem_size(env));
    REQUIRE(kproc_reservation_release(&environment_other) && !procmem_size(env) &&
            !aspace_translate(backend,(VADDR)(uintptr_t)env,NULL,NULL));
    REQUIRE(kproc_reservation_release(&copy));
    REQUIRE(kproc_reservation_release(&image));
    REQUIRE(kproc_reservation_release(&data));
    kproc_release(owner); kproc_release(source);
#if REGION_FILE_TEST
    fileback_reap();REQUIRE(file_reference_count()==file_references);
#endif
#if CONF_WITH_PRIVATE_CLONE
    REQUIRE(aspace_cow_frames()==cow_frames && aspace_cow_references()==cow_references);
#endif
    procmem_stats(&after);
    REQUIRE(after.live_allocs==before.live_allocs && after.free_pages==before.free_pages &&
            after.bad_frees==before.bad_frees && kproc_count()==records);
    *fs_error_current()=error;
    ksched_cleanup_end(&scope); /* enclosing Syield owns the safe checkpoint */
    kcprintf("native allocation/reservation ownership and orphan reclamation: PASS\n");
}
