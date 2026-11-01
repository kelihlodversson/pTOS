/* Classic desktop window calls over the typed AES interface.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "aesdefs.h"
#include "deskaes.h"
#include "gemerror.h"
#include "string.h"

#if CONF_WITH_X32_DESKTOP_TEST && !defined(DESK_WINDOW_HOST_TEST)
static ULONG getter_retries;
#endif

static LONG retry(void)
{
    LONG rc;

#if CONF_WITH_X32_DESKTOP_TEST && !defined(DESK_WINDOW_HOST_TEST)
    getter_retries++;
#endif
    rc = aes_appl_yield();
    return rc < 0 ? rc : EAGAIN;
}

static LONG information(WORD handle, AES_WINDOW_INFO *info)
{
    LONG rc;

    do {
        rc = aes_wind_get(handle, info);
        if (rc == EAGAIN && (rc = retry()) != EAGAIN)
            return rc;
    } while (rc == EAGAIN);
    return rc;
}

static LONG get(WORD handle, WORD field, UWORD *out)
{
    AES_WINDOW_INFO info;
    GRECT rectangle;
    LONG rc;

    if (field == WF_FIRSTXYWH || field == WF_NEXTXYWH) {
        rc = aes_wind_rect(handle, field == WF_NEXTXYWH, &rectangle);
        if (!rc)
            memcpy(out + 1, &rectangle, sizeof(rectangle));
        return rc;
    }
    rc = aes_wind_get(handle, &info);
    if (rc < 0)
        return rc;
    switch (field) {
    case WF_KIND: out[1] = info.kind; break;
    case WF_WXYWH: memcpy(out + 1, &info.work, sizeof(GRECT)); break;
    case WF_CXYWH: memcpy(out + 1, &info.current, sizeof(GRECT)); break;
    case WF_PXYWH: memcpy(out + 1, &info.previous, sizeof(GRECT)); break;
    case WF_FXYWH: memcpy(out + 1, &info.full, sizeof(GRECT)); break;
    case WF_TOP: out[1] = info.top; break;
    case WF_HSLIDE: out[1] = info.hslide; break;
    case WF_VSLIDE: out[1] = info.vslide; break;
    case WF_HSLSIZ: out[1] = info.hslsiz; break;
    case WF_VSLSIZ: out[1] = info.vslsiz; break;
    default: return EINVFN;
    }
    return E_OK;
}

/* gembind's arrays can be reused by callbacks while a call yields. Copy
 * arguments once, and retry only contention; all other errors pass through. */
LONG desk_window(UWORD opcode, const UWORD *input, UWORD *out)
{
    UWORD in[8];
    GRECT rectangle;
    AES_WINDOW_INFO info;
    void *text;
    LONG rc;
    BOOL retried = FALSE, opening = opcode == WIND_OPEN;

    memcpy(in, input, sizeof(in));
    if (opcode == WIND_OPEN || opcode == WIND_CLOSE) {
        rc = information(in[0], &info);
        if (rc < 0)
            return rc;
        if (!!(info.flags & AES_WINDOW_OPEN) == opening)
            return EINVAL;       /* preserve invalid initial-state errors */
    }
    for (;;) {
        if (retried && (opcode == WIND_OPEN || opcode == WIND_CLOSE)) {
            rc = information(in[0], &info);
            if (rc < 0)
                return rc;
            /* END_UPDATE can yield after native open/close committed. */
            if (!!(info.flags & AES_WINDOW_OPEN) == opening)
                return E_OK;
        }
        switch (opcode) {
        case WIND_CREATE:
            memcpy(&rectangle, in + 1, sizeof(rectangle));
            rc = aes_wind_create(in[0], &rectangle); break;
        case WIND_OPEN:
            memcpy(&rectangle, in + 1, sizeof(rectangle));
            rc = aes_wind_open(in[0], &rectangle); break;
        case WIND_CLOSE: rc = aes_wind_close(in[0]); break;
        /* DELETE's EAGAIN retains the handle, possibly closed. Once the
         * handle is deleted the service never reports contention. */
        case WIND_DELETE: rc = aes_wind_delete(in[0]); break;
        case WIND_GET: rc = get(in[0], in[1], out); break;
        case WIND_FIND: rc = aes_wind_find(in[0], in[1]); break;
        case WIND_UPDATE: rc = aes_wind_update(in[0]); break;
        case WIND_CALC:
            memcpy(&rectangle, in + 2, sizeof(rectangle));
            rc = aes_wind_calc(in[0], in[1], &rectangle, (GRECT *)(out + 1)); break;
        case WIND_SET:
            memcpy(&rectangle, in + 2, sizeof(rectangle));
            memcpy(&text, in + 2, sizeof(text));
            switch (in[1]) {
            case WF_CXYWH: rc = aes_wind_set_rect(in[0], &rectangle); break;
            case WF_TOP: rc = aes_wind_top(in[0]); break;
            case WF_NAME: case WF_INFO:
                rc = aes_wind_set_text(in[0], in[1] == WF_INFO, text); break;
            case WF_HSLIDE: case WF_VSLIDE:
                rc = aes_wind_slider(in[0], in[1] == WF_VSLIDE, in[2]); break;
            case WF_HSLSIZ: case WF_VSLSIZ:
                rc = aes_wind_slider(in[0], in[1] == WF_VSLSIZ ? AES_WINDOW_VSLSIZ : AES_WINDOW_HSLSIZ, in[2]); break;
            default: return EINVFN;
            }
            break;
        default: return EINVFN;
        }
        if (rc != EAGAIN || (rc = retry()) != EAGAIN)
            return rc;
        retried = TRUE;
    }
}

