/* GEM result conventions and per-process globals; transport stays private. */
#include "libaes.h"
#include "native_bindings.h"
#include "gemerror.h"
#include "transport.h"
#include "application.h"
#include "menubar.h"
#include "graphics.h"
#include "desktop.h"
#include "lifecycle.h"
#include "string.h"

static LONG application_session = -1;
static short application_id = -1;
static LONG search_after = -1;
static BOOL application_accessory;

static void clear_global(AESGLOBAL *global)
{
    global->ap_version = 0;
    global->ap_count = 0;
    global->ap_id = -1;
    global->ap_ptree = NULL;
    global->ap_rscmem = NULL;
    global->ap_rsclen = 0;
    global->ap_planes = 0;
    global->ap_3resv = NULL;
    global->ap_4resv = 0;
}

static BOOL registered(AESGLOBAL *global)
{
    return global && application_session >= 0 && application_id > 0 &&
           global->ap_id == application_id && aes_session_query(application_session) == E_OK;
}

LONG aes_application_session(AESGLOBAL *global)
{
    return registered(global) ? application_session : EACCDN;
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
    ULONG length, tag = APP_APPLICATION;
    LONG result = cuis_call6(CUIS_SESSION_NAME, handle, (uintptr_t)name, sizeof(name), 0, 0, 0);
    if (result < 0) return FALSE;
    length = (ULONG)result;
    if (length >= 4 && name[length-4] == '.' &&
        (name[length-3] == 'A' || name[length-3] == 'a') &&
        (name[length-2] == 'C' || name[length-2] == 'c') &&
        (name[length-1] == 'C' || name[length-1] == 'c')) tag = APP_ACCESSORY;
    application_label(name, label);
    if (cuis_call6(CUIS_SESSION_LABEL,handle,(uintptr_t)label,sizeof(label),0,0,0) != E_OK) return FALSE;
    if (handle == application_session) return TRUE;
    if (cuis_call6(CUIS_SESSION_TAG,handle,tag,0,0,0,0) != E_OK) return FALSE;
    application_accessory = tag == APP_ACCESSORY;
    return TRUE;
}

short mt_appl_init(AESGLOBAL *global)
{
    LONG pid, handle;
    if (!global) return -1;
    clear_global(global);
    pid = aes_process_call(0x10b); /* Pgetpid: an existing bounded, trusted ID namespace. */
    if (pid <= 0 || pid > 32767) { return -1; }
    handle = aes_session_open();
    if (handle < 0) { return -1; }
    if (!publish_label(handle)) {
        (void)aes_session_close(handle);
        aes_menu_reset(); aes_windows_reset(); aes_desktop_reset(); aes_graphics_reset();
        aes_resources_reset();
        application_session = -1; application_id = -1;
        return -1;
    }
    if (handle != application_session) { aes_lifecycle_reset(application_accessory); search_after = -1; aes_menu_reset(); aes_windows_reset(); aes_desktop_reset(); aes_graphics_reset(); aes_resources_reset(); }
    application_session = handle;
    application_id = (short)pid;
    global->ap_version = 0x0400; /* AES global-array ABI, not a claim of complete AES coverage. */
    global->ap_count = -1; /* GEM multitasking sentinel. */
    global->ap_id = application_id;
    return application_id;
}


short mt_appl_exit(AESGLOBAL *global)
{
    if (!registered(global) || aes_session_close(application_session) != E_OK) return 0;
    aes_menu_reset(); aes_windows_reset(); aes_desktop_reset(); aes_graphics_reset();
    aes_resources_reset();
    application_session = -1;
    application_id = -1;
    search_after = -1;
    aes_lifecycle_reset(FALSE);
    clear_global(global);
    return 1;
}


short mt_appl_yield(AESGLOBAL *global)
{
    return registered(global) && aes_process_call(0xff) == E_OK ? 1 : 0;
}


short mt_evnt_timer(unsigned long interval, AESGLOBAL *global)
{
    if (!registered(global) || interval > 0xffffffffUL) return 0;
    if (!aes_menu_registered() && !aes_frames_registered() &&
        !aes_controls_listening() && !aes_frames_wait_display() && !aes_lifecycle_listening())
        return cuis_call6(CUIS_SESSION_DELAY, (QUAD)application_session, interval, 0, 0, 0, 0) == E_OK ? 1 : 0;
    return mt_evnt_multi(MU_TIMER,0,0,0,0,0,0,0,0,0,0,0,0,0,NULL,interval,
                         NULL,NULL,NULL,NULL,NULL,NULL,global) == MU_TIMER;
}


