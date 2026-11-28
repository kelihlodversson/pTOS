/* Production checked reference + FAT/PFS close; PROCESS registry/disk modeled. */
#define main close_backend_fixture_main
#define USE_PRODUCTION_PFS_IO
#include "fs_close.c"
#undef main
#include "kproc.h"
#include "kheap.h"
#include <stdlib.h>
#if CONF_WITH_MULTITHREADING
#include "ksched.h"

/* Model the scheduler's nonreturning group-exit checkpoint. The SFT and
 * the PFS operation that must defer it are production code. */
static KSCHED_CLEANUP_SCOPE *active_cleanup;
static BOOL group_exit_pending;
static jmp_buf group_exit;

void ksched_cleanup_begin(KSCHED_CLEANUP_SCOPE *scope)
{
    CHECK(!active_cleanup && !scope->active);
    scope->active = TRUE;
    active_cleanup = scope;
}

void ksched_cleanup_end(KSCHED_CLEANUP_SCOPE *scope)
{
    CHECK(active_cleanup == scope && scope->active);
    CHECK(!sft[0].f_io_busy && !sft[0].f_staged_refs);
    scope->active = FALSE;
    active_cleanup = NULL;
}

void ksched_cancel_point(void)
{
    if (group_exit_pending && !active_cleanup)
        longjmp(group_exit, 1);
}

static LONG release_at_cancel_point(PFSCOOKIE *cookie)
{
    CHECK(group_exit_pending);
    ksched_cancel_point();
    return driver_release_file(cookie);
}
#endif

static BOOL source_live=TRUE;
PD *kproc_live_pd(const KPROC *p)
{ return source_live && p==(KPROC *)&owner ? &owner : NULL; }
void kproc_standard_file_refs(ULONG *refs)
{
#if !CONF_WITH_PROCESS_FILES
    int i;
#endif
    memset(refs,0,OPNFILES*sizeof(*refs));
#if !CONF_WITH_PROCESS_FILES
    /* The older registry reports the legacy aliases itself. */
    for(i=0;i<NUMSTD;i++) if(owner.p_uft[i]>=NUMSTD)
        refs[owner.p_uft[i]-NUMSTD]++;
#endif
}
static BOOL nested_dispose;
static FS_FILE_REFERENCE *nested_staged_owner;
static UWORD nested_expected;
static LONG reference_reenter_close(PFSCOOKIE *cookie)
{
    FS_CLOSE_REPORT nested;
    (void)cookie;
    if (nested_dispose) {
        nested_dispose=FALSE;
        CHECK((nested_staged_owner ? fs_reference_close(nested_staged_owner,TRUE,&nested):
            fs_close_bounded(NUMSTD,TRUE,&nested))==EACCDN);
        CHECK(nested.disposition==nested_expected);
        CHECK(sft[0].f_cleanup_refs==(nested_expected==FS_CLOSE_TRANSFERRED ? 1UL:0UL));
    }
    return E_OK;
}
static void reentrant_reference_case(BOOL outer_staged,BOOL extra_ordinary,BOOL inner_staged)
{
    FS_FILE_REFERENCE held=FS_FILE_REFERENCE_INIT;
    FS_CLOSE_REPORT result;
    FS_CLEANUP_TOKEN residual;
    CHECK(fs_sft_claim(0));
    sft[0].f_own=&owner;sft[0].f_use=1;sft[0].f_pfs.fs=&driver;
    driver.close=reference_reenter_close;driver.release_file=driver_release_file;
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,sft[0].f_incarnation,&held)==E_OK);
    if(extra_ordinary)CHECK(ixforce(0,NUMSTD,&owner)==E_OK);
    nested_expected=(outer_staged || extra_ordinary || inner_staged) ? FS_CLOSE_TRANSFERRED:FS_CLOSE_NOT_OWNED;
    nested_staged_owner=inner_staged ? &held:NULL;
    nested_dispose=TRUE;release_error=E_OK;
    CHECK((outer_staged ? fs_reference_close(&held,TRUE,&result):fs_close_bounded(NUMSTD,TRUE,&result))==E_OK);
    CHECK(!nested_dispose && sft[0].f_use==(extra_ordinary ? 2:1));
    CHECK(sft[0].f_staged_refs==((outer_staged || inner_staged) ? 0UL:1UL));
    if(!outer_staged && !inner_staged)CHECK(fs_reference_close(&held,TRUE,&result)==E_OK && !held.owned);
    if(outer_staged || extra_ordinary || inner_staged) {
        residual.handle=NUMSTD;residual.incarnation=sft[0].f_incarnation;residual.reference=1;
        CHECK(fs_cleanup_retry(&residual,&result)==E_OK && !sft[0].f_use);
    }
    CHECK(!sft[0].f_use && !sft[0].f_cleanup_refs && !sft[0].f_staged_refs);
}

