/* Production legacy resolver/close control; modeled disk and PROCESS lookup.
 * A staged SFT reference is not an independently closeable child binding. */
#define main reference_fixture_main
#include "fs_close.c"
#undef main
#include "kproc.h"
PD *kproc_live_pd(const KPROC *p) { return p==(KPROC *)&owner?&owner:NULL; }
void kproc_standard_file_refs(ULONG *r) { int i; memset(r,0,OPNFILES*sizeof(*r));for(i=0;i<NUMSTD;i++) if(owner.p_uft[i]>=NUMSTD) r[owner.p_uft[i]-NUMSTD]++; }
int main(int argc,char **argv)
{
 FS_FILE_REFERENCE snapshot=FS_FILE_REFERENCE_INIT;
 PD child; int i,violated; LONG closed; FS_CLOSE_REPORT report;
 (void)argv;
 memset(&child,0,sizeof child);
 for(i=0;i<NUMSTD;i++) child.p_uft[i]=owner.p_uft[i]=-1;
 open_file();
 CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,sft[0].f_incarnation,&snapshot)==E_OK);
 run=&child;
 closed=xclose(NUMSTD);
 run=&owner;
 printf("child raw close=%ld; parent binding accessible=%d; staged owner=%d; f_use=%d staged=%lu\n",(long)closed,getofd(NUMSTD)!=0,snapshot.owned,sft[0].f_use,(unsigned long)sft[0].f_staged_refs);
 /* Fork inheritance requires parent binding survive child's close. */
 violated=closed==E_OK && !getofd(NUMSTD) && snapshot.owned;
 CHECK(fs_reference_close(&snapshot,TRUE,&report)==E_OK);
 CHECK(!snapshot.owned && !sft[0].f_use && !sft[0].f_staged_refs);
 CHECK(violated);
 return argc>1?1:0;
}
