/* Typed snapshot calls and a cancellable modal loop in x32 user space.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later.
 */
#include "aes_objects.h"
#include "aes_service.h"
#include "gemerror.h"

LONG aes_object(const AES_OBJECT_REQUEST *request, AES_OBJECT_RESULT *result)
{
    AES_OBJECT_RESULT unused;
    register UQUAD rax __asm__("rax") = ((UQUAD)AES_SERVICE_CLASS << 32) | AES_SERVICE_OBJECT;
    register UQUAD rdi __asm__("rdi") = (uintptr_t)request;
    register UQUAD rsi __asm__("rsi") = (uintptr_t)(result ? result : &unused);
    register UQUAD rdx __asm__("rdx") = 0;
    register UQUAD r10 __asm__("r10") = 0;

    __asm__ volatile ("syscall" : "+r"(rax)
                      : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10)
                      : "rcx", "r11", "memory", "cc");
    return (LONG)rax;
}

static LONG simple(ULONG tree, UWORD op, UWORD object, WORD value, WORD extra,
                    ULONG index, AES_OBJECT_RESULT *out)
{
    AES_OBJECT_REQUEST r = { tree, op, object, value, extra, { 0 }, 0, index };
    LONG rc;
    do {
        rc = aes_object(&r, out);
        if (rc == EAGAIN && aes_appl_yield())
            return EACCDN;
    } while (rc == EAGAIN);
    return rc;
}

static LONG state(ULONG tree, WORD object, UWORD flags)
{
    return simple(tree, AES_OBJECT_STATE, object, flags, 1, 0, NULL);
}

static BOOL contains_point(const GRECT *r, WORD x, WORD y)
{
    return x >= r->g_x && y >= r->g_y &&
           (LONG)x < (LONG)r->g_x + r->g_w && (LONG)y < (LONG)r->g_y + r->g_h;
}

static LONG click(ULONG tree, WORD object, UWORD clicks, AES_OBJECT_RESULT *out)
{
    AES_OBJECT_RESULT node, bounds, sibling;
    AES_EVENT_REQUEST wait = { 0 };
    AES_INPUT_EVENT event;
    LONG rc, i;
    WORD parent;
    UWORD flags, original, selected;
    BOOL in = TRUE;

    rc = simple(tree, AES_OBJECT_GET, object, 0, 0, 0, &node);
    if (rc)
        return rc;
    flags = node.object.flags;
    original = node.object.state;
    out->index = 0;
    out->value = 1;
    if (original & 8)                    /* DISABLED */
        return E_OK;
    if (flags & 1) {                    /* SELECTABLE */
        selected = original ^ 1;
        if (flags & 16) {               /* RBUTTON: only this sibling stays selected */
            selected = original | 1;
            parent = node.index;
            if (parent < 0) return EINVAL;
            rc = simple(tree, AES_OBJECT_GET, parent, 0, 0, 0, &sibling);
            if (rc) return rc;
            i = sibling.object.head;
            while (i >= 0 && i != parent) {
                rc = simple(tree, AES_OBJECT_GET, i, 0, 0, 0, &sibling);
                if (rc) return rc;
                if ((sibling.object.flags & 16) && (sibling.object.state & 1) && i != object) {
                    rc = state(tree, i, sibling.object.state & ~1);
                    if (rc) return rc;
                }
                i = sibling.object.next;
            }
        }
        rc = state(tree, object, selected);
        if (rc) return rc;
        wait.clicks = wait.button_mask = 1;
        if (flags & 16) {
            /* Native radio buttons commit on press. Moving outside must
             * neither clear the new choice nor restore just one sibling. */
            if (!(flags & 64)) {
                wait.mask = AES_EVENT_BUTTON;
                rc = aes_evnt_multi(&wait, &event);
                if (rc < 0) return rc;
            }
        } else {
            rc = simple(tree, AES_OBJECT_OFFSET, object, 0, 0, 0, &bounds);
            if (rc) return rc;
            wait.mask = AES_EVENT_BUTTON | AES_EVENT_MOUSE1;
            wait.mouse1.rectangle = bounds.rectangle;
            do {
                wait.mouse1.outside = in;
                rc = aes_evnt_multi(&wait, &event);
                if (rc < 0) return rc;
                in = contains_point(&bounds.rectangle, event.mouse_x, event.mouse_y);
                rc = state(tree, object, in ? selected : original);
                if (rc) return rc;
            } while (!(event.events & AES_EVENT_BUTTON));
            if (!in) return E_OK;
        }
        if ((flags & 4) && (selected & 1))  /* EXIT */
            out->value = 0;
    }
    if (flags & 64) {                   /* TOUCHEXIT */
        out->value = 0;
        out->index = (UWORD)object | (clicks == 2 ? 0x8000 : 0);
    } else if (!out->value || (flags & 8))
        out->index = object;
    return E_OK;
}