static LONG test_ready(const PFSCOOKIE *file, UWORD events)
{
    CHECK(file == &sft[0].f_pfs && file->pos == 29);
    CHECK(events == FS_READY_READ);
    return FS_READY_READ;
}

static LONG test_terminal_ready(const PFSCOOKIE *file, UWORD events)
{
    CHECK(file == &sft[0].f_pfs && file->pos == 29);
    CHECK(!events);
    return FS_READY_READ | FS_READY_ERROR | FS_READY_HANGUP;
}

static LONG test_control(PFSCOOKIE *file, UWORD command, void *arg, ULONG size)
{
    FS_CLOSE_REPORT report;

    CHECK(file == &sft[0].f_pfs && file->pos == 29 && sft[0].f_io_busy);
    CHECK(sft[0].f_staged_refs == 1 && sft[0].f_use == 2);
    CHECK(fs_file_ioctl(NUMSTD, command, arg, size) == EACCDN);
    CHECK(fs_close_once(NUMSTD, FALSE, &report) == E_OK && sft[0].f_use == 1);
    CHECK(report.disposition == FS_CLOSE_CONSUMED && file->fs == &driver);
#if CONF_WITH_MULTITHREADING
    group_exit_pending = TRUE;
    ksched_cancel_point();
#endif
    if (!arg) {
        errcode = EWRITF;
        errdrv = 0;
        rwerr = 0x1234;
        longjmp(errbuf, 1);
    }
    CHECK(size == sizeof(LONG));
    *(LONG *)arg = file->pos++;
    return E_OK;
}

static void operation_callback_case(BOOL disk_fault)
{
    unsigned old_closes = closes, old_releases = releases;
    LONG position = -1;
#if CONF_WITH_MULTITHREADING
    volatile BOOL operation_settled = FALSE;
#endif

    CHECK(fs_sft_claim(0));
    sft[0].f_own = &owner;
    sft[0].f_use = 1;
    sft[0].f_pfs.fs = &driver;
    sft[0].f_pfs.pos = 29;
    driver.close = driver_close;
#if CONF_WITH_MULTITHREADING
    driver.release_file = release_at_cancel_point;
#else
    driver.release_file = driver_release_file;
#endif
    driver.ioctl = test_control;
    release_error = EWRITF;
#if CONF_WITH_MULTITHREADING
    if (!setjmp(group_exit))
#endif
    {
        if (setjmp(errbuf)) {
            CHECK(disk_fault && errcode == EWRITF && rwerr == 0x1234 && errdrv == 0);
        } else {
            CHECK(fs_file_ioctl(NUMSTD, 42, disk_fault ? NULL : &position, sizeof(position)) == E_OK);
            CHECK(!disk_fault && position == 29);
        }
#if CONF_WITH_MULTITHREADING
        CHECK(group_exit_pending && !active_cleanup);
        operation_settled = TRUE;
        ksched_cancel_point();
        CHECK(FALSE);
#endif
    }
    CHECK(closes == old_closes + 2 && releases == old_releases + 1);
    CHECK(!sft[0].f_own && !sft[0].f_use && !sft[0].f_staged_refs && !sft[0].f_io_busy);
#if CONF_WITH_MULTITHREADING
    CHECK(operation_settled);
    group_exit_pending = FALSE;
    CHECK(fs_sft_claim(0));
#endif
    driver.ioctl = NULL;
}

