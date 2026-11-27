/* GEMDOS descriptor readiness and scheduler-backed monotonic waits. */
#include "emutos.h"
#include "bdosstub.h"
#include "biosbind.h"
#include "console.h"
#include "fpoll_kernel.h"
#include "fs.h"
#include "gemerror.h"
#include "kheap.h"
#include "kproc.h"
#include "ksched.h"
#include "ksched_sequence.h"
#include "process_files.h"
#include "user_callback.h"

#define POLL_READ_EVENTS  (PTOS_POLLIN | PTOS_POLLRDNORM)
#define POLL_WRITE_EVENTS (PTOS_POLLOUT | PTOS_POLLWRNORM)
#define POLL_PRI_EVENTS   (PTOS_POLLPRI | PTOS_POLLRDBAND)

typedef struct {
    FS_FILE_REFERENCE file;
    UQUAD generation;
    WORD bios;
} POLL_REFERENCE;

typedef struct {
    const PROCESS_FILES *files;
    UQUAD epoch;
    UQUAD deadline;
    UQUAD sample_tick;
    BOOL finite, sample;
} POLL_WAIT;

static volatile UQUAD ticks;
static KSCHED_WAIT_SEQUENCE readiness_changes;

void fpoll_tick(void)
{
    ticks++;
}

void fpoll_notify(void)
{
    ksched_wake_sequence(&readiness_changes);
}

static UQUAD poll_now(void)
{
    uintptr_t flags = kowner_irq_save();
    UQUAD now = ticks;
    kowner_irq_restore(flags);
    return now;
}

static BOOL poll_user_context_allowed(void)
{
#if CONF_WITH_USER_CALLBACKS
    if (user_callback_active())
        return FALSE;
#endif
    return ksched_spawn_allowed() && ksched_exit_thread(run);
}

static BOOL deadline_reached(UQUAD now, UQUAD deadline)
{
    /* The largest supported interval is shorter than half the tick range. */
    return now - deadline < (1ULL << 63);
}

static BOOL poll_wake(void *argument)
{
    const POLL_WAIT *wait = argument;
    UQUAD now = poll_now();

    return (wait->finite && deadline_reached(now, wait->deadline)) ||
           (wait->sample && now != wait->sample_tick) ||
           (wait->files && (wait->files->retiring || wait->files->epoch != wait->epoch));
}

static LONG bios_status(WORD handle, UWORD events)
{
    LONG ready = 0;

    if ((events & FS_READY_READ) && console_input_status(HXFORM(handle)))
        ready |= FS_READY_READ;
    if ((events & FS_READY_WRITE) && Bcostat(HXFORM(handle)))
        ready |= FS_READY_WRITE;
    return ready;
}

static LONG poll_pin(const PROCESS_FILES *files, LONG fd, POLL_REFERENCE *reference)
{
    LONG error;

    if (fd < 0 || fd >= NUMHANDLES)
        return E_OK;
    error = process_files_poll_pin(files, (WORD)fd, &reference->file,
                                   &reference->bios, &reference->generation);
    return error == EIHNDL || error == EACCDN ? E_OK : error;
}

static LONG poll_bios_status(const POLL_REFERENCE *reference, UWORD events)
{
    if (reference->file.owned) {
        FS_HANDLE handle;
        LONG error = fs_reference_lookup(&reference->file, &handle);

        if (error)
            return error == EACCDN ? EAGAIN : error;
    }
    return bios_status(reference->bios, events);
}

static BOOL poll_binding_current(const PROCESS_FILES *files, LONG fd,
                                 const POLL_REFERENCE *reference)
{
    return reference->generation &&
           files->binding[fd].generation == reference->generation;
}

