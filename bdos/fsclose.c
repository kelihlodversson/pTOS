/* Exact SFT reference ownership across close errors and PROCESS retirement.
 * Cooperative owner admission only; this is not an SMP lock or retry daemon. */
#include "emutos.h"
#include "fs.h"
#include "fs_close.h"
#include "gemerror.h"
#include "bdosstub.h"
#include "biosext.h"
#include "console.h"
#include "kproc.h"
#include "string.h"
#if CONF_WITH_PROCESS_FILES
#include "process_files.h"
#endif

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
        sft[n].f_use>0 && (ULONG)sft[n].f_use>sft[n].f_cleanup_refs+sft[n].f_staged_refs;
}
BOOL fs_sft_claim(int n)
{
    FTAB *f;
    if (!kproc_file_references_allowed(run) || !valid_slot(n) || last_incarnation==~(UQUAD)0) return FALSE;
    f=&sft[n];
    if (f->f_own || f->f_use || f->f_close_busy || f->f_cleanup_refs || f->f_staged_refs) return FALSE;
#if CONF_WITH_PLUGGABLE_FS
    if (f->f_io_busy) return FALSE;
#endif
    f->f_incarnation=++last_incarnation;
    f->f_cleanup_issued=f->f_cleanup_consumed=f->f_cleanup_invalid_from=0;
    f->f_cleanup_first_error=f->f_cleanup_last_error=0;
    f->f_cleanup_state=0; f->f_close_cleanup=FALSE; f->f_close_staged=FALSE;
    return TRUE;
}
void fs_sft_invalidated(int n)
{
    FTAB *f=&sft[n];
    if (f->f_close_busy) panic("filesystem: invalidation during active close");
#if CONF_WITH_PLUGGABLE_FS
    if (f->f_io_busy) panic("filesystem: invalidation during active driver I/O");
#endif
    /* The filesystem's normal media-invalidation path owns disposal of the
     * handle group. It is not a forced free by the cleanup service. */
    /* Do this before the filesystem makes the numeric slot reusable. Never
     * let a retired alias close a replacement incarnation during PD cleanup. */
    kproc_standard_file_invalidate((WORD)(n+NUMSTD));
    if (f->f_cleanup_refs) f->f_cleanup_invalid_from=f->f_cleanup_consumed+1;
    f->f_cleanup_consumed=f->f_cleanup_issued;
    f->f_cleanup_refs=0;
    f->f_staged_refs=0; /* authoritative invalidation revokes live staging too */
    f->f_cleanup_state=FS_CLEANUP_INVALIDATED;
    f->f_cleanup_last_error=E_CHNG;
}
/* Residual references keep the backend alive, not ordinary descriptors.
 * Numeric closes may consume the final ordinary alias before the final total
 * count. Clear only aliases naming this slot; preserve callback rebindings. */
static void unpublish_exhausted_aliases(WORD handle)
{
    FTAB *f=&sft[handle-NUMSTD];
    if (f->f_use>=0 && (ULONG)f->f_use<=f->f_cleanup_refs+f->f_staged_refs)
        kproc_standard_file_invalidate(handle);
}
/* pTOS defines jmp_buf as a concrete register array in include/setjmp.h;
 * these copies preserve its own setjmp.S ABI, as DISK_CALL does. This is
 * intentionally not a portable hosted-libc jmp_buf abstraction. CPU-owned
 * execution save/restore also preserves the complete disk-error scope. */
typedef struct { jmp_buf jump; long code,rw; int drive; } CLOSE_ERROR_SCOPE;
static void save_error(CLOSE_ERROR_SCOPE *s)
{ memcpy(s->jump,errbuf,sizeof(errbuf)); s->code=errcode; s->rw=rwerr; s->drive=errdrv; }
static void restore_error(const CLOSE_ERROR_SCOPE *s)
{ memcpy(errbuf,s->jump,sizeof(errbuf)); errcode=s->code; rwerr=s->rw; errdrv=s->drive; }