static void operation_reference_case(void)
{
    FS_HANDLE borrowed, held;
    FS_FILE_REFERENCE pin = FS_FILE_REFERENCE_INIT;
    FS_CLOSE_REPORT report;
    unsigned old_closes = closes, old_releases = releases;

    CHECK(fs_sft_claim(0));
    sft[0].f_own = &owner;
    sft[0].f_use = 1;
    sft[0].f_pfs.fs = &driver;
    sft[0].f_pfs.pos = 29;
    sft[0].f_open_mode = RO_MODE;
    driver.close = driver_close;
    driver.release_file = driver_release_file;
    pfs_error = E_OK;
    release_error = EWRITF;
    CHECK(fs_handle_lookup(NUMSTD, &borrowed) == E_OK);
    CHECK(xfcntl(NUMSTD, 0, GEMDOS_F_GETFL) == RO_MODE);
    CHECK(xfcntl(H_Console, 0, GEMDOS_F_GETFL) == RW_MODE);
    CHECK(xfcntl(NUMSTD, 0, GEMDOS_F_GETFL + 1) == EINVFN);
    retiring = TRUE;
    CHECK(fs_reference_pin(&borrowed, &pin) == EACCDN && !pin.owned);
    retiring = FALSE;
    CHECK(fs_reference_pin(&borrowed, &pin) == E_OK);
    CHECK(fs_reference_unpin(&pin) == E_OK && !pin.owned);
    CHECK(closes == old_closes && releases == old_releases);
    CHECK(fs_reference_pin(&borrowed, &pin) == E_OK);
    CHECK(fs_close_once(NUMSTD, FALSE, &report) == E_OK);
    CHECK(report.disposition == FS_CLOSE_CONSUMED && sft[0].f_use == 1);
    CHECK(fs_handle_lookup(NUMSTD, &held) == EIHNDL);
    CHECK(xfcntl(NUMSTD, 0, GEMDOS_F_GETFL) == EIHNDL);
    CHECK(fs_reference_lookup(&pin, &held) == E_OK);
    CHECK(held.slot == borrowed.slot && held.slot->f_pfs.pos == 29);
    CHECK(fs_reference_ready(&pin, FS_READY_READ) == EINVFN);
    driver.ready = test_ready;
    CHECK(fs_reference_ready(&pin, FS_READY_READ | FS_READY_WRITE) == FS_READY_READ);
    driver.ready = test_terminal_ready;
    CHECK(fs_reference_ready(&pin, 0) == (FS_READY_ERROR | FS_READY_HANGUP));
    sft[0].f_close_busy = TRUE;
    CHECK(fs_reference_ready(&pin, FS_READY_READ) == EAGAIN);
    sft[0].f_close_busy = FALSE;
    driver.ready = NULL;
    CHECK(closes == old_closes + 1 && releases == old_releases);
    CHECK(fs_reference_unpin(&pin) == EWRITF && !pin.owned);
    CHECK(closes == old_closes + 2 && releases == old_releases + 1);
    CHECK(!sft[0].f_own && !sft[0].f_use && !sft[0].f_staged_refs);

    open_file();
    CHECK(fs_reference_pin(&borrowed, &pin) == EIHNDL && !pin.owned);
    CHECK(fs_handle_lookup(NUMSTD, &borrowed) == E_OK);
    CHECK(fs_reference_pin(&borrowed, &pin) == E_OK);
    file.o_dmd = &media;
    ofd_invalidate_drive(&media);
    CHECK(fs_reference_lookup(&pin, &held) == E_CHNG);
    CHECK(fs_reference_ready(&pin, FS_READY_READ) == E_CHNG);
    CHECK(fs_reference_unpin(&pin) == E_CHNG && !pin.owned);
    CHECK(!sft[0].f_own && !sft[0].f_use && !sft[0].f_staged_refs);
    puts("In-flight file pin survives close; release, media revocation and slot reuse conserve references: PASS (host)");
}

#define CONTROL_ADDRESS 0x1200UL
#define CONTROL_COMMAND 0x7b00
static ULONG control_word;
static BOOL control_readable, control_writable, control_fault;
static unsigned control_leases, control_allocations, control_calls;
static PFS_IOCTL_SPEC control_spec;

void *kalloc(ULONG bytes)
{
    void *buffer = calloc(1, bytes);

    CHECK(buffer != NULL);
    control_allocations++;
    return buffer;
}

void kfree(void *buffer)
{
    CHECK(buffer != NULL && control_allocations == 1);
    control_allocations--;
    free(buffer);
}

LONG kproc_user_copy_begin(KPROC_USER_COPY *copy, VADDR address, ULONG bytes, BOOL write)
{
    CHECK(!copy->active && !control_leases && bytes == sizeof(control_word) && write);
    if (address != CONTROL_ADDRESS || !control_writable)
        return EIMBA;
    control_leases++;
    copy->active = TRUE;
    return E_OK;
}

