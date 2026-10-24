/* Classic EmuDesk calls adapted locally to typed AES services.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "obdefs.h"
#include "aesdefs.h"
#include "aesbind.h"
#include "aes_shell.h"
#include "deskaes.h"
#include "gemrslib.h"
#include "gemobed.h"
#include "gemfmlib.h"
#include "gemfmalt.h"
#include "gemgrlib.h"
#include "gemerror.h"
#include "string.h"
#include "bdosbind.h"
#include "aesext.h"
#include "gsxdefs.h"
#include "intmath.h"
#if CONF_WITH_X32_DESKTOP_TEST
#include "desk_rsc.h"
#endif

static AESGLOBAL resource;
static BOOL registered;
#if CONF_WITH_X32_DESKTOP_TEST
static BOOL test_ready;
#endif
static LONG shell(AES_SHELL_REQUEST *request)
{
    LONG rc;

    do {
        rc = aes_shell(request);
        if (rc == EAGAIN && aes_appl_yield() < 0)
            return EACCDN;
    } while (rc == EAGAIN);
    return rc;
}

static WORD parent(OBJECT *tree, WORD object)
{
    WORD p = tree[object].ob_next, count = 0;

    while (p >= 0 && count++ < AES_TREE_MAX_OBJECTS) {
        if (tree[p].ob_tail == object)
            return p;
        object = p;
        p = tree[p].ob_next;
    }
    return -1;
}

#if CONF_WITH_X32_DESKTOP_TEST
static void menu_target(OBJECT *tree)
{
    WORD object, at, x[2] = { 0, 0 }, y[2] = { 0, 0 };
    WORD ids[2] = { READITEM, OPTNMENU };
    char message[100];

    for (object = 0; object < 2; object++) {
        for (at = ids[object]; at >= 0; at = parent(tree, at)) {
            x[object] += tree[at].ob_x;
            y[object] += tree[at].ob_y;
        }
        x[object] += tree[ids[object]].ob_width / 2;
        y[object] += tree[ids[object]].ob_height / 2;
    }
    sprintf(message, "x32 desktop readinf TARGET %d %d %d %d\r\n", x[0], y[0], x[1], y[1]);
    desk_test_record(message);
}
#endif

static BOOL unlink_object(OBJECT *tree, WORD object)
{
    WORD p = parent(tree, object), child;

    if (p < 0)
        return FALSE;
    child = tree[p].ob_head;
    if (child == object)
        tree[p].ob_head = tree[object].ob_next == p ? -1 : tree[object].ob_next;
    else {
        while (child != p && tree[child].ob_next != object)
            child = tree[child].ob_next;
        if (child == p)
            return FALSE;
        tree[child].ob_next = tree[object].ob_next;
    }
    if (tree[p].ob_tail == object)
        tree[p].ob_tail = tree[p].ob_head == -1 ? -1 : child;
    tree[object].ob_next = -1;
    return TRUE;
}

static void append(OBJECT *tree, WORD p, WORD child)
{
    if (tree[p].ob_tail < 0)
        tree[p].ob_head = child;
    else
        tree[tree[p].ob_tail].ob_next = child;
    tree[p].ob_tail = child;
    tree[child].ob_next = p;
}

static WORD reorder(OBJECT *tree, WORD object, WORD position)
{
    WORD p = parent(tree, object), child, previous = -1;

    if (p < 0 || !unlink_object(tree, object))
        return FALSE;
    if (position < 0) {
        append(tree, p, object);
        return TRUE;
    }
    child = tree[p].ob_head;
    while (child >= 0 && child != p && position-- > 0) {
        previous = child;
        child = tree[child].ob_next;
    }
    if (child < 0 || child == p)
        append(tree, p, object);
    else {
        tree[object].ob_next = child;
        if (previous < 0)
            tree[p].ob_head = object;
        else
            tree[previous].ob_next = object;
    }
    return TRUE;
}

static void outline(WORD x, WORD y, WORD w, WORD h)
{
    Point points[5];

    points[0].x = points[3].x = points[4].x = x;
    points[1].x = points[2].x = x + w;
    points[0].y = points[1].y = points[4].y = y;
    points[2].y = points[3].y = y + h;
    gsx_sclip(&gl_rscreen);
    gsx_attr(FALSE, MD_XOR, BLACK);
    gsx_pline(0, 0, 5, points);
}

static WORD rubber(const UWORD *in, UWORD *out)
{
    AES_EVENT_REQUEST request = { 0 };
    AES_INPUT_EVENT event;
    LONG result;
    WORD x, y, buttons, modifiers, width, height, nw, nh;
    GRECT arguments;
#if CONF_WITH_X32_DESKTOP_TEST
    char message[100];
#endif

    /* Nested classic bindings reuse gembind's static input array. */
    memcpy(&arguments, in, sizeof(arguments));
    graf_mkstate(&x, &y, &buttons, &modifiers);
    width = max((WORD)(x - arguments.g_x), arguments.g_w);
    height = max((WORD)(y - arguments.g_y), arguments.g_h);