static UWORD poll_revents(const PTOS_POLLFD *fd, const POLL_REFERENCE *reference,
                         POLL_WAIT *wait)
{
    UWORD events = 0, result = 0;
    LONG ready;

    if (fd->fd < 0)
        return 0;
    if (!poll_binding_current(wait->files, fd->fd, reference))
        return PTOS_POLLNVAL;

    if (fd->events & POLL_READ_EVENTS) events |= FS_READY_READ;
    if (fd->events & POLL_WRITE_EVENTS) events |= FS_READY_WRITE;
    if (fd->events & POLL_PRI_EVENTS) events |= FS_READY_PRIORITY;

    if (reference->bios) {
        /* Legacy BIOS status vectors lack a notification contract. Recheck
         * interested devices at the tick, on their own THREAD's stack. */
        ready = DISK_CALL(poll_bios_status(reference, events));
        if ((events & FS_READY_WRITE) ||
            ((events & FS_READY_READ) && reference->bios != H_Print))
            wait->sample = TRUE;
    } else {
        ready = DISK_CALL(fs_reference_ready(&reference->file, events));
    }
    if (ready == EAGAIN) {
        /* An admitted alias can be flushing the same description. */
        wait->sample = TRUE;
        return 0;
    }
    if (ready == EIHNDL || ready == E_CHNG) return PTOS_POLLNVAL;
    if (ready < 0) return PTOS_POLLERR;

    if (ready & FS_READY_READ) result |= fd->events & POLL_READ_EVENTS;
    if (ready & FS_READY_WRITE) result |= fd->events & POLL_WRITE_EVENTS;
    if (ready & FS_READY_PRIORITY) result |= fd->events & POLL_PRI_EVENTS;
    if (ready & FS_READY_ERROR) result |= PTOS_POLLERR;
    if (ready & FS_READY_HANGUP) result |= PTOS_POLLHUP;
    return result;
}

static LONG poll_wait(PTOS_POLLFD *fds, ULONG nfds, QUAD timeout, UQUAD start)
{
    PROCESS_FILE_OPERATION operation = PROCESS_FILE_OPERATION_INIT;
    KSCHED_CLEANUP_SCOPE cleanup = {0};
    KPROC *owner = NULL;
    POLL_REFERENCE *references = NULL;
    POLL_WAIT wait = {0};
    ULONG i;
    LONG result = E_OK;

    ksched_cleanup_begin(&cleanup);
    if (nfds) {
        result = process_files_operation_begin(run, &operation);
        if (result)
            goto done;
        if (!operation.table) {
            result = EINVFN;
            goto done;
        }
        wait.files = operation.table;
        references = kalloc(nfds * sizeof(*references));
        if (!references) {
            result = ENSMEM;
            goto done;
        }
        for (i = 0; i < nfds; i++) {
            result = poll_pin(wait.files, fds[i].fd, &references[i]);
            if (result)
                goto done;
        }
        /* Snapshot admission ends with the non-yielding pins. The separate
         * identity reference owns the table until these pins are released. */
        owner = kproc_retain_live(operation.owner);
        if (!owner) {
            result = EACCDN;
            goto done;
        }
        process_files_operation_end(&operation);
    }

    wait.finite = timeout > 0;
    /* A tick may arrive immediately after start. Round up the interval and
     * allow that partial initial tick to avoid completing a sleep early. */
    if (wait.finite)
        wait.deadline = start + ((UQUAD)timeout + 4) / 5 + 1;

    for (;;) {
        KSCHED_WAIT_OBSERVATION observation;
        KSCHED_WAIT_RESULT status;

        if (ksched_cancellation_pending() || (wait.files && wait.files->retiring)) {
            result = EACCDN;
            break;
        }
        ksched_sequence_observe(&readiness_changes, &observation);
        wait.sample_tick = poll_now();
        wait.sample = FALSE;

        for (i = 0; i < nfds; i++)
            fds[i].revents = poll_revents(&fds[i], &references[i], &wait);

        /* A later BIOS status vector can reenter and invalidate an earlier
         * sample. Validate every binding after the last vector has returned. */
        if (wait.files && wait.files->retiring) {
            result = EACCDN;
            break;
        }
        result = 0;
        for (i = 0; i < nfds; i++) {
            if (fds[i].fd >= 0 && !poll_binding_current(wait.files, fds[i].fd, &references[i]))
                fds[i].revents = PTOS_POLLNVAL;
            if (fds[i].revents)
                result++;
        }
        if (result || !timeout || (wait.finite && deadline_reached(poll_now(), wait.deadline)))
            break;
        /* Changes during the scan are accounted for by binding validation.
         * Use the current epoch to observe changes while asleep. */
        wait.epoch = wait.files ? wait.files->epoch : 0;
        status = ksched_wait_cancelable_observed(poll_wake, &wait, &observation);
        if (status != KSCHED_WAIT_READY) {
            result = status == KSCHED_WAIT_CANCELLED ? EACCDN : EINVFN;
            break;
        }
    }

done:
    process_files_operation_end(&operation);
    if (references) {
        for (i = 0; i < nfds; i++)
            if (references[i].file.owned)
                fs_reference_unpin(&references[i].file);
        kfree(references);
    }
    kproc_release(owner);
    if (ksched_cancellation_pending())
        result = EACCDN;
    ksched_cleanup_end(&cleanup);
    return result;
}

