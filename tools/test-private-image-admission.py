#!/usr/bin/env python3
"""Known private-image owner gap: real loader move and value capture, not public fork."""
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
parser.add_argument('--require',action='store_true',help='require moved-image admission; currently exits 1')
parser.add_argument('--mode',choices=['normal','asan','fortified'],default='normal')
args=parser.parse_args()
# Reserved fixture status for the known admission rejection. It must differ
# from 1, the default failure status of ASan and of UBSan with halt_on_error,
# and from abort's 134, so that sanitizer failures are never mistaken for it.
KNOWN_REJECTION=77
control=r'''
int main(void)
{
    KPROC loader={0},source={0};PD loader_pd={0},source_pd={0};
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
    /* Production PE_LOAD staging, ownership transfer and segment protection.
     * The source's eventual UH_IMAGE is independently owned, not a loan. */
    source.aspace=NULL;selected_root=aspace_root(loader.aspace);
    image=kproc_load_alloc(loader.pd,source.pd,2*TEST_PAGE);CHECK(image);
    layout.nseg=2;layout.entry=image+16;layout.end=image+2*TEST_PAGE;
    layout.seg[0].vaddr=image;layout.seg[0].memsz=TEST_PAGE;layout.seg[0].flags=5;
    layout.seg[1].vaddr=image+TEST_PAGE;layout.seg[1].memsz=TEST_PAGE;layout.seg[1].flags=6;
    CHECK(kproc_set_moved_image(source.pd,&layout,loader.pd,image,2));
    CHECK(move_loaded_image(&source,destination,loader.pd));source.aspace=destination;
    CHECK(!loader.nheap && source.nheap==1 && source.heap[0].kind==UH_IMAGE);
    CHECK(!source.heap[0].pd && !source.load_parent && !source.lender && !source.give_back);
    CHECK(kproc_thread_is_sole(&source,&thread) && loader.active_space!=source.active_space);
    source.started=TRUE;
    policy_ok=kproc_image_policy_capture(&source,&policy)==E_OK;CHECK(policy_ok);
    moved_ok=kproc_clone_private_runtime(NULL,&source);
    printf("production private admission: empty sole source=%d; committed independently owned UH_IMAGE source=%d; value policy capture=%d\n",empty_ok,moved_ok,policy_ok);
    source.threads=NULL;kproc_list=NULL;
    kfree(source.heap);kfree(loader.heap);kaspace_put(source.active_space);kaspace_put(loader.active_space);
    CHECK(physical_live==ram && metadata_live==metadata && !physical_live && !metadata_live);
    puts("production moved-image control cleanup: exact baseline PASS");
    return moved_ok?0:KNOWN_REJECTION; /* --require general private-image eligibility */
}
'''
with tempfile.TemporaryDirectory(prefix='ptos-971-moved-') as tmp:
    work=Path(tmp)
    k=kconfiglib.Kconfig(warn_to_stderr=False)
    k.load_config('configs/pc-x86_64_defconfig')
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
    fixture=work/'control.c';fixture.write_text(source+control.replace('KNOWN_REJECTION',str(KNOWN_REJECTION)))
    flags=['-std=gnu90','-O2','-g','-Wall','-Wextra','-Werror','-fno-builtin','-fno-pie','-no-pie','-ffunction-sections','-fdata-sections','-iquote','include','-I'+str(work),'-Iinclude/machine/pc-x86_64','-Iinclude/arch/x86_64','-Ibios/machine/pc-x86_64','-Ibios/arch/x86_64','-iquote','bdos','-iquote','bios','-iquote','fs','-iquote','aes','-idirafter','include']
    if args.mode=='asan': flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    elif args.mode=='fortified': flags+=['-D_FORTIFY_SOURCE=3','-fstack-protector-strong']
    subprocess.run(shlex.split(os.environ.get('HOSTCC','cc'))+flags+[str(fixture),'bdos/kaspace.c','bios/machine/pc-x86_64/x32image.c','lib/ldso/startup.c','-Wl,--gc-sections','-o',str(work/'control')],check=True)
    result=subprocess.run([str(work/'control')])
    print('required moved-image admission exit='+str(result.returncode))
    if result.returncode not in (0,KNOWN_REJECTION):
        sys.exit('moved-image control failed with status %d (sanitizer or CHECK failure), not an admission result' % result.returncode)
    if args.require: sys.exit(0 if result.returncode==0 else 1)
    assert result.returncode==KNOWN_REJECTION, 'known ownership admission changed; review its proof'
    print('Known #971 moved-image ownership gap reproduced with exact cleanup; not fork acceptance.')
