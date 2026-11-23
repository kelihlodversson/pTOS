/* Loader fixtures model SFT ownership; the real close service/backend is
 * exercised by test-fs-close.py and the non-CUIS cleanup fixture. */
#include "emutos.h"
#include "fs.h"
#include "fs_close.h"
#include "string.h"
#include "kproc.h"

BOOL __attribute__((weak)) kproc_begin_file_cleanup(PD *pd) { (void)pd; return TRUE; }
SBYTE __attribute__((weak)) get_default_handle(int index) { (void)index; return -1; }

LONG __attribute__((weak)) fs_close_bounded(WORD h, BOOL dispose, FS_CLOSE_REPORT *out)
{
    UWORD attempt;
    volatile BOOL returned;
    LONG first=0,error;
    memset(out,0,sizeof(*out));
    for (attempt=0;attempt<=BLKDEVNUM;attempt++) {
        returned=FALSE;
        error=DISK_CALL(__extension__ ({ long r=xclose(h); returned=TRUE; r; }));
        if (!attempt) first=error;
        if (returned) break;
    }
    out->error=first;
    out->disposition=returned ? FS_CLOSE_CONSUMED : dispose ? FS_CLOSE_TRANSFERRED : FS_CLOSE_RETAINED;
    return first;
}
void __attribute__((weak)) kproc_note_file_cleanup(PD *p,LONG error,BOOL transferred)
{ (void)p;(void)error;(void)transferred; }

#if CONF_WITH_PROCESS_FILES
#include "process_files.h"
#include "gemerror.h"
#include "biosext.h"

/* These launcher fixtures supply legacy SFTs and no PROCESS file table. */
BOOL process_files_managed(PD *pd) { (void)pd; return FALSE; }
PROCESS_FILES *kproc_files(PD *pd) { (void)pd; return NULL; }
LONG process_files_inherit(PD *child, PD *parent, UWORD policy)
{
    (void)child; (void)parent; (void)policy;
    panic("unexpected managed descriptor inheritance in launcher fixture");
    return EINVFN;
}
void process_files_retire(PROCESS_FILES *files)
{ if (files) panic("unexpected managed descriptor retirement in launcher fixture"); }
#endif