static LONG close_once(volatile WORD h, BOOL cleanup, BOOL staged, FS_CLOSE_REPORT *out)
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
    if (cleanup ? !f->f_cleanup_refs : staged ? !f->f_staged_refs :
        (ULONG)f->f_use<=f->f_cleanup_refs+f->f_staged_refs)
        return out->error=out->last_error=EIHNDL;
    if (cleanup) out->token.reference=f->f_cleanup_consumed+1;
    out->disposition=FS_CLOSE_RETAINED;
    if (f->f_close_busy) return out->error=out->last_error=EACCDN;
    f->f_close_busy=TRUE; f->f_close_cleanup=cleanup; f->f_close_staged=staged;
    save_error(&saved);
    if (setjmp(errbuf)) {
        if (consumed) panic("filesystem: consuming close finalizer escaped");
        error=errcode; out->disk_error=TRUE; out->drive=errdrv; out->rw=rwerr;
        out->first_disk_error=TRUE; out->first_drive=errdrv; out->first_rw=rwerr;
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
        f->f_cleanup_consumed++;
        if (!f->f_cleanup_refs) f->f_cleanup_state=FS_CLEANUP_COMPLETE;
    }
    if (consumed && !staged) unpublish_exhausted_aliases(h);
    /* A final PFS release may rebind the unpublished standard entry. Only
     * relinquish this operation's old mapping, never a callback's new alias.
     * Slot reuse itself is excluded until f_close_busy is cleared below. */
    if (consumed && standard>=0 && PD_UFT(run)[standard]==h)
        PD_UFT(run)[standard]=get_default_handle(standard);
    restore_error(&saved);
    f->f_close_busy=FALSE; f->f_close_cleanup=FALSE; f->f_close_staged=FALSE;
    out->error=out->last_error=error;
    return error;
}
LONG fs_close_once(WORD h, BOOL cleanup, FS_CLOSE_REPORT *out)
{ return close_once(h,cleanup,FALSE,out); }
/* A transfer reclassifies one already-owned reference; it does not increment
 * f_use, retain a PROCESS, allocate memory, or consume a peer's reference. */
static BOOL transfer_reference(FS_CLOSE_REPORT *r, BOOL staged)
{
    int n=r->token.handle-NUMSTD;
    FTAB *f;
    ULONG available,active;
    if (!valid_slot(n)) return FALSE;
    f=&sft[n];
    if (f->f_incarnation!=r->token.incarnation || f->f_use<=0 ||
        (ULONG)f->f_use<f->f_cleanup_refs+f->f_staged_refs ||
        f->f_cleanup_issued==~(UQUAD)0) return FALSE;
    available=staged ? f->f_staged_refs :
        (ULONG)f->f_use-f->f_cleanup_refs-f->f_staged_refs;
    active=f->f_close_busy && !f->f_close_cleanup && f->f_close_staged==staged ? 1UL:0UL;
    if (available<=active) return FALSE;
    r->token.reference=++f->f_cleanup_issued;
    f->f_cleanup_refs++;
    if (!f->f_cleanup_first_error) f->f_cleanup_first_error=r->error;
    f->f_cleanup_last_error=r->last_error;
    f->f_cleanup_state=FS_CLEANUP_PENDING;
    r->disposition=FS_CLOSE_TRANSFERRED;
    return TRUE;
}
static LONG bounded_close(WORD h, BOOL cleanup, BOOL staged, BOOL dispose, FS_CLOSE_REPORT *out)
{
    FS_CLOSE_REPORT step;
    LONG first=0;
    BOOL first_disk=FALSE;
    int first_drive=0;
    LONG first_rw=0;
    UWORD tries;
    for (tries=0;tries<=BLKDEVNUM;tries++) {
        close_once(h,cleanup,staged,&step);
        if (!tries) {
            first=step.error;
            first_disk=step.disk_error;first_drive=step.drive;first_rw=step.rw;
        }
        if (step.disk_error) mark_bcbs_invalid(step.drive);
        if (step.disposition==FS_CLOSE_CONSUMED || !step.disk_error) break;
    }
    *out=step; out->error=first;
    out->first_disk_error=first_disk; out->first_drive=first_drive; out->first_rw=first_rw;
    if (dispose && out->disposition==FS_CLOSE_RETAINED) {
        FTAB *f=&sft[out->token.handle-NUMSTD];
        /* Retained EACCDN from a recursive numeric close is not an independent
         * disposal right when the sole ordinary reference is already in flight.
         * Preserve close_once's retryable report; never manufacture a ticket. */
        if (!staged && f->f_close_busy && (ULONG)f->f_use<=f->f_cleanup_refs+
            f->f_staged_refs+(!f->f_close_cleanup && !f->f_close_staged ? 1UL:0UL))
            out->disposition=FS_CLOSE_NOT_OWNED;
        else if (!transfer_reference(out,staged))
            panic("filesystem: retirement lost its file reference");
    }
    if (!staged && out->disposition==FS_CLOSE_TRANSFERRED)
        unpublish_exhausted_aliases(out->token.handle);
    return first;
}
LONG fs_close_bounded(WORD h, BOOL dispose, FS_CLOSE_REPORT *out)
{ return bounded_close(h,FALSE,FALSE,dispose,out); }