#if CONF_WITH_X32_DESKTOP_TEST
    sprintf(message, "x32 desktop rubber READY %d %d %d\r\n",
            arguments.g_x, arguments.g_y, buttons);
    desk_test_record(message);
#endif
    request.mask = AES_EVENT_BUTTON | AES_EVENT_TIMER;
    request.clicks = 1; request.button_mask = 1; request.button_state = 0;
    request.timer_ms = 20;
    while (buttons & 1) {
        graf_mouse(M_OFF, NULL);
        outline(arguments.g_x, arguments.g_y, width, height);
        graf_mouse(M_ON, NULL);
        result = aes_evnt_multi(&request, &event);
        graf_mouse(M_OFF, NULL);
        outline(arguments.g_x, arguments.g_y, width, height);
        graf_mouse(M_ON, NULL);
        if (result < 0)
            break;
        nw = max((WORD)(event.mouse_x - arguments.g_x), arguments.g_w);
        nh = max((WORD)(event.mouse_y - arguments.g_y), arguments.g_h);
#if CONF_WITH_X32_DESKTOP_TEST
        if (nw != width || nh != height) {
            sprintf(message, "x32 desktop rubber TRACK %d %d %d\r\n", nw, nh, event.buttons);
            desk_test_record(message);
        }
#endif
        width = nw; height = nh; buttons = event.buttons;
    }
    out[1] = width; out[2] = height;
#if CONF_WITH_X32_DESKTOP_TEST
    sprintf(message, "x32 desktop rubber RESULT %d %d %d %d\r\n",
            arguments.g_x, arguments.g_y, width, height);
    desk_test_record(message);
#endif
    return TRUE;
}

static WORD animate(const UWORD *in, BOOL shrink)
{
    GRECT old, small, large;

    memcpy(&small, in, sizeof(small));
    memcpy(&large, in + 4, sizeof(large));
    gsx_gclip(&old);
    if (shrink)
        gr_shrinkbox(&small, &large);
    else
        gr_growbox(&small, &large);
    gsx_sclip(&old);
    return TRUE;
}

