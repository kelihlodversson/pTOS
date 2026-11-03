/* Receiver policy and delivery ordering over an untrusted native request. */
#include <stdio.h>
#include "gem.h"
#include "gemerror.h"
#include "lifecycle.h"
#include "application.h"
#include "transport.h"
#include "string.h"
#define VERIFY(c) do { if (!(c)) { fprintf(stderr,"lifecycle:%d\n",__LINE__); return 1; } } while (0)
static WORD packet[CUIS_COHORT_BYTES/2], stream[256];
static LONG token, work, tag, read_offset, read_bytes;
static LONG result_id = EFILNF, result_offset, result_bytes;
static ULONG bytes, queued, consumed, request_start, published, rejected, resets, windows;
static BOOL reset_fails, reset_busy, last_poll, pending_notice, notice_blocked;
static LONG phase;
LONG aes_window_messages_poll(LONG session, WORD *global)
{
    (void)session; (void)global;
    if (pending_notice && !notice_blocked) {
        memset((UBYTE *)stream+queued,0,16);
        stream[queued/2] = WM_REDRAW; stream[queued/2+3] = 7;
        queued += 16; pending_notice = FALSE;
    }
    return E_OK;
}
BOOL aes_frames_wait_display(void) { return notice_blocked; }
BOOL aes_windows_pending(void) { return FALSE; }
LONG aes_application_session(WORD *global) { (void)global; return 42; }
LONG aes_windows_reset_policy(WORD *global, BOOL polling, LONG token)
{
    (void)global; (void)token; last_poll = polling;
    if (reset_busy) return EAGAIN;
    resets++;
    if (reset_fails) return EACCDN;
    windows = 0; return E_OK;
}
LONG cuis_call6(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e, UQUAD f)
{
    (void)a; (void)e; (void)f;
    if (fn == CUIS_COHORT_BEGIN) { work = 0; return ++token; }
    if (fn == CUIS_PEER_NEXT) return EFILNF;
    if (fn == CUIS_COHORT_COMMIT) return E_OK;
    if (fn == CUIS_COHORT_CANCEL) { token = 0; return E_OK; }
    if (fn == CUIS_STREAM_PENDING) return queued-consumed;
    if (fn == CUIS_STREAM_PEEK || fn == CUIS_STREAM_READ) {
        if (queued-consumed < c) return EAGAIN;
        memcpy((void *)(uintptr_t)b,(UBYTE *)stream+consumed,c);
        read_offset = consumed >= request_start && consumed+c <= request_start+bytes ?
            (LONG)(consumed-request_start) : EFILNF;
        read_bytes = c;
        if (fn == CUIS_STREAM_READ) consumed += c;
        return c;
    }
    if (fn == CUIS_PEER_QUERY) return tag;
    if (fn == CUIS_SESSION_TAG) { tag = (LONG)b; return E_OK; }
    if (fn == CUIS_ENTRY_OWNER) return b < 2 ? 42 : EFILNF;
    if (fn == CUIS_COHORT_QUERY) {
        if (b == CUIS_COHORT_RESULT_ID) return result_id;
        if (b == CUIS_COHORT_RESULT_OFFSET) return result_offset;
        if (b == CUIS_COHORT_RESULT_BYTES) return result_bytes;
        if (!token) return EFILNF;
        if (b == CUIS_COHORT_ID) return token;
        if (b == CUIS_COHORT_WORK) return work;
        if (b == CUIS_COHORT_STATE) return phase;
        if (b == CUIS_COHORT_READ_OFFSET) return read_offset;
        if (b == CUIS_COHORT_READ_BYTES) return read_bytes;
    }
    if ((LONG)b != token) return EACCDN;
    if (fn == CUIS_COHORT_READ) { memcpy((void *)(uintptr_t)c,packet,bytes); return bytes; }
    if (fn == CUIS_COHORT_REJECT) { rejected++; work = 0; return E_OK; }
    if (fn == CUIS_COHORT_PUBLISH) {
        request_start = queued; memcpy((UBYTE *)stream+queued,packet,bytes);
        queued += bytes; published++; work = 0; return E_OK;
    }
    (void)d; return EINVFN;
}
static void begin(BOOL acc, WORD consent, const WORD *request, ULONG size)
{
    aes_lifecycle_reset(acc);
    token++; work = CUIS_COHORT_REQUEST; tag = acc ? APP_ACCESSORY : APP_APPLICATION;
    bytes = size; memcpy(packet,request,size);
    queued = consumed = published = rejected = resets = 0;
    read_offset = EFILNF; read_bytes = 0; reset_fails = reset_busy = pending_notice = notice_blocked = FALSE; phase = 2; windows = 2;
    aes_lifecycle_write(SWM_NEWMSG,consent,0,NULL,NULL);
}
static LONG next(WORD message[8])
{
    LONG result = aes_lifecycle_read(message,16,NULL,FALSE);
    return result == 16 ? E_OK : result == 0 ? EAGAIN : result;
}
int main(void)
{
    WORD request[24] = { AC_CLOSE,0,0,0,0,0,0,0,
                         AC_CLOSE,0,0,1,0,0,0,0,
                         AP_TERM,-1,0,0,0,AP_TERM,0,0 };
    LONG old_token, new_token;
    WORD refusal[8] = { AP_TFAIL,23,0,0,0,0,0,0 };
    WORD message[8], term[8] = { AP_TERM,-1,0,0,0,AP_TERM,0,0 };
    begin(FALSE,NM_APTERM,request,16); /* AC_CLOSE cannot reset an application. */
    VERIFY(aes_lifecycle_messages(NULL) == E_OK && rejected == 1 && !published && !resets && windows == 2);
    request[8] = 1234;
    begin(TRUE,NM_APTERM,request,32); /* Validate the suffix before any effect. */
    VERIFY(aes_lifecycle_messages(NULL) == E_OK && rejected == 1 && !published && !resets && windows == 2);
    request[8] = AC_CLOSE;
    begin(TRUE,0,request,48); /* Accessory did not consent to AP_TERM. */
    VERIFY(aes_lifecycle_messages(NULL) == E_OK && rejected == 1 && !published && !resets);
    request[3] = 2;
    begin(TRUE,NM_APTERM,request,48); /* Only the receiver's registered IDs. */
    VERIFY(aes_lifecycle_messages(NULL) == E_OK && rejected == 1 && !published && !resets);
    request[3] = 0;
    begin(TRUE,NM_APTERM,request,48);
    /* A queued AC_OPEN and window message must finish before the reset. */
    memset(stream,0,32); stream[0] = AC_OPEN; stream[8] = WM_REDRAW; queued = 32;
    VERIFY(aes_lifecycle_messages(NULL) == E_OK && published == 1 && !rejected && !resets && windows == 2);
    VERIFY(next(message) == E_OK && message[0] == AC_OPEN); windows++;
    VERIFY(next(message) == E_OK && message[0] == WM_REDRAW && windows == 3 && !resets);
    VERIFY(next(message) == E_OK && message[0] == AC_CLOSE && resets == 1 && !windows);
    VERIFY(next(message) == E_OK && message[0] == AC_CLOSE && resets == 1);
    VERIFY(next(message) == E_OK && message[0] == AP_TERM && resets == 1);
    /* Identical payloads without authenticated stream provenance are ignored. */
    read_offset = EFILNF;
    VERIFY(aes_lifecycle_deliver(term,16,NULL,FALSE) == EAGAIN);
    read_offset = 0; read_bytes = 8;
    VERIFY(aes_lifecycle_deliver(term,16,NULL,FALSE) == EAGAIN);
    read_bytes = 16; read_offset = 1;
    VERIFY(aes_lifecycle_deliver(term,16,NULL,FALSE) == EAGAIN);
    token = 0;
    VERIFY(aes_lifecycle_deliver(term,16,NULL,FALSE) == EAGAIN);
    begin(FALSE,NM_APTERM,term,16);
    VERIFY(aes_lifecycle_messages(NULL) == E_OK && published == 1 && next(message) == E_OK && !resets);
    begin(TRUE,NM_APTERM,request,48); reset_fails = TRUE;
    VERIFY(aes_lifecycle_messages(NULL) == E_OK && next(message) == EAGAIN && rejected == 1);
    VERIFY(windows == 2);
    /* Pending native notices are flushed before the lifecycle append. */
    begin(TRUE,0,request,32); pending_notice = notice_blocked = TRUE;
    VERIFY(aes_lifecycle_messages(NULL) == E_OK && !published && !resets);
    notice_blocked = FALSE;
    VERIFY(aes_lifecycle_messages(NULL) == E_OK && published == 1);
    VERIFY(next(message) == E_OK && message[0] == WM_REDRAW && !resets);
    /* A late notice behind the request is obsolete even if its ID is reused. */
    memset((UBYTE *)stream+queued,0,32);
    stream[queued/2] = WM_REDRAW; stream[queued/2+3] = 7;
    stream[queued/2+8] = 1234; queued += 32;
    reset_busy = TRUE;
    VERIFY(aes_lifecycle_read(message,16,NULL,TRUE) == EAGAIN && last_poll);
    VERIFY(consumed == 16 && !resets && aes_lifecycle_wait_display() && !rejected);
    VERIFY(aes_lifecycle_read(message,16,NULL,TRUE) == EAGAIN && consumed == 16);
    reset_busy = FALSE;
    VERIFY(aes_lifecycle_read(message,16,NULL,TRUE) == 16 && message[0] == AC_CLOSE && resets == 1);
    VERIFY(!aes_lifecycle_wait_display()); windows = 1; /* A new window reused ID 7. */
    VERIFY(aes_lifecycle_read(message,16,NULL,TRUE) == 16 && message[0] == AC_CLOSE);
    VERIFY(aes_lifecycle_read(message,16,NULL,TRUE) == 0 && windows == 1);
    VERIFY(aes_lifecycle_read(message,16,NULL,TRUE) == 16 && message[0] == 1234);
    begin(TRUE,0,request,32);
    VERIFY(aes_lifecycle_messages(NULL) == E_OK);
    reset_busy = TRUE;
    VERIFY(aes_lifecycle_read(message,16,NULL,TRUE) == EAGAIN && aes_lifecycle_wait_display());
    token = 0;
    VERIFY(aes_lifecycle_messages(NULL) == E_OK && !aes_lifecycle_wait_display());
    VERIFY(aes_lifecycle_read(message,16,NULL,TRUE) == 0 && !resets);
    aes_lifecycle_reset(FALSE);
    VERIFY(aes_lifecycle_write(SWM_SHUTDOWN,0,SD_COMPLETE,NULL,NULL) == 1);
    old_token = token;
    /* Ordinary-write refusals cannot bypass native participant checks. */
    result_id = EFILNF; result_offset = 0; result_bytes = 16;
    VERIFY(aes_lifecycle_deliver(refusal,16,NULL,FALSE) == EAGAIN);
    VERIFY(aes_lifecycle_write(SWM_SHUTDOWN,0,SD_ABORT,NULL,NULL) == 1);
    token = old_token; /* The transport never recycles tokens. */
    VERIFY(aes_lifecycle_write(SWM_SHUTDOWN,0,SD_COMPLETE,NULL,NULL) == 1);
    new_token = token; VERIFY(new_token > old_token);
    result_id = old_token;
    VERIFY(aes_lifecycle_deliver(refusal,16,NULL,FALSE) == EAGAIN);
    result_id = new_token; result_bytes = 8;
    VERIFY(aes_lifecycle_deliver(refusal,16,NULL,FALSE) == EAGAIN);
    result_bytes = 16; result_offset = 8;
    VERIFY(aes_lifecycle_deliver(refusal,16,NULL,FALSE) == EAGAIN);
    result_offset = 0;
    /* The correct refusal survives global cohort teardown before delivery. */
    token = 0;
    VERIFY(aes_lifecycle_messages(NULL) == E_OK);
    VERIFY(aes_lifecycle_deliver(refusal,16,NULL,FALSE) == E_OK);
    VERIFY(aes_lifecycle_deliver(refusal,16,NULL,FALSE) == EAGAIN);
    /* Authenticated rejection bytes are not a successful completion. */
    token = new_token;
    VERIFY(aes_lifecycle_write(SWM_SHUTDOWN,0,SD_COMPLETE,NULL,NULL) == 1);
    result_id = token; phase = 3;
    message[0] = SHUT_COMPLETED;
    VERIFY(aes_lifecycle_deliver(message,16,NULL,FALSE) == E_OK && message[0] == AP_TFAIL);
    VERIFY(aes_lifecycle_write(SWM_SHUTDOWN,0,SD_COMPLETE,NULL,NULL) == 1);
    result_id = token; phase = CUIS_COHORT_FINISHED; message[0] = SHUT_COMPLETED;
    VERIFY(aes_lifecycle_deliver(message,16,NULL,FALSE) == E_OK && message[0] == SHUT_COMPLETED);
    puts("Lifecycle receiver consent, complete preflight, provenance and ordered reset: PASS");
    return 0;
}