static LONG retain_slot(FTAB *file, FS_FILE_REFERENCE *out)
{
    if ((unsigned int)file->f_use == ((unsigned int)~0U >> 1) ||
        file->f_staged_refs == ~(ULONG)0)
        return EAGAIN;
    file->f_use++;
    file->f_staged_refs++;
    out->handle = (WORD)(file - sft + NUMSTD);
    out->incarnation = file->f_incarnation;
    out->owned = TRUE;
    return E_OK;
}

LONG fs_reference_pin(const FS_HANDLE *handle, FS_FILE_REFERENCE *out)
{
    FTAB *file;

    if (!handle || !out || out->owned || out->closing)
        return EINVAL;
    if (!kproc_file_references_allowed(run))
        return EACCDN;
    file = handle->slot;
    if (handle->bios || !file || !fs_sft_accessible((int)(file - sft)) ||
        !handle->incarnation || file->f_incarnation != handle->incarnation ||
        file->f_cleanup_state == FS_CLEANUP_INVALIDATED)
        return EIHNDL;
    return retain_slot(file, out);
}

LONG fs_reference_lookup(const FS_FILE_REFERENCE *reference, FS_HANDLE *out)
{
    FTAB *file;

    if (!out)
        return EINVAL;
    if (!reference || !reference->owned || !valid_slot(reference->handle - NUMSTD))
        return EIHNDL;
    file = &sft[reference->handle - NUMSTD];
    if (file->f_incarnation != reference->incarnation)
        return EIHNDL;
    if (file->f_cleanup_state == FS_CLEANUP_INVALIDATED)
        return E_CHNG;
    if (reference->closing || file->f_close_busy)
        return EACCDN;
    if (!file->f_own || !file->f_staged_refs || file->f_use <= 0)
        return EIHNDL;
    out->slot = file;
    out->bios = file->f_bios;
    out->incarnation = file->f_incarnation;
    return E_OK;
}

static UWORD file_open_mode(const FS_HANDLE *handle)
{
    if (handle->bios)
        return RW_MODE;
#if CONF_WITH_PLUGGABLE_FS
    if (handle->slot->f_pfs.fs)
        return handle->slot->f_open_mode;
#endif
    return handle->slot->f_ofd->o_mod & MODE_FAC;
}

LONG xfcntl(WORD descriptor, UWORD command)
{
    FS_HANDLE handle;
    LONG error;

    if (command != GEMDOS_F_GETFL)
        return EINVFN;
    error = fs_handle_lookup(descriptor, &handle);
    return error == E_OK ? file_open_mode(&handle) : error;
}

LONG fs_file_ioctl(WORD descriptor, UWORD command, void *arg, ULONG size)
{
#if CONF_WITH_PLUGGABLE_FS
    FS_HANDLE handle;
    LONG error = fs_handle_lookup(descriptor, &handle);

    if (error != E_OK)
        return error;
    if (!handle.bios && handle.slot->f_pfs.fs)
        return pfs_handle_ioctl(&handle, command, arg, size);
#else
    (void)descriptor;
    (void)command;
    (void)arg;
    (void)size;
#endif
    return EINVFN;
}

