/* Transport for the shared GEM file selector in the x32 desktop.
 * Dialog resources and interaction remain in aes/gemfslib.c.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "struct.h"
#include "obdefs.h"
#include "aesdefs.h"
#include "aesext.h"
#include "aesbind.h"
#include "gemlib.h"
#include "gem_rsc.h"
#include "gemfslib.h"
#include "gemfmlib.h"
#include "gemfmalt.h"
#include "gemoblib.h"
#include "gemgsxif.h"
#include "gemevlib.h"
#include "gemasm.h"
#include "gemwmlib.h"
#include "geminit.h"
#include "gsxdefs.h"
#include "biosbind.h"
#include "string.h"
#include "optimize.h"
#include "deskaes.h"

LONG drvbits;
WORD xrat, yrat;
GRECT gl_rzero, gl_rcenter;
static BOOL initialized, selector_active;
static DESK_TREE owned;
static BOOL own_update, own_mouse, own_modal, graphics_failed;

static void resources(void)
{
    if (initialized)
        return;
    gem_rsc_init();
    gem_rsc_fixit();
    fs_start();
    r_set(&gl_rcenter, (gl_width-gl_wbox)/2,
          (gl_height-2*gl_hbox)/2, gl_wbox, gl_hbox);
    initialized = TRUE;
}

WORD desk_file_select(char *path, char *selection, WORD *button, const char *title)
{
    WORD result;

    resources();
    drvbits = Drvmap();
    selector_active = TRUE;
    result = fs_input(path, selection, button, (char *)title);
    selector_active = FALSE;
#if CONF_WITH_X32_DESKTOP_TEST
    if (result && *button) {
        char message[200], directory[128];
        char *mask;

        strlcpy(directory, path, sizeof(directory));
        mask = directory + strlen(directory);
        while (mask > directory && *mask != '\\')
            mask--;
        if (*mask == '\\')
            mask[1] = 0;
        sprintf(message, "x32 desktop selector ACCEPT %s%s\r\n", directory, selection);
        desk_test_record(message);
    }
#endif
    return result;
}

void ob_draw(OBJECT *tree, WORD object, WORD depth)
{
    GRECT clip;

    gsx_gclip(&clip);
    objc_draw(tree, object, depth, clip.g_x, clip.g_y, clip.g_w, clip.g_h);
}

void ob_change(OBJECT *tree, WORD object, UWORD state, WORD redraw)
{
    tree[object].ob_state = state;
    if (redraw)
        ob_draw(tree, object, MAX_DEPTH);
}

void ob_add(OBJECT *tree, WORD parent, WORD child)
{
    objc_add(tree, parent, child);
}

WORD fm_do(OBJECT *tree, WORD start)
{
#if CONF_WITH_X32_DESKTOP_TEST
    if (tree == rs_trees[FSELECTR]) {
        char message[200], name[14];
        WORD i, x, y;
        static const WORD controls[] = {
            FCLSBOX, FUPAROW, FDNAROW, FSVSLID, FSVELEV,
            FSOK, FSCANCEL, FSDIRECT, FSSELECT, FS1STDRV, FS1STDRV+2, FS1STDRV+3
        };

        ob_offset(tree, FSCANCEL, &x, &y);
        sprintf(message, "x32 desktop selector READY %d %d\r\n",
                x + tree[FSCANCEL].ob_width / 2, y + tree[FSCANCEL].ob_height / 2);
        desk_test_record(message);
        sprintf(message, "x32 desktop selector PATH %s\r\n",
                tree[FSDIRECT].ob_spec.tedinfo->te_ptext);
        desk_test_record(message);
        unfmt_str(tree[FSSELECT].ob_spec.tedinfo->te_ptext, name);
        sprintf(message, "x32 desktop selector NAME %s\r\n", name);
        desk_test_record(message);
        for (i = 0; i < ARRAY_SIZE(controls); i++) {
            WORD object = controls[i];

            ob_offset(tree, object, &x, &y);
            sprintf(message, "x32 desktop selector CONTROL %d %d %d\r\n", object,
                    x + tree[object].ob_width / 2, y + tree[object].ob_height / 2);
            desk_test_record(message);
        }
        for (i = F1NAME; i <= F9NAME; i++) {
            const char *text = tree[i].ob_spec.tedinfo->te_ptext;

            if (!text[1])
                continue;
            unfmt_str(text + 1, name);
            ob_offset(tree, i, &x, &y);
            sprintf(message, "x32 desktop selector ROW %s %d %d\r\n", name,
                    x + tree[i].ob_width / 2, y + tree[i].ob_height / 2);
            desk_test_record(message);
        }
        desk_test_record("x32 desktop selector WAIT\r\n");
    }
#endif
    {
        WORD answer = form_do(tree, start);

        if (!answer && tree == rs_trees[FSELECTR]) {
            tree[FSCANCEL].ob_state |= SELECTED;
            return FSCANCEL;
        }
        return answer;
    }
}

WORD fm_dial(WORD kind, GRECT *small, GRECT *large)
{
    return form_dial(kind, small->g_x, small->g_y, small->g_w, small->g_h,
                     large->g_x, large->g_y, large->g_w, large->g_h);
}

static LONG own_operation(UWORD operation, WORD value)
{
    AES_OBJECT_REQUEST request = { 0 };
    AES_OBJECT_RESULT result;

    request.tree = owned.handle;
    request.operation = operation;
    request.value = value;
    return desk_object_call(&request, &result);
}

void fm_own(WORD begin)
{
    if (begin) {
        graphics_failed = TRUE;
        if (!desk_tree_create(&owned, rs_trees[FSELECTR]))
            return;
        own_update = own_operation(AES_OBJECT_UPDATE, BEG_UPDATE) >= 0;
        if (own_update)
            own_mouse = own_operation(AES_OBJECT_UPDATE, BEG_MCTRL) >= 0;
        if (own_mouse)
            own_modal = own_operation(AES_OBJECT_FORM, 1) >= 0;
        if (!own_modal) {
            fm_own(FALSE);
            return;
        }
        graphics_failed = FALSE;
        desk_test_record("x32 desktop selector scroll READY\r\n");
    } else if (owned.handle) {
        if (own_modal)
            own_operation(AES_OBJECT_FORM, 0);
        if (own_mouse)
            own_operation(AES_OBJECT_UPDATE, END_MCTRL);
        if (own_update)
            own_operation(AES_OBJECT_UPDATE, END_UPDATE);
        own_modal = own_mouse = own_update = FALSE;
        desk_tree_destroy(&owned);
    }
}

WORD fm_alert(WORD button, char *text)
{
    GRECT rectangle, clip;
    OBJECT *tree;
    WORD answer;

    resources();
    set_mouse_to_arrow();
    gsx_gclip(&clip);
    tree = fm_alert_tree(button, text, &rectangle);
    fm_dial(FMD_START, &gl_rzero, &rectangle);
    gsx_sclip(&rectangle);
    ob_draw(tree, ROOT, MAX_DEPTH);
    desk_test_record("x32 desktop alert READY\r\n");
    answer = fm_do(tree, 0) & 0x7fff;
    fm_dial(FMD_FINISH, &gl_rzero, &rectangle);
    gsx_sclip(&clip);
    /* A selector error must restore the current listing, including the
     * partially edited path, before its modal loop resumes. */
    if (selector_active)
        ob_draw(rs_trees[FSELECTR], ROOT, MAX_DEPTH);
    return answer >= BUTOFF ? answer - BUTOFF + 1 : 0;
}

