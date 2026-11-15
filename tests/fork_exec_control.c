/* Production root-association contract, hosted MMU/resource model.
 * New-PROCESS/THREAD construction is not same-PROCESS executable replacement. */
#define main existing_mapping_main
#include "shared_root_mapping.c"
#undef main
int main(int argc,char **argv)
{
    ASPACE old_backend={0},new_backend={0};
    KASPACE *oldspace=kaspace_adopt(&old_backend),*newspace=kaspace_adopt(&new_backend);
    KPROC process={0},*retained=NULL;
    KASPACE *attached=NULL;
    KTHREAD *next=NULL,*candidate=(KTHREAD *)(uintptr_t)16;
    BOOL replacement;
    (void)argv;
    CHECK(oldspace && newspace);
    process.pd=&child;process.references=1;process.active_space=oldspace;process.started=TRUE;
    CHECK(kproc_thread_attach_space(&process,oldspace,candidate,&retained,&attached,&next,TRUE));
    CHECK(retained==&process && attached==oldspace && process.references==2);
    CHECK(!kproc_thread_detach(&process,candidate,next));kaspace_put(attached);kproc_release(retained);
    retained=NULL;attached=NULL;next=NULL;
    replacement=kproc_thread_attach_space(&process,newspace,candidate,&retained,&attached,&next,TRUE);
    CHECK(!replacement && !retained && !attached && !next && !process.threads && process.references==1);
    CHECK(process.active_space==oldspace);
    kaspace_put(newspace);kaspace_put(oldspace);CHECK(old_backend.destroyed==1 && new_backend.destroyed==1);
    puts("production same-root attach succeeds; same-PROCESS replacement-root attach rejected without acquisition");
    return argc>1 ? !replacement : 0;
}
