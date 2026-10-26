/* GEM result conventions and per-process globals; transport stays private. */
#include "gem.h"
#include "libaes.h"
#include "gemerror.h"
#include "transport.h"

short aes_global[AES_GLOBMAX] = { 0, 0, -1 };
short gl_apid = -1, gl_ap_version;
static LONG application_session = -1;
static short application_id = -1;

static void clear_global(short *global)
{
    WORD i;
    for (i = 0; i < AES_GLOBMAX; i++) global[i] = 0;
    global[2] = -1;
}

static void sync_global(short *global)
{
    if (global == aes_global) {
        gl_apid = global[2];
        gl_ap_version = global[0];
    }
}

static BOOL registered(short *global)
{
    return global && application_session >= 0 && application_id > 0 &&
           global[2] == application_id && aes_session_query(application_session) == E_OK;
}

static void application_label(const char *name, char label[8])
{
    WORD i;
    for (i = 0; i < 8; i++) label[i] = ' ';
    for (i = 0; i < 8 && name[i] && name[i] != '.'; i++)
        label[i] = name[i] >= 'a' && name[i] <= 'z' ? name[i] - ('a' - 'A') : name[i];
}

static BOOL publish_label(LONG handle)
{
    char name[33], label[8];
    LONG result = cuis_call6(CUIS_SESSION_NAME, handle, (uintptr_t)name, sizeof(name), 0, 0, 0);
    if (result < 0) return FALSE;
    if (!result) return TRUE; /* Anonymous/basepage-only programs have no filename. */
    application_label(name, label);
    return cuis_call6(CUIS_SESSION_LABEL, handle, (uintptr_t)label, sizeof(label), 0, 0, 0) == E_OK;
}

short mt_appl_init(short *global)
{
    LONG pid, handle;
    if (!global) return -1;
    clear_global(global);
    pid = aes_process_call(0x10b); /* Pgetpid: an existing bounded, trusted ID namespace. */
    if (pid <= 0 || pid > 32767) { sync_global(global); return -1; }
    handle = aes_session_open();
    if (handle < 0) { sync_global(global); return -1; }
    if (!publish_label(handle)) {
        (void)aes_session_close(handle);
        application_session = -1; application_id = -1;
        if (global != aes_global) clear_global(aes_global);
        sync_global(aes_global);
        sync_global(global); return -1;
    }
    application_session = handle;
    application_id = (short)pid;
    global[0] = 0x0400; /* AES global-array ABI, not a claim of complete AES coverage. */
    global[1] = -1; /* GEM multitasking sentinel. */
    global[2] = application_id;
    sync_global(global);
    return application_id;
}

short appl_init(void) { return mt_appl_init(aes_global); }

short mt_appl_exit(short *global)
{
    if (!registered(global) || aes_session_close(application_session) != E_OK) return 0;
    application_session = -1;
    application_id = -1;
    clear_global(global);
    if (global != aes_global) clear_global(aes_global);
    sync_global(global);
    sync_global(aes_global);
    return 1;
}
short appl_exit(void) { return mt_appl_exit(aes_global); }

short mt_appl_yield(short *global)
{
    return registered(global) && aes_process_call(0xff) == E_OK ? 1 : 0;
}
short appl_yield(void) { return mt_appl_yield(aes_global); }

short mt_evnt_timer(unsigned long interval, short *global)
{
    if (!registered(global) || interval > 0xffffffffUL) return 0;
    return cuis_call6(CUIS_SESSION_DELAY, (QUAD)application_session, interval, 0, 0, 0, 0) == E_OK ? 1 : 0;
}
short evnt_timer(unsigned long interval) { return mt_evnt_timer(interval, aes_global); }

short mt_appl_getinfo(short type, short *a, short *b, short *c, short *d, short *global)
{
    /* Advertise only implemented extensions, using the standard AES query. */
    if (a) *a = 0;
    if (b) *b = 0;
    if (c) *c = 0;
    if (d) *d = 0;
    if (type == AES_PROCESS && registered(global)) {
        if (b) *b = 1; /* MiNT/AES ID conversion; scheduling remains cooperative. */
        return 1;
    }
    if (type == AES_INQUIRE && registered(global)) {
        if (a) *a = 1; /* APR_NOWAIT */
        return 1;
    }
    return 0;
}
short appl_getinfo(short type, short *a, short *b, short *c, short *d)
{
    return mt_appl_getinfo(type, a, b, c, d, aes_global);
}

short mt_appl_find(const char *name, short *global)
{
    uintptr_t value = (uintptr_t)name;
    LONG handle, pid;
    char label[8];
    if (!registered(global)) return -1;
    if ((value >> 16) == 0) return application_id;
    if ((value >> 16) == 0xffff || (value >> 16) == 0xfffe)
        handle = cuis_call6(CUIS_PEER_BY_PID, value & 0xffff, 0, 0, 0, 0, 0);
    else {
        application_label(name, label);
        handle = cuis_call6(CUIS_LABEL_FIND, (uintptr_t)label, sizeof(label), 0, 0, 0, 0);
    }
    if (handle < 0) return -1;
    pid = cuis_call6(CUIS_PEER_PID, handle, 0, 0, 0, 0, 0);
    return pid > 0 && pid <= 32767 ? (short)pid : -1;
}
short appl_find(const char *name) { return mt_appl_find(name, aes_global); }

short mt_appl_write(short id, short length, void *buffer, short *global)
{
    LONG peer;
    if (!registered(global) || id <= 0 || length < 0 || (length && !buffer)) return 0;
    peer = cuis_call6(CUIS_PEER_BY_PID, id, 0, 0, 0, 0, 0);
    if (peer < 0) return 0;
    return cuis_call6(CUIS_STREAM_WRITE, peer, (uintptr_t)buffer, length, 1, 0, 0) == length ? 1 : 0;
}
short appl_write(short id, short length, void *buffer) { return mt_appl_write(id, length, buffer, aes_global); }

short mt_appl_read(short id, short length, void *buffer, short *global)
{
    if (!registered(global) || (id != APR_NOWAIT && id != application_id) ||
        length < 0 || (length && !buffer)) return 0;
    return cuis_call6(CUIS_STREAM_READ, application_session, (uintptr_t)buffer, length,
                      id != APR_NOWAIT, 0, 0) == length ? 1 : 0;
}
short appl_read(short id, short length, void *buffer) { return mt_appl_read(id, length, buffer, aes_global); }
short mt_evnt_mesag(short message[8], short *global)
{
    return global ? mt_appl_read(global[2], 16, message, global) : 0;
}
short evnt_mesag(short message[8]) { return mt_evnt_mesag(message, aes_global); }