void wm_update(WORD mode)
{
    wind_update(mode);
}

void gsx_mxmy(WORD *x, WORD *y)
{
    WORD buttons, modifiers;

    graf_mkstate(&xrat, &yrat, &buttons, &modifiers);
    *x = xrat;
    *y = yrat;
}

void gsx_moff(void) { graf_mouse(M_OFF, NULL); }
void gsx_mon(void) { graf_mouse(M_ON, NULL); }
void set_mouse_to_arrow(void) { graf_mouse(ARROW, NULL); }
void set_mouse_to_hourglass(void) { graf_mouse(HOURGLASS, NULL); }
void dsptch(void) { aes_appl_yield(); }

#if CONF_WITH_MENU_EXTENSION
WORD ev_multi(WORD flags, MOBLK *first, MOBLK *second, MOBLK *third,
              LONG timer, LONG button, WORD *message, WORD result[])
#else
WORD ev_multi(WORD flags, MOBLK *first, MOBLK *second,
              LONG timer, LONG button, WORD *message, WORD result[])
#endif
{
    AES_EVENT_REQUEST request = { 0 };
    AES_INPUT_EVENT event;
    LONG rc;

#if CONF_WITH_MENU_EXTENSION
    MAYBE_UNUSED(third);
#endif
    request.mask = flags;
    request.clicks = (ULONG)button >> 16;
    request.button_mask = ((ULONG)button >> 8) & 3;
    request.button_state = button & 255;
    request.timer_ms = timer;
    if (first) {
        request.mouse1.outside = first->m_out;
        request.mouse1.rectangle = first->m_gr;
    }
    if (second) {
        request.mouse2.outside = second->m_out;
        request.mouse2.rectangle = second->m_gr;
    }
    if (graphics_failed)
        return MU_BUTTON;
    rc = aes_evnt_multi(&request, &event);
    if (rc < 0)
        return MU_BUTTON;
    result[0] = xrat = event.mouse_x;
    result[1] = yrat = event.mouse_y;
    result[2] = event.buttons; result[3] = event.modifiers;
    result[4] = event.key; result[5] = event.clicks;
    if (message && (event.events & AES_EVENT_MESSAGE))
        memcpy(message, &event.message, sizeof(event.message));
    return event.events;
}