short mt_appl_getinfo(short type, short *a, short *b, short *c, short *d, AESGLOBAL *global)
{
    /* Advertise only implemented extensions, using the standard AES query. */
    if (a) *a = 0;
    if (b) *b = 0;
    if (c) *c = 0;
    if (d) *d = 0;
    if (type == AES_PROCESS && registered(global)) {
        if (b) *b = 1; /* MiNT/AES ID conversion; scheduling remains cooperative. */
        if (c) *c = 1; /* Standard APP_FIRST/NEXT/DESK enumeration. */
        if (d) *d = 1; /* Standard caller-owned native rsrc_rcfix. */
        return 1;
    }
    if (type == AES_OBJECT && registered(global)) {
        if (a) *a = 1; /* Standard 3D object flags. */
        if (b) *b = 1; /* MultiTOS objc_sysvar, without extended selectors. */
        return 1;
    }
    if (type == AES_PCGEM && registered(global)) {
        if (d) *d = 1; /* Standard shel_rdef/shel_wdef. */
        return 1;
    }
    if (type == AES_MESSAGE && registered(global)) {
        if (a) *a = 0x28; /* AP_TERM and CH_EXIT; no live resolution backend. */
        return 1;
    }
    if (type == AES_INQUIRE && registered(global)) {
        if (a) *a = 1; /* APR_NOWAIT */
        if (b) *b = 1; /* SHEL_BUFSIZE and dynamic shell buffers. */
        return 1;
    }
    return 0;
}


short mt_appl_find(const char *name, AESGLOBAL *global)
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


short mt_appl_search(short mode, char *name, short *type, short *id, AESGLOBAL *global)
{
    LONG session = aes_application_session(global), peer, after, bytes, tag, pid, desktop;
    char label[CUIS_LABEL_MAX];
    if (session < 0 || !name || mode < APP_FIRST || mode > APP_DESK) return 0;
    if (mode == APP_FIRST) search_after = -1;
    after = mode == APP_FIRST ? -1 : search_after;
    for (;;) {
        peer = mode == APP_DESK ? cuis_call6(CUIS_PEER_QUERY,session,0,CUIS_PEER_BACKGROUND,0,0,0) :
            cuis_call6(CUIS_PEER_NEXT,session,(QUAD)after,0,0,0,0);
        if (peer < 0) return 0;
        after = peer;
        bytes = cuis_call6(CUIS_PEER_QUERY,session,peer,CUIS_PEER_LABEL,(uintptr_t)label,sizeof(label),0);
        tag = cuis_call6(CUIS_PEER_QUERY,session,peer,CUIS_PEER_TAG,0,0,0);
        pid = cuis_call6(CUIS_PEER_PID,peer,0,0,0,0,0);
        if (bytes == 8 && ((tag & 255) == APP_APPLICATION || (tag & 255) == APP_ACCESSORY) && pid > 0 && pid <= 32767) {
            desktop = cuis_call6(CUIS_PEER_QUERY,session,0,CUIS_PEER_BACKGROUND,0,0,0);
            memcpy(name,label,8); name[8] = 0;
            if (type) *type = (short)(tag & 255) | (peer == desktop ? APP_SHELL : 0);
            if (id) *id = (short)pid;
            if (mode != APP_DESK) search_after = after;
            return 1;
        }
        if (mode == APP_DESK) return 0;
        if (bytes < 0 && bytes != EFILNF) return 0;
        if ((tag < 0 && tag != EFILNF) || (pid < 0 && pid != EFILNF)) return 0;
        search_after = after;
    }
}


