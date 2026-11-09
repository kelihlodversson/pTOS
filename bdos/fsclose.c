/* Exact SFT reference ownership across close errors and PROCESS retirement.
 * Cooperative owner admission only; this is not an SMP lock or retry daemon. */
#include "emutos.h"
#include "fs.h"
#include "fs_close.h"
#include "gemerror.h"
#include "bdosstub.h"
#include "biosext.h"
#include "console.h"
#include "string.h"

static UQUAD last_incarnation;
#if CONF_WITH_X86_64_MEMTEST
static WORD test_exhaust_handle=-1;
void fs_close_test_exhaust(WORD handle) { test_exhaust_handle=handle; }
#endif
static BOOL valid_slot(int n) { return n>=0 && n<OPNFILES; }
BOOL fs_sft_busy(int n) { return valid_slot(n) && sft[n].f_close_busy; }
BOOL fs_sft_accessible(int n)
{
    return valid_slot(n) && sft[n].f_own && !sft[n].f_close_busy &&
        sft[n].f_use>0 && (ULONG)sft[n].f_use>sft[n].f_cleanup_refs;
}
BOOL fs_sft_claim(int n)
{
    FTAB *f;
    if (!valid_slot(n) || last_incarnation==~(UQUAD)0) return FALSE;
    f=&sft[n];
    if (f->f_own || f->f_use || f->f_close_busy || f->f_cleanup_refs) return FALSE;
    f->f_incarnation=++last_incarnation;
    f->f_cleanup_first_error=f->f_cleanup_last_error=0;
    f->f_cleanup_state=0; f->f_close_cleanup=FALSE;
    return TRUE;
}
void fs_sft_invalidated(int n)
{
    FTAB *f=&sft[n];
    if (f->f_close_busy) panic("filesystem: invalidation during active close");
    /* The filesystem's normal media-invalidation path owns disposal of the
     * handle group. It is not a forced free by the cleanup service. */
    f->f_cleanup_refs=0;
    f->f_cleanup_state=FS_CLEANUP_INVALIDATED;
    f->f_cleanup_last_error=E_CHNG;
}
typedef struct { jmp_buf jump; long code,rw; int drive; } CLOSE_ERROR_SCOPE;
static void save_error(CLOSE_ERROR_SCOPE *s)
{ memcpy(s->jump,errbuf,sizeof(errbuf)); s->code=errcode; s->rw=rwerr; s->drive=errdrv; }
static void restore_error(const CLOSE_ERROR_SCOPE *s)
{ memcpy(errbuf,s->jump,sizeof(errbuf)); errcode=s->code; rwerr=s->rw; errdrv=s->drive; }

LONG fs_close_once(volatile WORD h, BOOL cleanup, FS_CLOSE_REPORT *out)
{
    CLOSE_ERROR_SCOPE saved;
    volatile BOOL consumed=FALSE;
    FTAB *f;
    int n;
    volatile WORD standard=-1;
    LONG error;
    memset(out,0,sizeof(*out));
    out->disposition=FS_CLOSE_NOT_OWNED;
    if (h<0) { out->disposition=FS_CLOSE_CONSUMED; return E_OK; }
    if (h<NUMSTD) {
        standard=h; h=PD_UFT(run)[h];
        if (h<0) {
            PD_UFT(run)[standard]=get_default_handle(standard);
            out->disposition=FS_CLOSE_CONSUMED; return E_OK;
        }
    }
    n=h-NUMSTD;
    if (!valid_slot(n) || !sft[n].f_own || sft[n].f_use<=0)
        return out->error=out->last_error=EIHNDL;
    f=&sft[n]; out->token.handle=h; out->token.incarnation=f->f_incarnation;
    if (!f->f_incarnation) panic("filesystem: live slot without incarnation");
    if (cleanup ? !f->f_cleanup_refs : (ULONG)f->f_use<=f->f_cleanup_refs)
        return out->error=out->last_error=EIHNDL;
    out->disposition=FS_CLOSE_RETAINED;
    if (f->f_close_busy) return out->error=out->last_error=EACCDN;
    f->f_close_busy=TRUE; f->f_close_cleanup=cleanup;
    save_error(&saved);
    if (setjmp(errbuf)) {
        if (consumed) panic("filesystem: consuming close finalizer escaped");
        error=errcode; out->disk_error=TRUE; out->drive=errdrv; out->rw=rwerr;
    } else {
#if CONF_WITH_X86_64_MEMTEST
        if (h==test_exhaust_handle) {
            errcode=EWRITF; errdrv=0; rwerr=EWRITF;
            longjmp(errbuf,1);
        }
#endif
        error=fs_close_backend(h,&consumed);
    }
    out->disposition=consumed ? FS_CLOSE_CONSUMED : FS_CLOSE_RETAINED;
    if (consumed && cleanup) {
        if (!f->f_cleanup_refs) panic("filesystem: cleanup reference underflow");
        f->f_cleanup_refs--;
        if (!f->f_cleanup_refs) f->f_cleanup_state=FS_CLEANUP_COMPLETE;
    }
    if (consumed && standard>=0) PD_UFT(run)[standard]=get_default_handle(standard);
    restore_error(&saved);
    f->f_close_busy=FALSE; f->f_close_cleanup=FALSE;
    out->error=out->last_error=error;
    return error;
}
/* A transfer reclassifies one already-owned reference; it does not increment
 * f_use, retain a PROCESS, allocate memory, or consume a peer's reference. */