LONG aes_form_do(ULONG tree, WORD start)
{
    AES_OBJECT_RESULT out, root;
    AES_OBJECT_REQUEST find = { tree, AES_OBJECT_FIND, 0, AES_TREE_MAX_DEPTH };
    AES_EVENT_REQUEST wait = { 0 };
    AES_INPUT_EVENT event;
    LONG rc, end, answer = 0, i;
    WORD edit = 0, next = start, index = 0;
    BOOL update = FALSE, mouse = FALSE, begun = FALSE;

    rc = simple(tree, AES_OBJECT_GET, 0, 0, 0, 0, &root);
    if (rc) return rc;
    if (start < 0 || start >= root.value)
        return EINVAL;
    if (start) {
        rc = simple(tree, AES_OBJECT_GET, start, 0, 0, 0, &out);
        if (rc) return rc;
        if (!out.key || !(out.object.flags & 8) || (out.object.state & 8))
            return EINVAL;
    }
    if (!next)
        for (i = 1; i < root.value; i++) {
            rc = simple(tree, AES_OBJECT_GET, i, 0, 0, 0, &out);
            if (!rc && out.key && (out.object.flags & 8) && !(out.object.state & 8)) {
                next = i;
                break;
            }
        }
    rc = aes_wind_update(AES_BEG_UPDATE);
    if (rc) return rc;
    update = TRUE;
    rc = aes_wind_update(AES_BEG_MCTRL);
    if (rc) goto cleanup;
    mouse = TRUE;
    rc = simple(tree, AES_OBJECT_FORM, 0, 1, 0, 0, NULL);
    if (rc) goto cleanup;
    begun = TRUE;
    wait.mask = AES_EVENT_KEYBOARD | AES_EVENT_BUTTON;
    wait.clicks = 2; wait.button_mask = wait.button_state = 1;
    for (;;) {
        if (next && next != edit) {
            edit = next; next = 0;
            rc = simple(tree, AES_OBJECT_EDIT, edit, 0, 1, 0, &out);
            if (rc) break;
            index = out.index;
        }
        rc = aes_evnt_multi(&wait, &event);
        if (rc < 0) break;
        out.value = 1;
        if (event.events & AES_EVENT_KEYBOARD) {
            rc = simple(tree, AES_OBJECT_KEY, edit, event.key, 0, 0, &out);
            if (rc) break;
            next = out.index;
            if (out.key && edit) {
                rc = simple(tree, AES_OBJECT_EDIT, edit, out.key, 2, index, &out);
                if (rc) break;
                index = out.index;
                out.value = 1;
            }
        }
        if (event.events & AES_EVENT_BUTTON) {
            find.rectangle.g_x = event.mouse_x; find.rectangle.g_y = event.mouse_y;
            rc = aes_object(&find, &out);
            if (rc) break;
            if (out.value >= 0) {
                rc = click(tree, out.value, event.clicks, &out);
                if (rc) break;
                next = out.index;
            } else
                out.value = 1;
        }
        if (!out.value || (next && next != edit)) {
            if (edit) {
                rc = simple(tree, AES_OBJECT_EDIT, edit, 0, 3, index, NULL);
                if (rc) break;
            }
            edit = 0;
        }
        if (!out.value) { answer = (UWORD)next; rc = E_OK; break; }
    }
cleanup:
    if (begun) {
        end = simple(tree, AES_OBJECT_FORM, 0, 0, 0, 0, NULL);
        if (!rc) rc = end;
    }
    /* Unregister releases the old tokens. A callback can register again
     * before the wait resumes; never release that new registration's locks. */
    if (simple(tree, AES_OBJECT_GET, 0, 0, 0, 0, &root) == E_OK) {
        if (mouse) (void)aes_wind_update(AES_END_MCTRL);
        if (update) (void)aes_wind_update(AES_END_UPDATE);
    }
    return rc ? rc : answer;
}