WORD desk_aes_dispatch(UWORD opcode, UWORD *global, UWORD *in, UWORD *out,
                       intptr_t *addresses, intptr_t *results)
{
    AES_APP_INFO app;
    AES_EVENT_REQUEST event_request = { 0 };
    AES_INPUT_EVENT event;
    AES_OBJECT_REQUEST object_request = { 0 };
    AES_OBJECT_RESULT object_result = { 0 };
    AES_SHELL_REQUEST sr = { 0 };
    OBJECT *tree = (OBJECT *)addresses[0];
    GRECT rectangle;
    DESK_TREE snapshot;
    LONG rc = EINVFN;
    void *pointer;

    desk_tree_checkpoint();
    (void)results;
    memset(out, 0, 7 * sizeof(*out));
    switch (opcode) {
    case APPL_INIT:
        rc = aes_appl_init("EMUDESK", &app);
        if (!rc) {
            registered = TRUE;
#if CONF_WITH_X32_DESKTOP_TEST
            test_ready = FALSE;
#endif
            memset(global, 0, 32 * sizeof(*global));
            ((AESGLOBAL *)global)->ap_version = AES_VERSION;
            ((AESGLOBAL *)global)->ap_count = 1;
            ((AESGLOBAL *)global)->ap_id = app.id;
            out[0] = app.id;
            return out[0];
        }
        break;
    case APPL_EXIT:
        desk_trees_exit();
        sr.operation = AES_SHELL_RESET;
        rc = shell(&sr);
        if (rc < 0)
            break;
        rc = aes_appl_exit();
        registered = FALSE;
        break;
    case EVNT_MULTI:
#if CONF_WITH_X32_DESKTOP_TEST
        {
            BOOL main_wait = (in[0] & (MU_KEYBD | MU_BUTTON | MU_MESAG)) == (MU_KEYBD | MU_BUTTON | MU_MESAG);

            if (main_wait) {
                desk_tree_test();
                desk_window_test();
            }
            if (!test_ready || main_wait) {
                AES_OBJECT_REQUEST request = { 0 };
                AES_OBJECT_RESULT result;
                char message[100];

                request.operation = AES_OBJECT_MOUSE_STATE;
                if (!desk_object_call(&request, &result)) {
                    if (main_wait) {
                        sprintf(message, "x32 desktop IDLE %d %d\r\n", result.rectangle.g_x, result.rectangle.g_y);
                        desk_test_record(message);
                    }
                    if (!test_ready) {
                        sprintf(message, "x32 desktop READY %d %d %d %d\r\n",
                                result.rectangle.g_x, result.rectangle.g_y, gl_width, gl_height);
                        desk_test_record(message);
                    }
                }
                test_ready = TRUE;
            }
        }
#endif
        event_request.mask = in[0];
        event_request.clicks = in[1]; event_request.button_mask = in[2] & 3;
        event_request.button_state = in[3] & 3;
        memcpy(&event_request.mouse1, in + 4, sizeof(AES_EVENT_RECT));
        memcpy(&event_request.mouse2, in + 9, sizeof(AES_EVENT_RECT));
        event_request.timer_ms = (ULONG)in[14] | ((ULONG)in[15] << 16);
        goto wait_event;
    case EVNT_BUTTON:
        event_request.mask = AES_EVENT_BUTTON;
        event_request.clicks = in[0]; event_request.button_mask = in[1] & 3;
        event_request.button_state = in[2] & 3;
        goto wait_event;
    case EVNT_TIMER:
        event_request.mask = AES_EVENT_TIMER;
        event_request.timer_ms = (ULONG)in[0] | ((ULONG)in[1] << 16);
wait_event:
        rc = aes_evnt_multi(&event_request, &event);
        if (rc >= 0) {
            out[1] = event.mouse_x; out[2] = event.mouse_y;
            out[3] = event.buttons; out[4] = event.modifiers;
            out[5] = event.key; out[6] = event.clicks;
            if (opcode == EVNT_MULTI && (event.events & AES_EVENT_MESSAGE))
                memcpy((void *)addresses[0], &event.message, sizeof(event.message));
            out[0] = opcode == EVNT_BUTTON ? event.clicks : event.events;
            return out[0];
        }
        break;
    case EVNT_DCLICK:
        sr.operation = AES_SHELL_DCLICK; sr.value = in[0]; sr.extra = in[1];
        out[0] = shell(&sr); return out[0];
    case MENU_BAR:
        out[0] = desk_tree_retain(tree, 0, TRUE, in[0]);
#if CONF_WITH_X32_DESKTOP_TEST
        if (out[0] && in[0])
            menu_target(tree);
#endif
        return out[0];
    case MENU_ICHECK: case MENU_IENABLE: case MENU_TNORMAL:
        if (opcode == MENU_ICHECK)
            tree[in[0]].ob_state = in[1] ? tree[in[0]].ob_state | CHECKED : tree[in[0]].ob_state & ~CHECKED;
        else if (opcode == MENU_IENABLE)
            tree[in[0]].ob_state = in[1] ? tree[in[0]].ob_state & ~DISABLED : tree[in[0]].ob_state | DISABLED;
        else
            tree[in[0]].ob_state = in[1] ? tree[in[0]].ob_state & ~SELECTED : tree[in[0]].ob_state | SELECTED;
        out[0] = desk_tree_retain(tree, 0, TRUE, TRUE); return out[0];
    case OBJC_ADD:
        append(tree, in[0], in[1]); rc = E_OK; break;
    case OBJC_ORDER:
        out[0] = reorder(tree, in[0], (WORD)in[1]); return out[0];
    case OBJC_DRAW:
        memcpy(&rectangle, in + 2, sizeof(rectangle));
        rc = desk_tree_operation(tree, in[0], AES_OBJECT_DRAW_TREE, in[1], 0, &rectangle, &object_result);
        break;
    case OBJC_FIND:
        rectangle.g_x = in[2]; rectangle.g_y = in[3]; rectangle.g_w = rectangle.g_h = 0;
        rc = desk_tree_operation(tree, in[0], AES_OBJECT_FIND, in[1], 0, &rectangle, &object_result);
        out[0] = rc < 0 ? -1 : object_result.value; return out[0];
    case OBJC_OFFSET:
        rc = desk_tree_operation(tree, in[0], AES_OBJECT_OFFSET, 0, 0, NULL, &object_result);
        out[1] = object_result.rectangle.g_x; out[2] = object_result.rectangle.g_y;
        break;
    case FORM_CENTER:
        ob_center(tree, (GRECT *)(out + 1)); rc = E_OK; break;
    case FORM_DO:
        if (!desk_tree_create(&snapshot, tree))
            break;
        desk_test_record("x32 desktop form READY\r\n");
        rc = aes_form_do(snapshot.handle, in[0]);
        if (rc >= 0)
            desk_test_record("x32 desktop form PASS\r\n");
        desk_tree_read(&snapshot);
        desk_tree_destroy(&snapshot);
        out[0] = rc < 0 ? 0 : rc; return out[0];
    case FORM_DIAL:
        if (in[0] == FMD_FINISH) {
            sr.operation = AES_SHELL_DAMAGE;
            memcpy(&sr.rectangle, in + 5, sizeof(GRECT));
            rc = shell(&sr);
        } else
            rc = E_OK;
        break;
    case FORM_ALERT:
        out[0] = fm_alert(in[0], (char *)addresses[0]); return out[0];
    case FORM_ERROR:
        out[0] = fm_error(in[0]); return out[0];
    case GRAF_RUBBOX:
        out[0] = rubber(in, out); return out[0];
    case GRAF_GROWBOX: case GRAF_SHRINKBOX:
        out[0] = animate(in, opcode == GRAF_SHRINKBOX); return out[0];
    case GRAF_HANDLE:
        object_request.operation = AES_OBJECT_METRICS;
        rc = desk_object_call(&object_request, &object_result);
        memcpy(out + 1, &object_result.rectangle, sizeof(GRECT));
        out[0] = rc < 0 ? 0 : object_result.value; return out[0];
    case GRAF_MOUSE:
        object_request.operation = AES_OBJECT_MOUSE; object_request.value = in[0];
        rc = desk_object_call(&object_request, &object_result); break;
    case GRAF_MKSTATE:
        object_request.operation = AES_OBJECT_MOUSE_STATE;
        rc = desk_object_call(&object_request, &object_result);
        out[1] = object_result.rectangle.g_x; out[2] = object_result.rectangle.g_y;
        out[3] = object_result.value; out[4] = object_result.key; break;
    case FSEL_EXINPUT: {
        WORD button = 0;

        out[0] = desk_file_select((char *)addresses[0], (char *)addresses[1], &button, (const char *)addresses[2]);
        out[1] = button;
        return out[0];
    }
    case WIND_SET:
        if (in[1] == WF_NEWDESK) {
            memcpy(&pointer, in + 2, sizeof(pointer));
            out[0] = desk_tree_retain(pointer, in[4], FALSE, pointer != NULL);
            return out[0];
        }
        FALLTHROUGH;
    case WIND_CREATE: case WIND_OPEN: case WIND_CLOSE: case WIND_DELETE:
    case WIND_GET: case WIND_FIND: case WIND_UPDATE: case WIND_CALC:
        rc = desk_window(opcode, in, out);
        if (opcode == WIND_CREATE || opcode == WIND_FIND) {
            out[0] = opcode == WIND_CREATE && rc < 0 ? -1 : rc;
            return out[0];
        }
        if (opcode == WIND_OPEN && !rc)
            desk_test_record("x32 desktop window OPEN\r\n");
        break;
    case WIND_NEW:
        sr.operation = AES_SHELL_RESET;
        rc = registered ? shell(&sr) : E_OK;
        break;
    case RSRC_LOAD:
        rc = rs_load(&resource, (char *)addresses[0]) ? E_OK : EFILNF;
        if (!rc)
            ((AESGLOBAL *)global)->ap_rscmem = resource.ap_rscmem;
        break;
    case RSRC_FREE:
        if (resource.ap_rscmem) {
            rs_free(&resource);
            resource.ap_rscmem = NULL;
            ((AESGLOBAL *)global)->ap_rscmem = NULL;
        }
        rc = E_OK; break;
    case RSRC_OBFIX:
        rs_obfix(tree, in[0]); rc = E_OK; break;
    case SHEL_WRITE:
        sr.operation = AES_SHELL_WRITE; sr.value = in[0]; sr.extra = in[1]; sr.bytes = in[2];
        sr.data = addresses[0]; sr.second = addresses[1]; rc = shell(&sr);
        out[0] = rc > 0; return out[0];
    case SHEL_GET: case SHEL_PUT:
        sr.operation = opcode == SHEL_GET ? AES_SHELL_GET : AES_SHELL_PUT;
        sr.bytes = in[0]; sr.data = addresses[0]; rc = shell(&sr); break;
    case SHEL_WDEF:
        sr.operation = AES_SHELL_WDEF; sr.data = addresses[0]; sr.second = addresses[1];
        rc = shell(&sr); break;
    }
    out[0] = rc >= 0;
    return out[0];
}