short mt_appl_control(short id, short mode, void *output, AESGLOBAL *global)
{
    LONG session = aes_application_session(global), peer, state;
    if (session < 0 || (mode != APC_TOP && mode != APC_INFO) || (mode == APC_INFO && !output)) return 0;
    if (id == -1 && mode == APC_INFO)
        peer = cuis_call6(CUIS_PEER_QUERY,session,0,CUIS_PEER_ACTIVE,0,0,0);
    else if (id > 0) peer = cuis_call6(CUIS_PEER_BY_PID,id,0,0,0,0,0);
    else return 0;
    if (peer < 0) return 0;
    if (mode == APC_TOP) return cuis_call6(CUIS_PEER_RAISE,session,peer,0,0,0,0) == E_OK;
    state = cuis_call6(CUIS_PEER_QUERY,session,peer,CUIS_PEER_STATE,0,0,0);
    if (state < 0) return 0;
    *(short *)output = (state & CUIS_PEER_HAS_SURFACE ? APCI_HASMBAR : 0) |
                      (state & CUIS_PEER_HAS_BACKGROUND ? APCI_HASDESK : 0);
    return 1;
}


short mt_menu_register(short id, const char *text, AESGLOBAL *global)
{
    ULONG length, slot;
    LONG result;
    char label[8];
    if (!registered(global) || !text) return -1;
    if (id == REG_NEWNAME) {
        application_label(text,label);
        return cuis_call6(CUIS_SESSION_LABEL,application_session,(uintptr_t)label,8,0,0,0) == E_OK ? 0 : -1;
    }
    /* An application may register several entries, but may never publish a
     * menu item as another application's identity. Native AES also records
     * the calling process as the recipient. Six is the classic Desk limit. */
    if (id != application_id) return -1;
    for (length = 0; length < CUIS_ENTRY_BYTES && text[length]; length++) ;
    if (!length || length == CUIS_ENTRY_BYTES) return -1;
    for (slot = 0; slot < 6; slot++) {
        result = cuis_call6(CUIS_ENTRY_PUBLISH,application_session,slot,0,(uintptr_t)text,length+1,0);
        if (result >= 0) return (short)result;
        if (result != EAGAIN) return -1;
    }
    return -1;
}


short mt_appl_write(short id, short length, void *buffer, AESGLOBAL *global)
{
    LONG peer;
    if (!registered(global) || id <= 0 || length < 0 || (length && !buffer)) return 0;
    peer = cuis_call6(CUIS_PEER_BY_PID, id, 0, 0, 0, 0, 0);
    if (peer < 0) return 0;
    return cuis_call6(CUIS_STREAM_WRITE, peer, (uintptr_t)buffer, length, 1, 0, 0) == length ? 1 : 0;
}


short mt_appl_read(short id, short length, void *buffer, AESGLOBAL *global)
{
    if (!registered(global) || (id != APR_NOWAIT && id != application_id) ||
        length < 0 || (length && !buffer)) return 0;
    if (!length) return 1;
    for (;;) {
        LONG result;
        if (aes_lifecycle_messages(global) < 0) return 0;
        if (aes_child_messages(application_session) < 0) return 0;
        if ((id == APR_NOWAIT ? aes_menu_flush(global) : aes_menu_dispatch(global)) < 0) return 0;
        if (!aes_lifecycle_wait_display() && (id == APR_NOWAIT ? aes_window_messages_poll(application_session,global) :
             aes_window_messages(application_session,global)) < 0) return 0;
        result = aes_lifecycle_read(buffer,length,global,id == APR_NOWAIT);
        if (result == length) return 1;
        if (!result) continue;
        if (result != EAGAIN || id == APR_NOWAIT) return 0;
        {
            BOOL repeat = aes_controls_repeating();
            if (cuis_call6(CUIS_SESSION_WAIT, application_session, length,
                           (aes_lifecycle_wait_display() ? CUIS_READY_DISPLAY : CUIS_READY_STREAM | CUIS_READY_CHILD) | (aes_frames_suspended() ? 0 : aes_frames_wait_display() ? CUIS_READY_DISPLAY : CUIS_READY_NOTICE) |
                           (aes_menu_listening() ? CUIS_READY_SURFACE : 0) | (aes_controls_listening() ? CUIS_READY_CONTROL : 0) |
                           (repeat ? CUIS_READY_SERVICE : 0) |
                           (aes_lifecycle_listening() && !aes_lifecycle_wait_display() && !aes_frames_wait_display() ? CUIS_READY_COHORT : 0), 0, 0, 0) <= 0) return 0;
        }
    }
}

short mt_evnt_mesag(short message[8], AESGLOBAL *global)
{
    return global ? mt_appl_read(global->ap_id, 16, message, global) : 0;
}