LONG fs_reference_ready(const FS_FILE_REFERENCE *reference, UWORD events)
{
    FS_HANDLE handle;
    UWORD mode;
    LONG error = fs_reference_lookup(reference, &handle);

    if (error != E_OK)
        return error == EACCDN ? EAGAIN : error;
    mode = file_open_mode(&handle);
    events &= FS_READY_READ | FS_READY_WRITE | FS_READY_PRIORITY;
    if (mode == WO_MODE)
        events &= ~(FS_READY_READ | FS_READY_PRIORITY);
    if (mode == RO_MODE)
        events &= ~FS_READY_WRITE;
#if CONF_WITH_PLUGGABLE_FS
    if (handle.slot->f_pfs.fs) {
        PFSCOOKIE *file = &handle.slot->f_pfs;
        LONG ready = file->fs->ready ? file->fs->ready(file, events) : EINVFN;
        return ready < 0 ? ready : ready & (events | FS_READY_ERROR | FS_READY_HANGUP);
    }
#endif
    /* Regular FAT files can complete I/O synchronously, including EOF. */
    return events & (FS_READY_READ | FS_READY_WRITE);
}

/* A pin does not flush a still-owned file. If it is the final owner, run
 * the existing consuming close/cleanup protocol before releasing storage. */
LONG fs_reference_unpin(FS_FILE_REFERENCE *reference)
{
    FS_CLOSE_REPORT report;

    if (reference && reference->owned && !reference->closing &&
        valid_slot(reference->handle - NUMSTD)) {
        FTAB *file = &sft[reference->handle - NUMSTD];

        if (file->f_incarnation == reference->incarnation && file->f_use > 1 &&
            file->f_staged_refs && file->f_cleanup_state != FS_CLEANUP_INVALIDATED) {
            file->f_use--;
            file->f_staged_refs--;
            reference->owned = FALSE;
            return E_OK;
        }
    }
    return fs_reference_close(reference, TRUE, &report);
}

/* Existing legacy binding resolver for this prerequisite. Stage B replaces
 * resolution for ALL managed descriptors, not just inherited child tables.
 * Caller holds source identity and reversible descriptor admission; this
 * check/count publication is non-yielding under today's single-CPU contract. */
static LONG reference_acquire(PD *pd,WORD descriptor,UQUAD expected,FS_FILE_REFERENCE *out)
{
    WORD handle=descriptor;
    FTAB *f;
    ULONG aliases[OPNFILES];
    if (!out || out->owned || out->closing || !expected) return EINVAL;
    if (!pd || pd!=run || !kproc_file_references_allowed(pd)) return EACCDN;
    if (descriptor<0) return EIHNDL;
#if CONF_WITH_PROCESS_FILES
    if (process_files_managed(pd)) {
        UQUAD incarnation;
        LONG error=process_files_resolve(pd,descriptor,&handle,&incarnation);
        if (error) return error;
        if (incarnation!=expected) return EIHNDL;
    } else
#endif
    if (descriptor<NUMSTD) handle=PD_UFT(pd)[descriptor];
    if (handle<NUMSTD || !valid_slot(handle-NUMSTD)) return EIHNDL;
    f=&sft[handle-NUMSTD];
    if (f->f_incarnation!=expected || f->f_cleanup_state==FS_CLEANUP_INVALIDATED ||
        !fs_sft_accessible(handle-NUMSTD)) return EIHNDL;
    if (!f->f_ofd
#if CONF_WITH_PLUGGABLE_FS
        && !f->f_pfs.fs
#endif
        ) return EIHNDL;
    if (descriptor>=NUMSTD
#if CONF_WITH_PROCESS_FILES
        && !process_files_managed(pd)
#endif
    ) {
        /* f_own alone can survive the original explicit close. Do not mint
         * from someone else's standard alias, quarantine or staged owner. */
        kproc_standard_file_refs(aliases);
#if CONF_WITH_PROCESS_FILES
        if (!process_files_managed(pd)) {
            WORD i;
            for (i=0;i<NUMSTD;i++) {
                WORD alias=PD_UFT(pd)[i];
                if (alias>=NUMSTD && valid_slot(alias-NUMSTD)) aliases[alias-NUMSTD]++;
            }
        }
#endif
        if (f->f_own!=pd || (ULONG)f->f_use<=aliases[handle-NUMSTD]+
                f->f_cleanup_refs+f->f_staged_refs) return EACCDN;
    }
    return retain_slot(f, out);
}
LONG fs_reference_acquire(KPROC *source,WORD descriptor,UQUAD expected,FS_FILE_REFERENCE *out)
{
    PD *pd=kproc_live_pd(source);
    if (!pd) return EACCDN;
    return reference_acquire(pd,descriptor,expected,out);
}
#if CONF_WITH_PROCESS_FILES
/* Trusted pre-KPROC boot handoff only. A normal PROCESS must authenticate its
 * canonical identity and binding; numeric SFT membership alone grants nothing. */