static BOOL transfer_reference(FS_CLOSE_REPORT *r)
{
    int n=r->token.handle-NUMSTD;
    FTAB *f;
    ULONG active_user;
    if (!valid_slot(n)) return FALSE;
    f=&sft[n];
    active_user=f->f_close_busy && !f->f_close_cleanup ? 1 : 0;
    if (f->f_incarnation!=r->token.incarnation || f->f_use<=0 ||
        (ULONG)f->f_use<=f->f_cleanup_refs+active_user) return FALSE;
    f->f_cleanup_refs++;
    if (!f->f_cleanup_first_error) f->f_cleanup_first_error=r->error;
    f->f_cleanup_last_error=r->last_error;
    f->f_cleanup_state=FS_CLEANUP_PENDING;
    r->disposition=FS_CLOSE_TRANSFERRED;
    return TRUE;
}
static LONG bounded_close(WORD h, BOOL cleanup, BOOL dispose, FS_CLOSE_REPORT *out)
{
    FS_CLOSE_REPORT step;
    LONG first=0;
    UWORD tries;
    for (tries=0;tries<=BLKDEVNUM;tries++) {
        fs_close_once(h,cleanup,&step);
        if (!tries) first=step.error;
        if (step.disk_error) mark_bcbs_invalid(step.drive);
        if (step.disposition==FS_CLOSE_CONSUMED || !step.disk_error) break;
    }
    *out=step; out->error=first;
    if (dispose && out->disposition==FS_CLOSE_RETAINED && !transfer_reference(out))
        panic("filesystem: retirement lost its file reference");
    return first;
}
LONG fs_close_bounded(WORD h, BOOL dispose, FS_CLOSE_REPORT *out)
{ return bounded_close(h,FALSE,dispose,out); }
LONG fs_cleanup_snapshot(const FS_CLEANUP_TOKEN *token, FS_CLEANUP_STATUS *out)
{
    int n;
    FTAB *f;
    if (!token || !out) return EINVAL;
    n=token->handle-NUMSTD;
    if (!valid_slot(n) || !token->incarnation || sft[n].f_incarnation!=token->incarnation) return EIHNDL;
    f=&sft[n];
    out->token=*token; out->references=f->f_cleanup_refs;
    out->first_error=f->f_cleanup_first_error; out->last_error=f->f_cleanup_last_error;
    out->state=f->f_cleanup_state;
    return E_OK;
}
LONG fs_cleanup_retry(const FS_CLEANUP_TOKEN *token, FS_CLOSE_REPORT *out)
{
    FS_CLEANUP_STATUS status;
    LONG error=fs_cleanup_snapshot(token,&status);
    if (error) return error;
    if (!status.references) return EIHNDL;
    error=bounded_close(token->handle,TRUE,FALSE,out);
    sft[token->handle-NUMSTD].f_cleanup_last_error=out->last_error;
    return error;
}
long xclose(WORD h)
{
    FS_CLOSE_REPORT result;
    LONG error=fs_close_once(h,FALSE,&result);
    if (result.disk_error) {
        /* Preserve osif's existing media-change/error policy. The operation
         * has already unpublished its owner and restored the enclosing target. */
        errcode=error; errdrv=result.drive; rwerr=result.rw;
        longjmp(errbuf,1);
    }
    return error;
}