LONG kproc_copy_from_user_checked(void *buffer, VADDR address, ULONG bytes)
{
    CHECK(bytes == sizeof(control_word));
    if (address != CONTROL_ADDRESS || !control_readable)
        return EIMBA;
    memcpy(buffer, &control_word, bytes);
    return E_OK;
}

BOOL kproc_user_copy_write(KPROC_USER_COPY *copy, const void *buffer)
{
    CHECK(copy->active && control_leases == 1);
    if (!control_writable)
        return FALSE;
    memcpy(&control_word, buffer, sizeof(control_word));
    return TRUE;
}

void kproc_user_copy_end(KPROC_USER_COPY *copy)
{
    if (copy->active) {
        CHECK(control_leases == 1);
        control_leases--;
        copy->active = FALSE;
    }
}

static LONG user_control_spec(const PFSCOOKIE *cookie, UWORD command, PFS_IOCTL_SPEC *spec)
{
    CHECK(cookie == &sft[0].f_pfs && sft[0].f_staged_refs == 1);
    if (command != CONTROL_COMMAND)
        return EINVFN;
    *spec = control_spec;
    return E_OK;
}

static LONG user_control(PFSCOOKIE *cookie, UWORD command, void *buffer, ULONG bytes)
{
    ULONG *word = buffer;

    CHECK(cookie == &sft[0].f_pfs && sft[0].f_staged_refs == 1 && sft[0].f_io_busy);
    CHECK(command == CONTROL_COMMAND && bytes == sizeof(*word));
    CHECK(word != &control_word && *word == (control_spec.kind == PFS_IOCTL_OUT ? 0 : 17));
    CHECK(control_leases == (control_spec.kind == PFS_IOCTL_IN ? 0 : 1));
    control_calls++;
    *word = 123;
    if (control_fault) {
        errcode = EWRITF;
        errdrv = 0;
        longjmp(errbuf, 1);
    }
    return E_OK;
}

static void public_control_case(void)
{
    FS_CLOSE_REPORT report;

    CHECK(fs_sft_claim(0));
    sft[0].f_own = &owner;
    sft[0].f_use = 1;
    sft[0].f_pfs.fs = &driver;
    driver.release_file = driver_release_file;
    driver.ioctl_spec = user_control_spec;
    driver.ioctl = user_control;
    control_spec = (PFS_IOCTL_SPEC){ PFS_IOCTL_INOUT, sizeof(control_word) };
    control_word = 17;
    control_readable = TRUE;
    CHECK(xfcntl(NUMSTD, 1, CONTROL_COMMAND + 1) == EINVFN);
    CHECK(xfcntl(NUMSTD, CONTROL_ADDRESS, CONTROL_COMMAND) == EIMBA);
    CHECK(xfcntl(NUMSTD, (1ULL << 32) | CONTROL_ADDRESS, CONTROL_COMMAND) == EIMBA);
    CHECK(!control_calls && !control_leases && !control_allocations);

    control_writable = TRUE;
    control_fault = TRUE;
    CHECK(xfcntl(NUMSTD, CONTROL_ADDRESS, CONTROL_COMMAND) == EWRITF);
    CHECK(control_word == 17 && !control_leases && !control_allocations);
    control_fault = FALSE;
    CHECK(xfcntl(NUMSTD, CONTROL_ADDRESS, CONTROL_COMMAND) == E_OK && control_word == 123);

    control_spec.kind = PFS_IOCTL_IN;
    control_word = 17;
    control_writable = FALSE;
    CHECK(xfcntl(NUMSTD, CONTROL_ADDRESS, CONTROL_COMMAND) == E_OK && control_word == 17);
    control_spec.kind = PFS_IOCTL_OUT;
    control_writable = TRUE;
    control_readable = FALSE;
    CHECK(xfcntl(NUMSTD, CONTROL_ADDRESS, CONTROL_COMMAND) == E_OK && control_word == 123);
    CHECK(control_calls == 4 && !control_leases && !control_allocations &&
          sft[0].f_use == 1 && !sft[0].f_staged_refs && !sft[0].f_io_busy);
    driver.ioctl = NULL;
    driver.ioctl_spec = NULL;
    release_error = E_OK;
    CHECK(fs_close_once(NUMSTD, FALSE, &report) == E_OK);
    puts("Public Fcntl checked argument staging, disk unwind and exact reference cleanup: PASS (host)");
}