LONG fs_reference_acquire_boot(PD *source,WORD descriptor,UQUAD expected,FS_FILE_REFERENCE *out)
{
    if (!source || source!=run || process_files_managed(source)) return EACCDN;
    return reference_acquire(source,descriptor,expected,out);
}
#endif
#if CONF_WITH_PROCESS_FILES
BOOL fs_reference_publishable(const FS_FILE_REFERENCE *reference)
{
    FTAB *f;
    if (!reference || !reference->owned || reference->closing || !valid_slot(reference->handle-NUMSTD)) return FALSE;
    f=&sft[reference->handle-NUMSTD];
    return f->f_incarnation==reference->incarnation && f->f_own && f->f_use>0 &&
        f->f_staged_refs && (ULONG)f->f_use>=f->f_staged_refs+f->f_cleanup_refs &&
        f->f_cleanup_state!=FS_CLEANUP_INVALIDATED;
}
void fs_reference_publish(FS_FILE_REFERENCE *reference)
{
    if (!fs_reference_publishable(reference)) panic("publishing invalid staged descriptor reference");
    sft[reference->handle-NUMSTD].f_staged_refs--;
    reference->owned=FALSE;
}
LONG fs_reference_unpublish(WORD backend,UQUAD incarnation,FS_FILE_REFERENCE *out)
{
    FTAB *f;
    ULONG ordinary,active;
    if (!out || out->owned || out->closing || !valid_slot(backend-NUMSTD)) return EINVAL;
    f=&sft[backend-NUMSTD];
    if (f->f_incarnation!=incarnation) return EIHNDL;
    if (f->f_cleanup_state==FS_CLEANUP_INVALIDATED) return E_CHNG;
    if (!f->f_own || f->f_use<=0 || (ULONG)f->f_use<f->f_staged_refs+f->f_cleanup_refs)
        panic("descriptor exact ordinary reference lost");
    ordinary=(ULONG)f->f_use-f->f_staged_refs-f->f_cleanup_refs;
    active=f->f_close_busy && !f->f_close_cleanup && !f->f_close_staged;
    if (ordinary<=active) return EACCDN;
    if (f->f_staged_refs==~(ULONG)0) return EAGAIN;
    f->f_staged_refs++;
    out->handle=backend;out->incarnation=incarnation;out->owned=TRUE;
    return E_OK;
}
LONG fs_reference_dispose(FS_FILE_REFERENCE *reference,FS_CLOSE_REPORT *report)
{
    FS_CLOSE_REPORT transferred=*report;
    if (!fs_reference_publishable(reference)) return EIHNDL;
    transferred.token.handle=reference->handle;transferred.token.incarnation=reference->incarnation;
    if (!transfer_reference(&transferred,TRUE)) return EACCDN;
    sft[reference->handle-NUMSTD].f_staged_refs--;
    reference->owned=FALSE;*report=transferred;
    unpublish_exhausted_aliases(reference->handle);
    return E_OK;
}
#endif
LONG fs_reference_close(FS_FILE_REFERENCE *reference, BOOL dispose, FS_CLOSE_REPORT *out)
{
    FTAB *f;
    LONG error;
    if (!reference || !out) return EINVAL;
    memset(out,0,sizeof(*out)); out->disposition=FS_CLOSE_NOT_OWNED;
    if (!reference->owned) return out->error=out->last_error=EIHNDL;
    if (reference->closing) {
        out->disposition=FS_CLOSE_RETAINED;
        return out->error=out->last_error=EACCDN;
    }
    if (!valid_slot(reference->handle-NUMSTD)) panic("invalid live file reference");
    f=&sft[reference->handle-NUMSTD];
    if (f->f_incarnation!=reference->incarnation ||
        (f->f_cleanup_state==FS_CLEANUP_INVALIDATED && !f->f_staged_refs)) {
        /* Only authoritative media invalidation/reincarnation revokes a live
         * owner. A lost count without that evidence is not a fake media error. */
        error=f->f_incarnation==reference->incarnation ? E_CHNG : EIHNDL;
        reference->owned=FALSE;
        return out->error=out->last_error=error;
    }
    if (!f->f_own || !f->f_staged_refs || f->f_use<=0 ||
        (ULONG)f->f_use<f->f_staged_refs+f->f_cleanup_refs)
        panic("live file reference ownership lost");
    reference->closing=TRUE;
    error=bounded_close(reference->handle,FALSE,TRUE,dispose,out);
    if (out->disposition==FS_CLOSE_CONSUMED || out->disposition==FS_CLOSE_TRANSFERRED) {
        if (f->f_incarnation!=reference->incarnation || !f->f_staged_refs)
            panic("live file reference lost during close");
        f->f_staged_refs--; reference->owned=FALSE;
        unpublish_exhausted_aliases(reference->handle);
    }
    reference->closing=FALSE;
    return error;
}
LONG fs_cleanup_snapshot(const FS_CLEANUP_TOKEN *token, FS_CLEANUP_STATUS *out)
{
    int n;
    FTAB *f;
    if (!token || !out) return EINVAL;
    n=token->handle-NUMSTD;
    if (!valid_slot(n) || !token->incarnation || sft[n].f_incarnation!=token->incarnation) return EIHNDL;
    f=&sft[n];
    if (!token->reference || token->reference>f->f_cleanup_issued) return EIHNDL;
    out->token=*token;
    out->pending_references=f->f_cleanup_refs;
    out->references=token->reference>f->f_cleanup_consumed ? 1 : 0;
    out->first_error=f->f_cleanup_first_error; out->last_error=f->f_cleanup_last_error;
    out->state=out->references ? FS_CLEANUP_PENDING :
        f->f_cleanup_invalid_from && token->reference>=f->f_cleanup_invalid_from ?
        FS_CLEANUP_INVALIDATED : FS_CLEANUP_COMPLETE;
    return E_OK;
}
LONG fs_cleanup_retry(const FS_CLEANUP_TOKEN *token, FS_CLOSE_REPORT *out)
{
    FS_CLEANUP_STATUS status;
    LONG error=fs_cleanup_snapshot(token,&status);
    if (error) return error;
    if (!status.references) return EIHNDL;
    /* FIFO keeps exact once-only transfer rights without an allocated record
     * per failed alias. A later ticket cannot consume an earlier owner. */
    if (token->reference!=sft[token->handle-NUMSTD].f_cleanup_consumed+1) return EAGAIN;
    error=bounded_close(token->handle,TRUE,FALSE,FALSE,out);
    sft[token->handle-NUMSTD].f_cleanup_last_error=out->last_error;
    return error;
}
long xclose(WORD h)
{
    FS_CLOSE_REPORT result;
    LONG error=
#if CONF_WITH_PROCESS_FILES
        h>=0 && process_files_managed(run)?process_files_close(run,h,FALSE,&result):
#endif
        fs_close_once(h,FALSE,&result);
    if (result.disk_error) {
        /* Preserve osif's existing media-change/error policy. The operation
         * has already unpublished its owner and restored the enclosing target. */
        errcode=error; errdrv=result.drive; rwerr=result.rw;
        longjmp(errbuf,1);
    }
    return error;
}
