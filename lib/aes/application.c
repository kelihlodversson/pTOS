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

short mt_appl_init(short *global)
{
    LONG pid, handle;
    if (!global) return -1;
    clear_global(global);
    pid = aes_process_call(0x10b); /* Pgetpid: an existing bounded, trusted ID namespace. */
    if (pid <= 0 || pid > 32767) { sync_global(global); return -1; }
    handle = aes_session_open();
    if (handle < 0) { sync_global(global); return -1; }
    application_session = handle;
    application_id = (short)pid;
    global[0] = 0x0400; /* AES global-array ABI, not a claim of complete AES coverage. */
    global[1] = -1; /* Multiple processes; no fixed application count. */
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
    (void)type; (void)global;
    /* No optional query/extension is advertised before its implementation. */
    if (a) *a = 0;
    if (b) *b = 0;
    if (c) *c = 0;
    if (d) *d = 0;
    return 0;
}
short appl_getinfo(short type, short *a, short *b, short *c, short *d)
{
    return mt_appl_getinfo(type, a, b, c, d, aes_global);
}
