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
static BOOL reset_fails;
LONG aes_application_session(WORD *global) { (void)global; return 42; }
WORD mt_wind_new(WORD *global)
{
    (void)global; resets++;
    if (reset_fails) return 0;
    windows = 0; return 1;
}
LONG cuis_call6(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e, UQUAD f)
{
    (void)a; (void)e; (void)f;
    if (fn == CUIS_COHORT_BEGIN) { work = 0; return ++token; }
    if (fn == CUIS_PEER_NEXT) return EFILNF;
    if (fn == CUIS_COHORT_COMMIT) return E_OK;
    if (fn == CUIS_COHORT_CANCEL) { token = 0; return E_OK; }
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
    read_offset = EFILNF; read_bytes = 0; reset_fails = FALSE; windows = 2;
    aes_lifecycle_write(SWM_NEWMSG,consent,0,NULL,NULL);
}
static LONG next(WORD message[8])
{
    memcpy(message,(UBYTE *)stream+consumed,16);
    read_offset = consumed >= request_start && consumed+16 <= request_start+bytes ?
        (LONG)(consumed-request_start) : EFILNF;
    read_bytes = 16; consumed += 16;
    return aes_lifecycle_deliver(message,16,NULL);
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
    VERIFY(aes_lifecycle_deliver(term,16,NULL) == EAGAIN);
    read_offset = 0; read_bytes = 8;
    VERIFY(aes_lifecycle_deliver(term,16,NULL) == EAGAIN);
    read_bytes = 16; read_offset = 1;
    VERIFY(aes_lifecycle_deliver(term,16,NULL) == EAGAIN);
    token = 0;
    VERIFY(aes_lifecycle_deliver(term,16,NULL) == EAGAIN);
    begin(FALSE,NM_APTERM,term,16);
    VERIFY(aes_lifecycle_messages(NULL) == E_OK && published == 1 && next(message) == E_OK && !resets);
    begin(TRUE,NM_APTERM,request,48); reset_fails = TRUE;
    VERIFY(aes_lifecycle_messages(NULL) == E_OK && next(message) == EAGAIN && rejected == 1);
    VERIFY(windows == 2);
    aes_lifecycle_reset(FALSE);
    VERIFY(aes_lifecycle_write(SWM_SHUTDOWN,0,SD_COMPLETE,NULL,NULL) == 1);
    old_token = token;
    /* Ordinary-write refusals cannot bypass native participant checks. */
    result_id = EFILNF; result_offset = 0; result_bytes = 16;
    VERIFY(aes_lifecycle_deliver(refusal,16,NULL) == EAGAIN);
    VERIFY(aes_lifecycle_write(SWM_SHUTDOWN,0,SD_ABORT,NULL,NULL) == 1);
    token = old_token; /* The transport never recycles tokens. */
    VERIFY(aes_lifecycle_write(SWM_SHUTDOWN,0,SD_COMPLETE,NULL,NULL) == 1);
    new_token = token; VERIFY(new_token > old_token);
    result_id = old_token;
    VERIFY(aes_lifecycle_deliver(refusal,16,NULL) == EAGAIN);
    result_id = new_token; result_bytes = 8;
    VERIFY(aes_lifecycle_deliver(refusal,16,NULL) == EAGAIN);
    result_bytes = 16; result_offset = 8;
    VERIFY(aes_lifecycle_deliver(refusal,16,NULL) == EAGAIN);
    result_offset = 0;
    /* The correct refusal survives global cohort teardown before delivery. */
    token = 0;
    VERIFY(aes_lifecycle_messages(NULL) == E_OK);
    VERIFY(aes_lifecycle_deliver(refusal,16,NULL) == E_OK);
    VERIFY(aes_lifecycle_deliver(refusal,16,NULL) == EAGAIN);
    puts("Lifecycle receiver consent, complete preflight, provenance and ordered reset: PASS");
    return 0;
}