LONG xfpoll_time64(UQUAD address, UQUAD nfds, QUAD timeout)
{
    KPROC_USER_COPY output = {0};
    PTOS_POLLFD *fds;
    ULONG bytes;
    LONG result;
    UQUAD start = poll_now();

    if (timeout < -1 || nfds > PTOS_POLL_MAX)
        return EINVAL;
    if (!poll_user_context_allowed())
        return EINVFN;
    if (!nfds)
        return poll_wait(NULL, 0, timeout, start);
    if (!address || (VADDR)address != address)
        return EIMBA;

    bytes = (ULONG)nfds * sizeof(*fds);
    fds = kalloc(bytes);
    if (!fds)
        return ENSMEM;
    result = kproc_user_copy_begin(&output, (VADDR)address, bytes, TRUE);
    if (!result)
        result = kproc_copy_from_user_checked(fds, (VADDR)address, bytes);
    if (!result)
        result = poll_wait(fds, (ULONG)nfds, timeout, start);
    if (result >= 0 && !kproc_user_copy_write(&output, fds))
        result = EIMBA;
    kproc_user_copy_end(&output);
    kfree(fds);
    return result;
}

#if ARCH_M68K
LONG xfselect(UWORD timeout, ULONG read_mask, ULONG write_mask, ULONG except_mask)
{
    static const UWORD event[3] = { PTOS_POLLIN, PTOS_POLLOUT, PTOS_POLLPRI };
    ULONG address[3] = { read_mask, write_mask, except_mask };
    KPROC_USER_COPY copy[3] = {{0}};
    ULONG masks[3] = {0}, requested;
    PTOS_POLLFD fds[32];
    ULONG i, j, nfds = 0;
    UQUAD start = poll_now();
    LONG result = E_OK;

    if (!poll_user_context_allowed())
        return EINVFN;
    for (i = 0; i < 3 && !result; i++) {
        if (!address[i])
            continue;
        result = kproc_user_copy_begin(&copy[i], address[i], sizeof(masks[i]), TRUE);
        if (!result)
            result = kproc_copy_from_user_checked(&masks[i], address[i], sizeof(masks[i]));
    }
    if (result)
        goto done;

    requested = masks[0] | masks[1] | masks[2];
    for (i = 0; i < 32; i++) {
        ULONG bit = 1UL << i;

        if (!(requested & bit))
            continue;
        fds[nfds].fd = i;
        fds[nfds].events = 0;
        for (j = 0; j < 3; j++)
            if (masks[j] & bit)
                fds[nfds].events |= event[j];
        nfds++;
    }

    result = poll_wait(fds, nfds, timeout ? (QUAD)timeout : -1, start);
    if (result < 0)
        goto done;
    result = 0;
    masks[0] = masks[1] = masks[2] = 0;
    for (i = 0; i < nfds; i++) {
        if (fds[i].revents & PTOS_POLLNVAL) {
            masks[0] = masks[1] = masks[2] = 0;
            result = EIHNDL;
            break;
        }
        for (j = 0; j < 3; j++) {
            UWORD ready = event[j] | PTOS_POLLERR | PTOS_POLLHUP;

            if ((fds[i].events & event[j]) && (fds[i].revents & ready)) {
                masks[j] |= 1UL << fds[i].fd;
                result++;
            }
        }
    }
    /* Write each shared bitmap once, after counting readiness per mask. */
    for (i = 0; i < 3; i++) {
        if (!address[i])
            continue;
        for (j = i + 1; j < 3; j++) {
            if (address[j] == address[i]) {
                masks[i] |= masks[j];
                address[j] = 0;
            }
        }
        if (!kproc_user_copy_write(&copy[i], &masks[i]))
            result = EIMBA;
    }

done:
    for (i = 3; i; i--)
        kproc_user_copy_end(&copy[i-1]);
    return result;
}
#endif
