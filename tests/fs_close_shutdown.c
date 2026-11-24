/* Explicit shutdown composes production sweep, SFT recovery and FAT reclaim.
 * Disk failures/cluster storage are injected; no CPU switch is modeled. */
#include "emutos.h"
#include "biosext.h" /* panic attributes must precede the reused definition. */
#define main close_backend_fixture_main
#include "fs_close.c"
#undef main
#include "../bdos/xshutdown.c"
static unsigned shutdown_errors;
int kprintf(const char *format,...)
{ CHECK(strstr(format,"Shutdown:")!=NULL); shutdown_errors++; return 0; }
int main(void)
{
    FS_CLOSE_REPORT result;
    FS_CLEANUP_TOKEN ticket;
    FS_CLEANUP_STATUS status;
    open_file();
    file.o_dmd=&media; file.o_disk.o_flag=O_UNLINKED; file.o_disk.o_strtcl=2;
    chain_fixture=TRUE; cluster_chain[2]=3; cluster_chain[3]=0xffff;
    buffer.b_dirty=FALSE; buffer.b_dm=&media;
    fail_disk=TRUE;
    CHECK(fs_close_bounded(NUMSTD,TRUE,&result)==EWRITF &&
          result.disposition==FS_CLOSE_TRANSFERRED && !freed);
    ticket=result.token;
    CHECK(file.o_disk.o_strtcl==2 && file.o_disk.o_reclaim_next==3 &&
          (file.o_disk.o_flag&O_RECLAIM_PENDING));
    close_files(); /* still failing: bounded, observable, ownership preserved */
    CHECK(shutdown_errors==1 && !freed && sft[0].f_use==1 && sft[0].f_cleanup_refs==1);
    CHECK(fs_cleanup_snapshot(&ticket,&status)==E_OK && status.state==FS_CLEANUP_PENDING);
    fail_disk=FALSE;
    close_files(); /* explicit shutdown can now finish the service-owned close */
    CHECK(freed==1 && !sft[0].f_use && !sft[0].f_cleanup_refs && !file.o_disk.o_strtcl &&
          !(file.o_disk.o_flag&O_RECLAIM_PENDING) && !cluster_chain[2] && !cluster_chain[3]);
    CHECK(fs_cleanup_snapshot(&ticket,&status)==E_OK && status.state==FS_CLEANUP_COMPLETE);
    CHECK(fs_cleanup_retry(&ticket,&result)==EIHNDL);
    close_files(); CHECK(freed==1 && shutdown_errors==1);
    chain_fixture=FALSE;
    open_file();
    CHECK(ixforce(0,NUMSTD,&owner)==E_OK && sft[0].f_use==2);
    fail_disk=TRUE;
    CHECK(fs_close_bounded(NUMSTD,TRUE,&result)==EWRITF && sft[0].f_cleanup_refs==1);
    fail_disk=FALSE;
    close_files();
    CHECK(freed==2 && !sft[0].f_use && !sft[0].f_cleanup_refs && owner.p_uft[0]<0);
    /* Service recovery must progress without stealing an independent staged
     * owner; final shutdown reports that outstanding owner instead. */
    open_file(); sft[0].f_use=2; sft[0].f_staged_refs=1; fail_disk=TRUE;
    CHECK(fs_close_bounded(NUMSTD,TRUE,&result)==EWRITF && sft[0].f_cleanup_refs==1);
    ticket=result.token; fail_disk=FALSE;
    close_files();
    CHECK(freed==2 && sft[0].f_use==1 && sft[0].f_staged_refs==1 &&
          !sft[0].f_cleanup_refs && shutdown_errors==2);
    CHECK(fs_cleanup_retry(&ticket,&result)==EIHNDL);
    {
        FS_FILE_REFERENCE staged=FS_FILE_REFERENCE_INIT;
        staged.handle=NUMSTD; staged.incarnation=sft[0].f_incarnation; staged.owned=TRUE;
        CHECK(fs_reference_close(&staged,TRUE,&result)==E_OK && !staged.owned &&
              freed==3 && !sft[0].f_staged_refs);
    }
    close_files(); CHECK(shutdown_errors==2 && freed==3);
    puts("Production shutdown recovers service reference without consuming staged owner: PASS (host)");
    puts("Production shutdown: bounded residual failure, unlinked FAT recovery, mixed live/service aliases and exactly once: PASS (host)");
    return 0;
}
