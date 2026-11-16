#!/usr/bin/env python3
"""Private moved-image ownership: production loader, MMU clone and exact cleanup."""
import argparse
import os
import shlex
from pathlib import Path
import subprocess
import sys
import tempfile
import kconfiglib

root=Path(__file__).resolve().parents[1]
os.chdir(root)
os.environ['CONFIG_']=''
parser=argparse.ArgumentParser()
parser.add_argument('--require',action='store_true',help='require moved-image admission (retained for CI compatibility)')
parser.add_argument('--mode',choices=['normal','asan','fortified'],default='normal')
parser.add_argument('--preset',choices=['pc-x86_64','virt-arm-aspace','virt-m68k-aspace'],default='pc-x86_64')
args=parser.parse_args()
control=r'''
int kprintf(const char *format,...) { (void)format;CHECK(0);return 0; }
void halt(void) { abort(); }
#if defined(__m68k__)
LONG mcpu=40; /* hosted supported 68040 backend */
#endif
static void check_order(BOOL source_first)
{
    KPROC loader={0},source={0};PD loader_pd={0},source_pd={0};
    KPROC child={0},foreign={0};PD child_pd={0},foreign_pd={0};
    KTHREAD alien={0};KASPACE_MEMBER *foreign_members=NULL;
    KASPACE *copy=NULL;KASPACE_RESERVATION_INFO child_info;
    VADDR bp,env,dta;PADDR before,after;UWORD rights;
    USERPTR_T(DTA) mirror;
    KPROC descendant={0};PD descendant_pd={0};
    ULONG prefix,pins,live;
    KTHREAD thread={0};PROG_LAYOUT layout={0};VADDR image;
    ASPACE *destination;BOOL empty_ok,moved_ok,policy_ok;
    KPROC_IMAGE_POLICY policy;
    ULONG ram=physical_live,metadata=metadata_live;
    window_phys=(PADDR)(uintptr_t)window;free_pages=WINDOW_PAGES;
    loader.number=101;source.number=102;loader.references=source.references=1;
    loader.pd=&loader_pd;source.pd=&source_pd;loader.next=&source;kproc_list=&loader;
    loader.aspace=aspace_create();loader.active_space=kaspace_adopt(loader.aspace);
    destination=aspace_create();CHECK(loader.aspace && loader.active_space && destination);
    source.aspace=destination;source.active_space=kaspace_adopt(destination);
    CHECK(source.active_space);thread.process=&source;thread.space=source.active_space;source.threads=&thread;
    empty_ok=kproc_clone_private_runtime(NULL,&source);CHECK(empty_ok);
    foreign.pd=&foreign_pd;foreign.number=103;foreign.references=1;source.next=&foreign;
    alien.process=&foreign;alien.space=source.active_space;foreign.threads=&alien;
    CHECK(!kproc_clone_private_runtime(NULL,&source)); /* unpublished foreign THREAD */
    foreign.threads=NULL;foreign.pending_space=source.active_space;
    CHECK(!kproc_clone_private_runtime(NULL,&source));foreign.pending_space=NULL;
    CHECK(kaspace_member_new(source.active_space,foreign.number,&foreign,&foreign_members)==E_OK);
    CHECK(!kproc_clone_private_runtime(NULL,&source));kaspace_member_release(&foreign_members);
    alien.space=loader.active_space;thread.space=loader.active_space;
    CHECK(!kproc_clone_private_runtime(NULL,&source));thread.space=source.active_space;
    CHECK(kproc_clone_private_runtime(NULL,&source));source.next=NULL;
    /* Production PE_LOAD staging, ownership transfer and segment protection.
     * The source's eventual UH_IMAGE is independently owned, not a loan. */
    source.aspace=NULL;selected_root=aspace_root(loader.aspace);
    image=kproc_load_alloc(loader.pd,source.pd,2*TEST_PAGE,0);CHECK(image);
    layout.nseg=2;layout.entry=image+16;layout.end=image+2*TEST_PAGE;
    layout.seg[0].vaddr=image;layout.seg[0].memsz=TEST_PAGE;layout.seg[0].flags=5;
    layout.seg[1].vaddr=image+TEST_PAGE;layout.seg[1].memsz=TEST_PAGE;layout.seg[1].flags=6;
    CHECK(kproc_set_moved_image(source.pd,&layout,loader.pd,image,2));
    CHECK(move_loaded_image(&source,destination,loader.pd));source.aspace=destination;
    CHECK(!loader.nheap && source.nheap==1 && source.heap[0].kind==UH_IMAGE);
    CHECK(!source.heap[0].pd && !source.load_parent && !source.lender && !source.give_back);
    CHECK(kproc_thread_is_sole(&source,&thread) && loader.active_space!=source.active_space);
    source.started=TRUE;
    source.user_start=procmem_alloc(TEST_PAGE,0);source.env_start=procmem_alloc(TEST_PAGE,0);
    CHECK(source.user_start && source.env_start);
    source.user_end=source.user_start+TEST_PAGE;source.env_end=source.env_start+TEST_PAGE;
    source.tpa_gen=procmem_gen(source.user_start);source.env_gen=procmem_gen(source.env_start);
    CHECK(procmem_set_owner(source.user_start,source.pd) && procmem_set_owner(source.env_start,source.pd));
    CHECK(aspace_map_procmem(source.aspace,(VADDR)(uintptr_t)source.user_start,TEST_PAGE,ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    CHECK(aspace_map_procmem(source.aspace,(VADDR)(uintptr_t)source.env_start,TEST_PAGE,ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    source.dta=(DTAINFO *)(uintptr_t)(image+TEST_PAGE+32);
    memset((void *)(uintptr_t)procmem_phys_of((VADDR)(uintptr_t)source.user_start),0xa3,TEST_PAGE);
    memset((void *)(uintptr_t)procmem_phys_of((VADDR)(uintptr_t)source.env_start),0xb4,TEST_PAGE);
    kproc_set_dta(source.pd,(DTAINFO *)(uintptr_t)((VADDR)(uintptr_t)source.user_start+offsetof(PD,p_cmdlin)));
    CHECK(aspace_copy_from_user(source.aspace,&mirror,(VADDR)(uintptr_t)source.user_start+offsetof(PD,p_xdta),sizeof(mirror)));
    CHECK(mirror==PTR_TO_USERPTR_UNCHECKED((DTA *)source.dta));
    descendant.pd=&descendant_pd;descendant.number=105;descendant.references=1;
    CHECK(kproc_clone_inherit_identity(&descendant,&source));
    CHECK(descendant.dta==source.dta && descendant.dta!=(DTAINFO *)descendant_pd.p_cmdlin);
    CHECK(!descendant.dtas[0].va && !descendant.dtas[0].stamp);
    kproc_release(descendant.parent_identity);descendant.parent_identity=NULL;
    source.stack_top=USER_STACK_TOP-16;source.stack_low=USER_STACK_TOP-TEST_PAGE;
    source.entry=layout.entry;

    policy_ok=kproc_image_policy_capture(&source,&policy)==E_OK;CHECK(policy_ok);
    moved_ok=kproc_clone_private_runtime(NULL,&source);
    printf("production private admission: empty sole source=%d; committed independently owned UH_IMAGE source=%d; value policy capture=%d\n",empty_ok,moved_ok,policy_ok);
    source.heap[0].kind=UH_LOADED;CHECK(!kproc_clone_private_runtime(NULL,&source));
    source.heap[0].kind=UH_IMAGE;source.give_back=TRUE;
    CHECK(!kproc_clone_private_runtime(NULL,&source));source.give_back=FALSE;
    pins=procmem_pins(source.user_start)+procmem_pins(source.env_start);
    CHECK(moved_ok && kaspace_clone(source.active_space,&copy)==E_OK);
    CHECK(pins==procmem_pins(source.user_start)+procmem_pins(source.env_start));
    child.pd=&child_pd;child.references=1;child.number=104;source.next=&child;
    for(prefix=1;prefix<=2;prefix++) {
        live=metadata_live;fail_metadata=allocation_call+prefix;
        CHECK(kproc_clone_prepare_private_image(&child,&source)==ENSMEM);reset_failure();
        CHECK(!child.private_image && !child.heap && metadata_live==live);
    }
    child.heap=kalloc(sizeof(*child.heap));CHECK(child.heap);child.heap_capacity=1;
    CHECK(kproc_clone_prepare_private_image(&child,&source)==EACCDN);
    kfree(child.heap);child.heap=NULL;child.heap_capacity=0;
    CHECK(kproc_clone_private_runtime(&child,&source));
    CHECK(!child.nheap && !child.has_image && !child.entry && !child.stack_top);
    CHECK(child.private_image && child.private_image->heap!=source.heap);
    private_image_commit(&child);child.active_space=copy;child.aspace=kaspace_backend(copy);child.started=TRUE;
    CHECK(child.nheap==1 && child.heap!=source.heap && !child.heap[0].pd);
    CHECK(child.heap[0].reservation && child.heap[0].reservation!=source.heap[0].reservation);
    CHECK(child.layout.seg[0].src==NULL && !child.layout.seg[0].filesz);
    CHECK(child.stack_top==source.stack_top && child.stack_low==source.stack_low && child.entry==source.entry);
    CHECK(kproc_private_image_addresses(&child,&bp,&env,&dta));
    CHECK(kproc_private_environment(child.pd)==env);
    CHECK(aspace_translate(child.aspace,bp,&after,NULL) && *(UBYTE *)(uintptr_t)after==0xa3);
    CHECK(aspace_translate(child.aspace,env,&after,NULL) && *(UBYTE *)(uintptr_t)after==0xb4);
    CHECK(bp==(VADDR)(uintptr_t)source.user_start && env==(VADDR)(uintptr_t)source.env_start && dta==(VADDR)(uintptr_t)source.dta);
    CHECK(child.dta==source.dta);
    kproc_set_dta(child.pd,(DTAINFO *)(uintptr_t)(image+TEST_PAGE+32));
    CHECK(aspace_copy_from_user(child.aspace,&mirror,bp+offsetof(PD,p_xdta),sizeof(mirror)));
    CHECK(mirror==PTR_TO_USERPTR_UNCHECKED((DTA *)child.dta));
    CHECK(aspace_copy_from_user(source.aspace,&mirror,bp+offsetof(PD,p_xdta),sizeof(mirror)));
    CHECK(mirror==PTR_TO_USERPTR_UNCHECKED((DTA *)source.dta) && child.dta!=source.dta);
    /* Changing a read-only mirror never grants WRITE or changes authority. */
    CHECK(aspace_protect_private(child.aspace,bp,TEST_PAGE,ASPACE_PROT_USER));
    kproc_set_dta(child.pd,(DTAINFO *)(uintptr_t)dta);
    CHECK(child.dta==source.dta && aspace_logical_protection(child.aspace,bp,&rights) && !(rights&ASPACE_PROT_WRITE));
    CHECK(aspace_protect_private(child.aspace,bp,TEST_PAGE,ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    CHECK(kproc_reservation_info(child.pd,bp,TEST_PAGE,&child_info)==E_OK);
    CHECK(kproc_reservation_info(child.pd,env,TEST_PAGE,&child_info)==E_OK);
    CHECK(kproc_reservation_info(child.pd,image+TEST_PAGE,TEST_PAGE,&child_info)==E_OK);
    CHECK(child_info.identity==child.heap[0].reservation && (child_info.rights&ASPACE_PROT_WRITE));
    CHECK(aspace_logical_protection(child.aspace,image,&rights) && !(rights&ASPACE_PROT_WRITE));
    CHECK(aspace_cow_fault(child.aspace,image,ASPACE_PROT_USER|ASPACE_PROT_WRITE)!=1);
    CHECK(aspace_translate(source.aspace,image+TEST_PAGE,&before,NULL));
    CHECK(aspace_cow_fault(child.aspace,image+TEST_PAGE,ASPACE_PROT_USER|ASPACE_PROT_WRITE)==1);
    CHECK(aspace_translate(child.aspace,image+TEST_PAGE,&after,NULL) && before!=after);
    CHECK(aspace_cow_fault(source.aspace,image+TEST_PAGE,ASPACE_PROT_USER|ASPACE_PROT_WRITE)==1);
    source.threads=NULL;
    if(source_first) {kaspace_put(source.active_space);source.active_space=NULL;source.aspace=NULL;procmem_free_owned(source.pd,NULL);
        CHECK(aspace_translate(child.aspace,bp,&after,NULL) && *(UBYTE *)(uintptr_t)after==0xa3);
        CHECK(aspace_translate(child.aspace,env,&after,NULL) && *(UBYTE *)(uintptr_t)after==0xb4);
        CHECK(kproc_reservation_info(child.pd,bp,TEST_PAGE,&child_info)==E_OK);
        CHECK(kproc_reservation_info(child.pd,env,TEST_PAGE,&child_info)==E_OK);
    }
    CHECK(kproc_reservation_info(child.pd,image+TEST_PAGE,TEST_PAGE,&child_info)==E_OK);
    kfree(child.heap);kfree(child.private_image);kaspace_put(copy);
    kproc_list=NULL;kfree(source.heap);kfree(loader.heap);
    kaspace_put(source.active_space);procmem_free_owned(source.pd,NULL);kaspace_put(loader.active_space);
    CHECK(physical_live==ram && metadata_live==metadata && !physical_live && !metadata_live);
    puts("production moved-image control cleanup: exact baseline PASS");
}
int main(void)
{
    check_order(FALSE);check_order(TRUE);return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='ptos-971-moved-') as tmp:
    work=Path(tmp)
    k=kconfiglib.Kconfig(warn_to_stderr=False)
    k.load_config('configs/'+args.preset+'_defconfig')
    arch,machine={'pc-x86_64':('x86_64','pc-x86_64'),'virt-arm-aspace':('arm','virt-arm'),'virt-m68k-aspace':('m68k','virt-m68k')}[args.preset]
    for name in ('CONF_WITH_NATIVE_SCHED','CONF_WITH_SHARED_VM_PEXEC_TEST','CONF_WITH_ELF_SHARED','CONF_WITH_MMAP','CONF_WITH_PRIVATE_CLONE','CONF_WITH_IMAGE_POLICY'):
        k.syms[name].set_value(2)
        assert k.syms[name].str_value=='y',name
    k.syms['CONF_WITH_PRIVATE_CLONE_TEST'].set_value(0)
    k.write_config(str(work/'config'))
    subprocess.run([sys.executable,'tools/genconfig.py','--kconfig','Kconfig','--config',str(work/'config'),'--header',str(work/'autoconf.h'),'--makefile',str(work/'auto.conf')],check=True)
    source=(root/'tests/kproc_shared_interp.c').read_text()
    source=source.replace('#include "procmem_borrow.c"','#include "'+str(root/'tests/procmem_borrow.c')+'"')
    source=source.replace('#include "../bdos/kproc.c"','#include "'+str(root/'bdos/kproc.c')+'"')
    source=source.replace('\nint main(void)\n','\nint shared_interp_original_main(void)\n')
    fixture=work/'control.c';fixture.write_text(source+control)
    flags=['-std=gnu90','-O2','-g','-Wall','-Wextra','-Werror','-fno-builtin','-fno-pie','-no-pie','-ffunction-sections','-fdata-sections','-iquote','include','-I'+str(work),'-Iinclude/machine/'+machine,'-Iinclude/arch/'+arch,'-Ibios/machine/'+machine,'-Ibios/arch/'+arch,'-iquote','bdos','-iquote','bios','-iquote','fs','-iquote','aes','-idirafter','include']
    if arch!='x86_64': flags+=['-DBORROW_GENERIC']
    if arch=='m68k': flags+=['-D__m68k__']
    if args.mode=='asan': flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    elif args.mode=='fortified': flags+=['-D_FORTIFY_SOURCE=3','-fstack-protector-strong']
    # Host architecture differs from the modeled 32-bit target. Enable only
    # the production layout implementation's target-selection guard.
    generic_layout=work/'progimage.c'
    generic_layout.write_text((root/'bios/progimage.c').read_text().replace(
        '#if CONF_WITH_USER_ASPACE && !defined(__x86_64__)', '#if CONF_WITH_USER_ASPACE'))
    subprocess.run(shlex.split(os.environ.get('HOSTCC','cc'))+flags+[str(fixture),'bdos/kaspace.c']+(['bios/machine/pc-x86_64/x32image.c'] if arch=='x86_64' else [str(generic_layout)])+['lib/ldso/startup.c','-Wl,--gc-sections','-o',str(work/'control')],check=True)
    result=subprocess.run([str(work/'control')])
    print('required moved-image admission exit='+str(result.returncode))
    if result.returncode:
        sys.exit(result.returncode)
    print('Private moved-image ownership, COW, foreign associations and both cleanup orders: PASS')
