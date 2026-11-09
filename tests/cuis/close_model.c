/* Loader fixtures model SFT ownership; the real close service/backend is
 * exercised by test-fs-close.py and the non-CUIS cleanup fixture. */
#include "emutos.h"
#include "fs.h"
#include "fs_close.h"
#include "string.h"
#include "kproc.h"

LONG fs_close_bounded(WORD h, BOOL dispose, FS_CLOSE_REPORT *out)
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
void kproc_note_file_cleanup(PD *p,LONG error,BOOL transferred)
{ (void)p;(void)error;(void)transferred; }