#if CONF_WITH_X32_DESKTOP_TEST && !defined(DESK_WINDOW_HOST_TEST)
void desk_window_test(void)
{
    static BOOL done;
    AES_CLIENT_MESSAGE message = { 652, 0, 0, { 0 } };
    AES_EVENT event;
    UWORD out[7];
    WORD mode;
    ULONG before;
    LONG peer;
    BOOL ok = TRUE;

    if (done)
        return;
    done = TRUE;
    peer = aes_appl_find("DESKACC");
    if (peer < 0)
        ok = FALSE;
    for (mode = 0; ok && mode < 2; mode++) {
        message.data[0] = mode ? BEG_MCTRL : BEG_UPDATE;
        if (aes_appl_write(peer, &message) != E_OK) {
            ok = FALSE;
            break;
        }
        do {
            if (aes_evnt_wait(AES_EVENT_MESSAGE, &event) != AES_EVENT_MESSAGE) {
                ok = FALSE;
                break;
            }
        } while (event.message.type != 653);
        before = getter_retries;
        if (ok)
            ok = desk_window(WIND_GET, (UWORD[8]){ 0, mode ? WF_FIRSTXYWH : WF_WXYWH }, out) == E_OK &&
                 out[3] > 0 && out[4] > 0 && getter_retries > before;
    }
    if (ok) {
        AES_WINDOW_INFO screen = { 0 };
        GRECT rectangle;
        UWORD request[8] = { 0 };
        LONG handle;

        ok = information(0, &screen) == E_OK;
        rectangle = screen.full;
        rectangle.g_x += rectangle.g_w + 20;
        rectangle.g_y += rectangle.g_h + 20;
        rectangle.g_w = 160; rectangle.g_h = 120;
        request[0] = AES_WINDOW_NAME_GADGET;
        memcpy(request + 1, &rectangle, sizeof(rectangle));
        handle = ok ? desk_window(WIND_CREATE, request, out) : EACCDN;
        if (handle > 0) {
            request[0] = handle;
            ok = desk_window(WIND_OPEN, request, out) == E_OK;
            message.data[0] = BEG_UPDATE;
            ok = aes_appl_write(peer, &message) == E_OK && ok;
            do {
                if (aes_evnt_wait(AES_EVENT_MESSAGE, &event) != AES_EVENT_MESSAGE) {
                    ok = FALSE;
                    break;
                }
            } while (event.message.type != 653);
            before = getter_retries;
            request[1] = WF_TOP;
            ok = desk_window(WIND_SET, request, out) == E_OK &&
                 getter_retries > before && ok;
            ok = information(handle, &screen) == E_OK && screen.top == handle && ok;
            ok = desk_window(WIND_CLOSE, request, out) == E_OK && ok;
            ok = desk_window(WIND_DELETE, request, out) == E_OK && ok;
        } else
            ok = FALSE;
        desk_test_record(ok ? "x32 desktop window top retries PASS\r\n" :
                              "x32 desktop window top retries FAIL\r\n");
    }
    desk_test_record(ok ? "x32 desktop window getter retries PASS\r\n" :
                          "x32 desktop window getter retries FAIL\r\n");
}
#endif
