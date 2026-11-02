/* Standard GEM shutdown coordination. No GUI wire format crosses into the
 * native cohort implementation as anything other than bounded opaque bytes.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gem.h"
#include "gemerror.h"
#include "application.h"
#include "transport.h"
#include "lifecycle.h"
#include "string.h"

static BOOL accessory, coordinator;
static WORD capabilities;
static LONG reset_token;
void aes_lifecycle_reset(BOOL acc)
{
    accessory = acc; coordinator = FALSE; capabilities = 0; reset_token = 0;
}
BOOL aes_lifecycle_listening(void) { return accessory || coordinator || (capabilities & NM_APTERM); }

static LONG reject(LONG session, LONG token)
{
    WORD failure[8] = { AP_TFAIL,1,0,0,0,0,0,0 };
    return cuis_call6(CUIS_COHORT_REJECT,session,token,(uintptr_t)failure,sizeof(failure),0,0);
}
LONG aes_lifecycle_messages(WORD *global)
{
    WORD data[CUIS_COHORT_BYTES/2];
    LONG session, token, work, bytes, result;
    ULONG i;
    if (!aes_lifecycle_listening()) return E_OK;
    session = aes_application_session(global);
    if (session < 0) return session;
    token = cuis_call6(CUIS_COHORT_QUERY,session,CUIS_COHORT_ID,0,0,0,0);
    if (token == EFILNF) { coordinator = FALSE; return E_OK; }
    if (token < 0) return token;
    work = cuis_call6(CUIS_COHORT_QUERY,session,CUIS_COHORT_WORK,0,0,0,0);
    if (work < 0) return work == EACCDN ? E_OK : work;
    if (!work) return E_OK;
    if (work == CUIS_COHORT_ACCEPT)
        result = cuis_call6(CUIS_COHORT_ACK,session,token,0,0,0,0);
    else {
        bytes = cuis_call6(CUIS_COHORT_READ,session,token,(uintptr_t)data,sizeof(data),0,0);
        if (bytes < 0) return bytes == EACCDN || bytes == EAGAIN ? E_OK : bytes;
        if (work == CUIS_COHORT_REQUEST) {
            if (!bytes || bytes % 16) { (void)reject(session,token); return E_OK; }
            for (i = 0; i < (ULONG)bytes/2; i += 8) {
                if (data[i] != AC_CLOSE && data[i] != AP_TERM) { (void)reject(session,token); return E_OK; }
                if (data[i] == AC_CLOSE && reset_token != token) {
                    if (!mt_wind_new(global)) { (void)reject(session,token); return E_OK; }
                    reset_token = token;
                }
            }
        }
        result = cuis_call6(CUIS_COHORT_PUBLISH,session,token,0,0,0,0);
    }
    return result == EAGAIN || result == EACCDN ? E_OK : result;
}

WORD aes_lifecycle_write(WORD mode, WORD flags, WORD action, const void *command, WORD *global)
{
    LONG session = aes_application_session(global), token, peer, after, tag, result;
    WORD data[CUIS_COHORT_BYTES/2], completion[8] = { SHUT_COMPLETED,0,0,0,0,0,0,0 };
    ULONG slot, used;
    BOOL acc;
    if (session < 0) return 0;
    if (mode == SWM_REZCHANGE) return 0; /* x32 has no live mode-switch backend. */
    if (mode == SWM_NEWMSG) {
        if ((UWORD)flags & ~NM_APTERM) return 0;
        tag = cuis_call6(CUIS_PEER_QUERY,session,session,CUIS_PEER_TAG,0,0,0);
        if (tag < 0) return 0;
        tag = (tag & 255) | ((LONG)flags << 8);
        if (cuis_call6(CUIS_SESSION_TAG,session,tag,0,0,0,0) != E_OK) return 0;
        capabilities = flags; return 1;
    }
    if (mode == SWM_AESMSG) {
        if (!command) return 0;
        memcpy(data,command,16);
        if (data[0] != AP_TFAIL) return 0;
        token = cuis_call6(CUIS_COHORT_QUERY,session,CUIS_COHORT_ID,0,0,0,0);
        return token > 0 && cuis_call6(CUIS_COHORT_REJECT,session,token,(uintptr_t)data,16,16,0) == E_OK;
    }
    if (mode != SWM_SHUTDOWN || action < SD_ABORT || action > SD_COMPLETE) return 0;
    if (action == SD_ABORT) {
        token = cuis_call6(CUIS_COHORT_QUERY,session,CUIS_COHORT_ID,0,0,0,0);
        if (token <= 0 || cuis_call6(CUIS_COHORT_CANCEL,session,token,0,0,0,0) != E_OK) return 0;
        coordinator = FALSE; return 1;
    }
    token = cuis_call6(CUIS_COHORT_BEGIN,session,0,0,0,0,0);
    if (token <= 0) return 0;
    coordinator = TRUE; after = -1;
    for (;;) {
        peer = cuis_call6(CUIS_PEER_NEXT,session,(QUAD)after,0,0,0,0);
        if (peer == EFILNF) break;
        if (peer < 0) goto failed;
        after = peer;
        if (peer == session) continue;
        tag = cuis_call6(CUIS_PEER_QUERY,session,peer,CUIS_PEER_TAG,0,0,0);
        if (tag == EFILNF) continue;
        if (tag < 0 || ((tag & 255) != APP_APPLICATION && (tag & 255) != APP_ACCESSORY)) goto failed;
        acc = (tag & 255) == APP_ACCESSORY;
        if ((!acc || action == SD_COMPLETE) && !(tag & (NM_APTERM << 8))) goto failed;
        memset(data,0,sizeof(data)); used = 0;
        if (acc) {
            for (slot = 0; slot < 6; slot++) {
                result = cuis_call6(CUIS_ENTRY_OWNER,session,slot,0,0,0,0);
                if (result == peer) { data[used] = AC_CLOSE; data[used+3] = slot; used += 8; }
                else if (result < 0 && result != EFILNF) goto failed;
            }
            if (!used) goto failed; /* An accessory is still starting up. */
        }
        if (!acc || action == SD_COMPLETE) {
            data[used] = AP_TERM; data[used+1] = -1; data[used+5] = AP_TERM; used += 8;
        }
        result = cuis_call6(CUIS_COHORT_STAGE,session,token,peer,(uintptr_t)data,used*2,!acc || action == SD_COMPLETE);
        if (result != E_OK && result != EFILNF) goto failed;
    }
    if (cuis_call6(CUIS_COHORT_COMMIT,session,token,(uintptr_t)completion,sizeof(completion),0,0) != E_OK) goto failed;
    return 1;
failed:
    (void)cuis_call6(CUIS_COHORT_CANCEL,session,token,0,0,0,0);
    coordinator = FALSE; return 0;
}