int main(void)
{
    FS_FILE_REFERENCE a=FS_FILE_REFERENCE_INIT,b=FS_FILE_REFERENCE_INIT;
    FS_FILE_REFERENCE empty=FS_FILE_REFERENCE_INIT;
    FS_CLOSE_REPORT report;
    FS_CLEANUP_TOKEN token;
    UQUAD incarnation;
    unsigned i;
    memset(&a,0,sizeof(a)); memset(&b,0,sizeof(b)); memset(&empty,0,sizeof(empty));
    for(i=0;i<NUMSTD;i++) owner.p_uft[i]=-1;
    open_file(); incarnation=sft[0].f_incarnation;
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation+1,&a)==EIHNDL);
    CHECK(!memcmp(&a,&empty,sizeof(a)) && sft[0].f_use==1);
    CHECK(fs_reference_acquire((KPROC *)&file,NUMSTD,incarnation,&a)==EACCDN);
    retiring=TRUE;
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&a)==EACCDN);
    retiring=FALSE; sft[0].f_ofd=NULL;
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&a)==EIHNDL && !a.owned);
    sft[0].f_ofd=&file; sft[0].f_close_busy=TRUE;
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&a)==EIHNDL);
    sft[0].f_close_busy=FALSE;
    sft[0].f_cleanup_state=FS_CLEANUP_INVALIDATED;
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&a)==EIHNDL);
    sft[0].f_cleanup_state=0;
    sft[0].f_use=(int)((unsigned int)~0U>>1);
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&a)==EAGAIN);
    sft[0].f_use=1;
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&a)==E_OK);
    CHECK(a.owned && a.handle==NUMSTD && a.incarnation==incarnation &&
          sft[0].f_use==2 && sft[0].f_staged_refs==1 && sft[0].f_ofd==&file);
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&a)==EINVAL);
    CHECK(ixforce(0,NUMSTD,&owner)==E_OK);
    CHECK(fs_reference_acquire((KPROC *)&owner,0,incarnation,&b)==E_OK);
    CHECK(sft[0].f_use==4 && sft[0].f_staged_refs==2);
    CHECK(fs_close_bounded(0,FALSE,&report)==E_OK);
    CHECK(fs_close_bounded(NUMSTD,FALSE,&report)==E_OK && sft[0].f_use==2);
    /* Staged-only reference is not an explicit source binding or raw close. */
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&empty)==EIHNDL &&
          !empty.owned && sft[0].f_use==2 && sft[0].f_staged_refs==2);
    CHECK(fs_close_once(NUMSTD,FALSE,&report)==EIHNDL && sft[0].f_use==2);
    source_live=FALSE; retiring=TRUE;
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&empty)==EACCDN &&
          !empty.owned && sft[0].f_use==2 && sft[0].f_staged_refs==2);
    CHECK(fs_reference_close(&a,FALSE,&report)==E_OK && !a.owned && sft[0].f_use==1);
    CHECK(fs_reference_close(&a,TRUE,&report)==EIHNDL);
    /* The remaining live reference owns backend state, not a live PROCESS. */
    buffer.b_dirty=TRUE; fail_disk=TRUE;
    CHECK(fs_reference_close(&b,FALSE,&report)==EWRITF && b.owned &&
          report.disposition==FS_CLOSE_RETAINED && sft[0].f_staged_refs==1);
    CHECK(fs_reference_close(&b,TRUE,&report)==EWRITF && !b.owned &&
          report.disposition==FS_CLOSE_TRANSFERRED && !sft[0].f_staged_refs &&
          sft[0].f_cleanup_refs==1);
    token=report.token;
    CHECK(fs_reference_close(&b,TRUE,&report)==EIHNDL);
    fail_disk=FALSE;
    CHECK(fs_cleanup_retry(&token,&report)==E_OK && !sft[0].f_use && freed==1);
    CHECK(fs_cleanup_retry(&token,&report)==EIHNDL);

    source_live=TRUE; retiring=FALSE; open_file(); incarnation=sft[0].f_incarnation;
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&a)==E_OK);
    file.o_dmd=&media;
    ofd_invalidate_drive(&media);
    CHECK(!sft[0].f_staged_refs && !sft[0].f_own);
    CHECK(fs_reference_close(&a,TRUE,&report)==E_CHNG && !a.owned && freed==2);
    open_file(); incarnation=sft[0].f_incarnation;
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&a)==E_OK);
    file.o_dmd=&media; ofd_invalidate_drive(&media);
    open_file();
    CHECK(fs_reference_close(&a,TRUE,&report)==EIHNDL && !a.owned &&
          sft[0].f_use==1 && !sft[0].f_staged_refs);
    CHECK(fs_close_bounded(NUMSTD,FALSE,&report)==E_OK);

    /* Same PFS cookie/position; a negative consuming close clears its owner. */
    CHECK(fs_sft_claim(0)); sft[0].f_own=&owner; sft[0].f_use=1;
    driver.close=driver_close; driver.release_file=driver_release_file;
    sft[0].f_pfs.fs=&driver; sft[0].f_pfs.pos=17;
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,sft[0].f_incarnation,&a)==E_OK);
    sft[0].f_pfs.pos++;
    CHECK(sft[a.handle-NUMSTD].f_pfs.pos==18);
    CHECK(fs_close_bounded(NUMSTD,FALSE,&report)==E_OK && sft[0].f_use==1);
    pfs_error=EWRITF; release_error=EWRITF;
    a.closing=TRUE;
    CHECK(fs_reference_close(&a,TRUE,&report)==EACCDN && a.owned);
    a.closing=FALSE;
    CHECK(fs_reference_close(&a,TRUE,&report)==EWRITF && !a.owned &&
          report.disposition==FS_CLOSE_CONSUMED && !sft[0].f_use && !sft[0].f_staged_refs);
    CHECK(fs_reference_close(&a,TRUE,&report)==EIHNDL);
    /* Releasing a staged owner must not mistake its temporarily charged count
     * for exhaustion of an ordinary alias which still owns the backend. */
    open_file(); incarnation=sft[0].f_incarnation;
    CHECK(ixforce(0,NUMSTD,&owner)==E_OK);
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&a)==E_OK);
    CHECK(fs_close_once(NUMSTD,FALSE,&report)==E_OK && sft[0].f_use==2);
    CHECK(fs_reference_close(&a,FALSE,&report)==E_OK && !a.owned &&
          owner.p_uft[0]==NUMSTD && sft[0].f_use==1 && !sft[0].f_staged_refs && fs_sft_accessible(0));
    CHECK(fs_close_once(0,FALSE,&report)==E_OK && !sft[0].f_use);
    /* Repeated numeric closes exhaust ordinary aliases while staging keeps
     * the backend alive. Neither reacquisition nor retirement may see a ghost. */
    open_file(); incarnation=sft[0].f_incarnation;
    CHECK(ixforce(0,NUMSTD,&owner)==E_OK);
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&a)==E_OK);
    CHECK(fs_close_once(NUMSTD,FALSE,&report)==E_OK && owner.p_uft[0]==NUMSTD);
    CHECK(fs_close_once(NUMSTD,FALSE,&report)==E_OK && owner.p_uft[0]==get_default_handle(0) &&
          sft[0].f_use==1 && sft[0].f_staged_refs==1 && !fs_sft_accessible(0));
    CHECK(fs_reference_acquire((KPROC *)&owner,0,incarnation,&empty)==EIHNDL && !empty.owned);
    CHECK(fs_reference_close(&a,TRUE,&report)==E_OK && !a.owned && !sft[0].f_staged_refs);
    reentrant_reference_case(FALSE,FALSE,FALSE);
    reentrant_reference_case(FALSE,TRUE,FALSE);
    reentrant_reference_case(TRUE,FALSE,FALSE);
    reentrant_reference_case(FALSE,FALSE,TRUE);
    operation_reference_case();
    operation_callback_case(FALSE);
    operation_callback_case(TRUE);
    public_control_case();
#if CONF_WITH_MULTITHREADING
    puts("PFS callback group exit deferred through final release and disk-error unwind: PASS (host scheduler model)");
#endif
    puts("Reentrant close reserves its exact reference class; no staged owner can fund an ordinary transfer: PASS (host)");
    puts("Numeric close exhaustion unpublishes standard aliases before staged owner release: PASS (host)");
    puts("Production checked live SFT refs: authenticated acquire, alias counts, retirement-independent ownership, exact close/transfer, invalidation and PFS errors: PASS (host)");
    return 0;
}
