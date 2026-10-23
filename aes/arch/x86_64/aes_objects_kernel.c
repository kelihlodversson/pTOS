/* Owned, immutable resource topology and typed native AES operations.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later.
 */
#include "emutos.h"
#include "struct.h"
#include "obdefs.h"
#include "aesvars.h"
#include "aesext.h"
#include "aes_objects_kernel.h"
#include "aes_accessory.h"
#include "aes_service.h"
#include "gemerror.h"
#include "gemlib.h"
#include "geminit.h"
#include "gemoblib.h"
#include "gemobed.h"
#include "gemmnlib.h"
#include "gemgsxif.h"
#include "gemgrlib.h"
#include "gemctrl.h"
#include "geminput.h"
#include "gemwmlib.h"
#include "gempd.h"
#include "kheap.h"
#include "string.h"
#include "rectfunc.h"
#include "../../../bdos/kproc.h"
#include "../../../bdos/user_callback.h"

typedef struct {
    char text[AES_TREE_MAX_TEXT];
    union {
        struct { TEDINFO ted; char template[AES_TREE_MAX_TEXT], validation[AES_TREE_MAX_TEXT]; } edit;
        struct { CICONBLK icon; CICON colours[8]; } icon;
        BITBLK image;
        struct {
            USERBLK block;
#if CONF_WITH_USER_CALLBACKS
            USER_CALLBACK *callback;
#endif
            AES_TREE_STATE *tree;
            ULONG parameter;
        } user;
    } u;
} NATIVE_PAYLOAD;

struct aes_tree_state {
    AES_TREE_STATE *next;
    PD *owner;
    AESPD *task;
    ULONG handle, bytes, cost, pins;
    ULONG executors[NUM_PDS];
    UWORD count;
    BOOL retired, menu, desktop;
    UBYTE *blob;
    OBJECT *objects;
    NATIVE_PAYLOAD *payload;
    WORD *parents;
};

static AES_TREE_STATE *trees;
static ULONG next_handle, allocated;
static struct {
    AES_TREE_STATE *tree, *menu;
    OBJECT *native_menu;
    AESPD *mouse;
    WORD executor;
    GRECT control;
} modal;
extern PD *run;

static void pin_as(AES_TREE_STATE *t, WORD executor)
{
    t->pins++;
    t->executors[executor]++;
}

static void pin(AES_TREE_STATE *t) { pin_as(t, rlr->p_pid); }

static AES_TREE_STATE *find(ULONG handle)
{
    AES_TREE_STATE *t;
    for (t = trees; t; t = t->next)
        if (t->handle == handle && t->owner == run && !t->retired)
            return t;
    return NULL;
}

AES_TREE_STATE *x86_64_aes_tree_pin(OBJECT *objects)
{
    AES_TREE_STATE *t;
    for (t = trees; t; t = t->next)
        if (t->objects == objects) {
            pin(t);
            return t;
        }
    return NULL;
}

BOOL x86_64_aes_tree_alive(AES_TREE_STATE *t)
{
    return !t || !t->retired;
}

BOOL x86_64_aes_tree_managed(OBJECT *objects)
{
    AES_TREE_STATE *t;
    for (t = trees; t; t = t->next)
        if (t->objects == objects)
            return TRUE;
    return FALSE;
}

BOOL x86_64_aes_tree_cancelled(OBJECT *objects)
{
    AES_TREE_STATE *t;
    for (t = trees; t; t = t->next)
        if (t->objects == objects)
            return t->retired;
    return FALSE;
}

static void destroy(AES_TREE_STATE *t)
{
    AES_TREE_STATE **link;
    UWORD i;

    for (link = &trees; *link; link = &(*link)->next)
        if (*link == t) {
            *link = t->next;
            break;
        }
#if CONF_WITH_USER_CALLBACKS
    for (i = 0; t->objects && t->payload && i < t->count; i++)
        if ((t->objects[i].ob_type & 0xff) == G_USERDEF)
            user_callback_drop(&t->payload[i].u.user.callback);
#else
    MAYBE_UNUSED(i);
#endif
    allocated -= t->cost;
    kfree(t->parents);
    kfree(t->payload);
    kfree(t->objects);
    kfree(t->blob);
    kfree(t);
}

static void unpin_as(AES_TREE_STATE *t, WORD executor)
{
    if (t) {
        t->pins--;
        t->executors[executor]--;
        if (t->retired && !t->pins && !t->menu && !t->desktop && modal.tree != t)
            destroy(t);
    }
}

void x86_64_aes_tree_unpin(AES_TREE_STATE *t) { unpin_as(t, rlr->p_pid); }

void x86_64_aes_objects_menu_change(OBJECT *objects, WORD show)
{
    AES_TREE_STATE *t, *old = modal.menu;

    if (!modal.tree)
        return;
    modal.menu = NULL;
    modal.native_menu = show ? objects : NULL;
    if (show)
        for (t = trees; t; t = t->next)
            if (t->objects == objects) {
                modal.menu = t;
                pin_as(t, modal.executor);
                break;
            }
    /* A native application may hide/free its old menu during a typed
     * dialog. Never restore that obsolete pointer at modal teardown. */
    unpin_as(old, modal.executor);
}

void x86_64_aes_objects_menu_fixup(OBJECT *objects, WORD box)
{
    AES_TREE_STATE *t;
    WORD i;

    for (t = trees; t; t = t->next)
        if (t->objects == objects) {
            /* menu_fixup replaces the whole Desk child list, including
             * any original children outside the reserved DA slots. */
            for (i = 0; i < t->count; i++)
                if (t->parents[i] == box)
                    t->parents[i] = -1;
            for (i = objects[box].ob_head; i != box; i = objects[i].ob_next)
                t->parents[i] = box;
            return;
        }
}

static void modal_end(void)
{
    AES_TREE_STATE *t = modal.tree, *menu = modal.menu;
    WORD executor = modal.executor;

    if (!t)
        return;
    ct_chgown(modal.mouse && !(modal.mouse->p_stat & X32_AES_DEAD) ?
              modal.mouse : fpdnm(NULL, 0), &modal.control);
    if (!gl_mntree && x86_64_aes_tree_alive(menu))
        gl_mntree = menu ? menu->objects : modal.native_menu;
    memset(&modal, 0, sizeof(modal));
    unpin_as(menu, executor);
    unpin_as(t, executor);
}

BOOL x86_64_aes_objects_control(void)
{
    if (!modal.tree || modal.tree->retired)
        return FALSE;
    /* The control manager can wake for a pending menu key after FORM
     * begins. Its ordinary active-window refresh must preserve modal input. */
    ct_chgown(modal.tree->task, &gl_rscreen);
    return TRUE;
}

void x86_64_aes_objects_reap(void)
{
    AES_TREE_STATE *t, *next;

#if CONF_WITH_USER_CALLBACKS
    if (user_callback_active())
        return;
#endif
    if (modal.tree && modal.tree->retired)
        modal_end();
    for (t = trees; t; t = next) {
        next = t->next;
        if (!t->retired)
            continue;
        /* Hiding a menu and clearing a desktop are pointer updates and a
         * nonblocking control-manager wake; they never acquire a native lock. */
        if (t->menu) {
            if (gl_mntree == t->objects)
                mn_bar(NULL, FALSE);
            t->menu = FALSE;
        }
        if (t->desktop) {
            wm_snapshot_clear(t->objects);
            t->desktop = FALSE;
        }
        if (!t->pins)
            destroy(t);
    }
}

void x86_64_aes_objects_exit(PD *owner)
{
    AES_TREE_STATE *t;
    for (t = trees; t; t = t->next)
        if (t->owner == owner) {
            t->retired = TRUE;
            t->owner = NULL;             /* basepage reuse cannot revive it */
        }
}

void x86_64_aes_objects_finished(AESPD *executor)
{
    AES_TREE_STATE *t;
    if (modal.tree && modal.tree->task == executor)
        modal_end();
    for (t = trees; t; t = t->next) {
        t->pins -= t->executors[executor->p_pid];
        t->executors[executor->p_pid] = 0;
    }
    x86_64_aes_objects_reap();
}

void x86_64_aes_objects_drain(void)
{
    /* Only after every user and native AES continuation is cancelled. */
    memset(&modal, 0, sizeof(modal));
    while (trees) {
        if (gl_mntree == trees->objects)
            gl_mntree = NULL;
        wm_snapshot_clear(trees->objects);
        destroy(trees);
    }
}

void x86_64_aes_tree_message(PD *owner, WORD *message)
{
    AES_TREE_STATE *t;
    ULONG handle = 0;

    if (message[0] != 10)                /* MN_SELECTED has legacy tree slots */
        return;
    for (t = trees; t; t = t->next)
        if (!t->retired && t->owner == owner && t->menu)
            handle = t->handle;
    message[5] = (WORD)handle;
    message[6] = (WORD)(handle >> 16);
}

static BOOL connected(AES_TREE_STATE *t, UWORD object)
{
    WORD at = object;
    if (object >= t->count)
        return FALSE;
    while (at > 0)
        at = t->parents[at];
    return at == 0;
}

static BOOL visible(AES_TREE_STATE *t, WORD object)
{
    for (; object != -1; object = t->parents[object])
        if (t->objects[object].ob_flags & HIDETREE)
            return FALSE;
    return TRUE;
}

static WORD user_draw(PARMBLK *pb)
{
    NATIVE_PAYLOAD *p = (NATIVE_PAYLOAD *)(uintptr_t)pb->pb_parm;
    AES_TREE_STATE *t = p->u.user.tree;
#if CONF_WITH_USER_CALLBACKS
    AES_OBJECT_DRAW draw = { 0 };
    ULONG state;

    if (t->retired)
        return 0;
    draw.tree = t->handle;
    draw.parameter = p->u.user.parameter;
    draw.object = pb->pb_obj;
    draw.previous = pb->pb_prevstate;
    draw.current = pb->pb_currstate;
    memcpy(&draw.rectangle, &pb->pb_x, sizeof(GRECT));
    memcpy(&draw.clip, &pb->pb_xc, sizeof(GRECT));
    if (user_callback_invoke_data(p->u.user.callback, &draw, sizeof(draw), &state))
        return 0;
    return state & 0x007f;
#else
    MAYBE_UNUSED(t);
    return 0;
#endif
}

#if CONF_WITH_USER_CALLBACKS
static void fallback(int unused) { MAYBE_UNUSED(unused); }
#endif

static void native_icon(AES_TREE_STATE *t, NATIVE_PAYLOAD *p, const AES_OBJECT_ICON *src)
{
    ICONBLK *dst = &p->u.icon.icon.monoblk;
    const AES_OBJECT_STRING *text = (const AES_OBJECT_STRING *)(t->blob + src->text);

    memcpy(p->text, t->blob + text->text, text->capacity);
    dst->ib_pmask = (WORD *)(t->blob + src->mask);
    dst->ib_pdata = (WORD *)(t->blob + src->data);
    dst->ib_ptext = p->text;
    dst->ib_char = src->character;
    dst->ib_xchar = src->character_x; dst->ib_ychar = src->character_y;
    memcpy(&dst->ib_xicon, &src->icon, sizeof(GRECT));
    memcpy(&dst->ib_xtext, &src->label, sizeof(GRECT));
}

static LONG create(UQUAD address, ULONG bytes)
{
    AES_TREE_HEADER header;
    AES_TREE_STATE *t, *other;
    AES_OBJECT *src;
    ULONG cost, owned = 0, i, j, next;
    LONG rc;

    if (bytes < sizeof(header) || bytes > AES_TREE_MAX_BYTES || (address & 3) ||
        !kproc_copy_from_user(&header, address, sizeof(header)))
        return EIMBA;
    if (!header.count || header.count > AES_TREE_MAX_OBJECTS || header.bytes != bytes)
        return EINVAL;
    cost = sizeof(*t) + bytes + header.count * (sizeof(OBJECT) + sizeof(NATIVE_PAYLOAD) + sizeof(WORD));
    for (other = trees; other; other = other->next)
        if (other->owner == run)
            owned++;
    if (owned >= 16 || cost > 16UL * 1024 * 1024 - allocated || next_handle == 0x7fffffffUL)
        return ENSMEM;
    t = kalloc(sizeof(*t));
    if (!t)
        return ENSMEM;
    t->cost = cost;
    allocated += cost;
    t->count = header.count;
    t->bytes = bytes;
    t->blob = kalloc(bytes);
    t->objects = kalloc(t->count * sizeof(OBJECT));
    t->payload = kalloc(t->count * sizeof(NATIVE_PAYLOAD));
    t->parents = kalloc(t->count * sizeof(WORD));
    if (!t->blob || !t->objects || !t->payload || !t->parents) {
        destroy(t);
        return ENSMEM;
    }
    if (!kproc_copy_from_user(t->blob, address, bytes)) {
        destroy(t);
        return EIMBA;
    }
    rc = ((AES_TREE_HEADER *)t->blob)->count != t->count ? EINVAL :
         aes_tree_validate(t->blob, bytes, t->parents);
    if (rc) {
        destroy(t);
        return rc ? rc : EINVAL;
    }
    src = (AES_OBJECT *)((AES_TREE_HEADER *)t->blob + 1);
    for (i = 0; i < t->count; i++) {
        OBJECT *o = &t->objects[i];
        NATIVE_PAYLOAD *p = &t->payload[i];
        const void *spec = ((src[i].type & 0xff) == G_BOX || (src[i].type & 0xff) == G_IBOX ||
                            (src[i].type & 0xff) == G_BOXCHAR) ?
                           NULL : t->blob + src[i].spec;

        o->ob_next = src[i].next; o->ob_head = src[i].head; o->ob_tail = src[i].tail;
        o->ob_type = src[i].type;
        o->ob_flags = (src[i].flags & ~LASTOB) | (i + 1 == t->count ? LASTOB : 0);
        o->ob_state = src[i].state;
        memcpy(&o->ob_x, &src[i].rectangle, sizeof(GRECT));
        switch (o->ob_type & 0xff) {
        case G_BOX: case G_IBOX: case G_BOXCHAR:
            o->ob_spec.index = src[i].spec;
            break;
        case G_BUTTON: case G_STRING: case G_TITLE: {
            const AES_OBJECT_STRING *s = spec;
            memcpy(p->text, t->blob + s->text, s->capacity);
            o->ob_spec.free_string = p->text;
            break;
        }
        case G_TEXT: case G_BOXTEXT: case G_FTEXT: case G_FBOXTEXT: {
            const AES_OBJECT_TEXT *s = spec;
            TEDINFO *d = &p->u.edit.ted;
            memcpy(p->text, t->blob + s->text, s->text_capacity);
            memcpy(p->u.edit.template, t->blob + s->template, s->template_capacity);
            memcpy(p->u.edit.validation, t->blob + s->validation, s->validation_capacity);
            d->te_ptext = p->text; d->te_ptmplt = p->u.edit.template; d->te_pvalid = p->u.edit.validation;
            d->te_font = s->font; d->te_just = s->justification; d->te_color = s->colour;
            d->te_thickness = s->thickness; d->te_txtlen = s->text_capacity; d->te_tmplen = s->template_capacity;
            o->ob_spec.tedinfo = d;
            break;
        }
        case G_IMAGE: {
            const AES_OBJECT_IMAGE *s = spec;
            BITBLK *d = &p->u.image;
            d->bi_pdata = t->blob + s->data; d->bi_wb = s->row_bytes; d->bi_hl = s->height;
            d->bi_x = s->x; d->bi_y = s->y; d->bi_color = s->colour;
            o->ob_spec.bitblk = d;
            break;
        }
        case G_ICON: case G_CICON:
            native_icon(t, p, spec);
            o->ob_spec.ciconblk = &p->u.icon.icon;
            if ((o->ob_type & 0xff) == G_CICON) {
                next = ((const AES_OBJECT_CICON *)spec)->colours;
                for (j = 0; next; j++) {
                    const AES_OBJECT_COLOUR *s = (const AES_OBJECT_COLOUR *)(t->blob + next);
                    CICON *c = &p->u.icon.colours[j];
                    c->num_planes = s->planes;
                    c->col_data = (WORD *)(t->blob + s->data); c->col_mask = (WORD *)(t->blob + s->mask);
                    c->sel_data = s->selected_data ? (WORD *)(t->blob + s->selected_data) : NULL;
                    c->sel_mask = s->selected_mask ? (WORD *)(t->blob + s->selected_mask) : NULL;
                    next = s->next;
                    c->next_res = next ? c + 1 : NULL;
                }
                p->u.icon.icon.mainlist = j ? p->u.icon.colours : NULL;
            }
            break;
        case G_USERDEF: {
            const AES_OBJECT_USERDEF *s = spec;
#if CONF_WITH_USER_CALLBACKS
            USER_CALLBACK_NATIVE native;
            ULONG old;
            memset(&native, 0, sizeof(native));
            native.i32_void = fallback;
            rc = s->handler >= USER_CALLBACK_IMAGE && s->handler < USER_CALLBACK_IMAGE + 4096 ? EIMBA :
                user_callback_exchange(&p->u.user.callback, USER_CALLBACK_DATA_U16, s->handler, &native, &old);
            if (!s->handler || rc) {
                destroy(t);
                return rc ? rc : EIMBA;
            }
#else
            MAYBE_UNUSED(s);
            destroy(t);
            return EINVFN;
#endif
            p->u.user.tree = t;
            p->u.user.parameter = s->parameter;
            p->u.user.block.ub_code = user_draw;
            p->u.user.block.ub_parm = (intptr_t)p;
            o->ob_spec.userblk = &p->u.user.block;
            break;
        }
        }
    }
    t->handle = ++next_handle;
    t->owner = run;
    t->task = rlr;
    t->next = trees;
    trees = t;
    return t->handle;
}

static BOOL geometry(AES_TREE_STATE *t)
{
    UWORD i;
    WORD at;
    LONG x, y;
    for (i = 0; i < t->count; i++) {
        x = y = 0;
        for (at = i; at != -1; at = t->parents[at]) {
            x += t->objects[at].ob_x;
            y += t->objects[at].ob_y;
        }
        if (x < -10000 || x > 10000 || y < -10000 || y > 10000 ||
            t->objects[i].ob_width < 0 || t->objects[i].ob_width > 10000 ||
            t->objects[i].ob_height < 0 || t->objects[i].ob_height > 10000)
            return FALSE;
    }
    return TRUE;
}

static BOOL clip_rectangle(const GRECT *r)
{
    return r->g_x >= -10000 && r->g_x <= 10000 && r->g_y >= -10000 && r->g_y <= 10000 &&
           r->g_w >= 0 && r->g_w <= 10000 && r->g_h >= 0 && r->g_h <= 10000;
}

static BOOL valid_request(AES_OBJECT_REQUEST r)
{
    AES_OBJECT_REQUEST zero = { 0 };

    switch (r.operation) {
    case AES_OBJECT_CREATE: r.data = r.bytes = 0; break;
    case AES_OBJECT_READ: r.tree = r.data = r.bytes = 0; break;
    case AES_OBJECT_SET_TEXT: r.tree = r.object = r.data = r.bytes = 0; break;
    case AES_OBJECT_EDIT: r.tree = r.object = r.value = r.extra = r.bytes = 0; break;
    case AES_OBJECT_DRAW_TREE: case AES_OBJECT_FIND:
        r.tree = r.object = r.value = 0; memset(&r.rectangle, 0, sizeof(r.rectangle)); break;
    case AES_OBJECT_STATE:
        r.tree = r.object = r.value = r.extra = 0; memset(&r.rectangle, 0, sizeof(r.rectangle)); break;
    case AES_OBJECT_RECTANGLE:
        r.tree = r.object = 0; memset(&r.rectangle, 0, sizeof(r.rectangle)); break;
    case AES_OBJECT_KEY: r.tree = r.object = r.value = 0; break;
    case AES_OBJECT_FORM:
        r.tree = r.value = 0; memset(&r.rectangle, 0, sizeof(r.rectangle)); break;
    case AES_OBJECT_MENU: r.tree = r.value = 0; break;
    case AES_OBJECT_DESKTOP: r.tree = r.object = r.value = 0; break;
    case AES_OBJECT_GET: case AES_OBJECT_OFFSET: r.tree = r.object = 0; break;
    case AES_OBJECT_CENTER: case AES_OBJECT_DESTROY: r.tree = 0; break;
    case AES_OBJECT_MOUSE: r.value = 0; break;
    case AES_OBJECT_MOUSE_STATE: case AES_OBJECT_METRICS: break;
    default: return FALSE;
    }
    r.operation = 0;
    return !memcmp(&r, &zero, sizeof(r));
}

static void object_record(AES_TREE_STATE *t, UWORD i, AES_OBJECT *out)
{
    AES_OBJECT *wire = (AES_OBJECT *)((AES_TREE_HEADER *)t->blob + 1);
    OBJECT *o = &t->objects[i];
    *out = wire[i];                       /* spec remains a blob offset/value */
    out->next = o->ob_next; out->head = o->ob_head; out->tail = o->ob_tail;
    out->flags = o->ob_flags; out->state = o->ob_state;
    memcpy(&out->rectangle, &o->ob_x, sizeof(GRECT));
}

static LONG read_tree(AES_TREE_STATE *t, UQUAD output, ULONG bytes)
{
    UWORD i;
    AES_OBJECT o, *wire = (AES_OBJECT *)((AES_TREE_HEADER *)t->blob + 1);
    ULONG at, capacity;

    if (bytes != t->bytes)
        return EINVAL;
    if (!kproc_validate_user_write(output, bytes) || !kproc_copy_to_user(output, t->blob, bytes))
        return EIMBA;
    /* Every address below comes from the immutable original, never an
     * editable payload or a native pointer returned by the object library. */
    for (i = 0; i < t->count; i++) {
        at = capacity = 0;
        switch (wire[i].type & 0xff) {
        case G_TEXT: case G_BOXTEXT: case G_FTEXT: case G_FBOXTEXT: {
            const AES_OBJECT_TEXT *text = (const AES_OBJECT_TEXT *)(t->blob + wire[i].spec);
            at = text->text; capacity = text->text_capacity;
            break;
        }
        case G_STRING: case G_TITLE: case G_BUTTON: {
            const AES_OBJECT_STRING *text = (const AES_OBJECT_STRING *)(t->blob + wire[i].spec);
            at = text->text; capacity = text->capacity;
            break;
        }
        }
        if (capacity && !kproc_copy_to_user(output + at, t->payload[i].text, capacity))
            return EIMBA;
    }
    /* Metadata wins over any deliberately overlapping text ranges. */
    if (!kproc_copy_to_user(output, t->blob, sizeof(AES_TREE_HEADER)))
        return EIMBA;
    for (i = 0; i < t->count; i++) {
        object_record(t, i, &o);
        if (!kproc_copy_to_user(output + sizeof(AES_TREE_HEADER) + i * sizeof(o), &o, sizeof(o)))
            return EIMBA;
    }
    return E_OK;
}

static BOOL menu_shape(AES_TREE_STATE *t)
{
    OBJECT *o = t->objects;
    WORD menus, box, title, item;
    UWORD count = 0;
    LONG available, ignored;
    LONG x, y, width, height, stride;
    WORD ax, ay;

    if (gl_width > 10000 || gl_height > 10000 || t->count < 8 ||
        o[0].ob_head != 1 || o[1].ob_head != 2 ||
        o[1].ob_height <= 0 || o[2].ob_width <= 0 || o[2].ob_height <= 0 ||
        (menus = o[0].ob_tail) < 3 || (box = o[menus].ob_head) < 3 ||
        o[2].ob_head < 3 || box + 2 + NUM_ACCS >= t->count)
        return FALSE;
    for (item = box + 1; item <= box + 2 + NUM_ACCS; item++)
        if ((o[item].ob_type & 0xff) != G_STRING || o[item].ob_head != -1 ||
            (t->parents[item] != box && t->parents[item] != -1))
            return FALSE;
    title = o[2].ob_head;
    item = box;
    gsx_mret(&ignored, &available);
    while (title != 2 && item != menus && ++count <= t->count) {
        if ((o[title].ob_type & 0xff) != G_TITLE ||
            o[title].ob_width <= 0 || o[title].ob_height <= 0 ||
            ((o[item].ob_type & 0xff) != G_BOX && (o[item].ob_type & 0xff) != G_IBOX))
            return FALSE;
        ob_offset(o, item, &ax, &ay);
        x = ax; y = ay;
        width = o[item].ob_width;
        height = o[item].ob_height;
        if (item == box && height < (2 + NUM_ACCS) * gl_hchar)
            height = (2 + NUM_ACCS) * gl_hchar;
        stride = ((x - 1 + width + 2 + 15) / 16 * 16 - (x - 1) / 16 * 16) / 8;
        if (width <= 0 || height <= 0 || x < 1 || y < 0 ||
            x + width + 1 > gl_width / 16 * 16 || y + height + 2 > gl_height ||
            stride * gl_nplanes * (height + 2) > available)
            return FALSE;               /* native menu backup must fit, including rounding */
        title = o[title].ob_next;
        item = o[item].ob_next;
    }
    if (!count || title != 2 || item != menus)
        return FALSE;
    for (count = 0; count < t->count; count++)
        if ((o[count].ob_type & 0xff) == G_USERDEF)
            return FALSE;               /* menu control never calls user code */
    return TRUE;
}

static LONG key_event(AES_TREE_STATE *t, UWORD object, WORD key, AES_OBJECT_RESULT *out)
{
    LONG i, step = 1;
    UWORD flag;
    BOOL exit = key == 0x1c0d || key == 0x720d;

    out->key = key;
    out->index = object;
    out->value = 1;
    if (exit) {
        i = 0; flag = DEFAULT;
    } else if (key == 0x0f09 || key == 0x5000 || key == 0x4800) {
        if (key == 0x4800)
            step = -1;
        i = object + step; flag = EDITABLE;
    } else
        return E_OK;
    out->key = 0;
    for (; i >= 0 && i < t->count; i += step)
        if (connected(t, i) && (t->objects[i].ob_flags & flag) &&
            visible(t, i) && !(t->objects[i].ob_state & DISABLED)) {
            out->index = i;
            if (exit) {
                ob_change(t->objects, i, t->objects[i].ob_state | SELECTED, TRUE);
                out->value = 0;
            }
            break;
        }
    return E_OK;
}

LONG x86_64_aes_objects(UQUAD input, UQUAD output)
{
    AES_OBJECT_REQUEST r;
    AES_OBJECT_RESULT result = { 0 };
    AES_TREE_STATE *t, *other;
    OBJECT *o;
    AES_OBJECT *wire;
    LONG rc = E_OK;
    GRECT saved, clip, old;
    ULONG capacity = 0, length;
    char text[AES_TREE_MAX_TEXT];
    WORD index;
    BOOL render, changed = FALSE;

    if ((input & 3) || !kproc_copy_from_user(&r, input, sizeof(r)) ||
        (output & 3) || !kproc_validate_user_write(output, sizeof(result)))
        return EIMBA;
    if (r.operation < AES_OBJECT_CREATE || r.operation > AES_OBJECT_METRICS)
        return EINVFN;
    if (!valid_request(r))
        return EINVAL;
    if (!x86_64_aes_owned(run))
        return EACCDN;
#if CONF_WITH_USER_CALLBACKS
    if (!user_callback_work_register(x86_64_aes_objects_reap))
        return ENSMEM;
#endif
    x86_64_aes_objects_reap();
    if (r.operation == AES_OBJECT_CREATE) {
        rc = create(r.data, r.bytes);
        if (rc < 0)
            return rc;
        result.value = rc;
        if (kproc_copy_to_user(output, &result, sizeof(result)))
            return E_OK;
        t = find(result.value);
        if (t)
            destroy(t);
        return EIMBA;
    }
    t = r.tree ? find(r.tree) : NULL;
    if (!t && r.operation != AES_OBJECT_MOUSE && r.operation != AES_OBJECT_MOUSE_STATE &&
        r.operation != AES_OBJECT_METRICS)
        return EIHNDL;
    if (t && r.object >= t->count)
        return EINVAL;
    if (t && r.operation != AES_OBJECT_DESTROY && r.operation != AES_OBJECT_READ && !connected(t, r.object))
        return EINVAL;
    render = r.operation != AES_OBJECT_GET && r.operation != AES_OBJECT_READ &&
             r.operation != AES_OBJECT_OFFSET && r.operation != AES_OBJECT_FIND &&
             r.operation != AES_OBJECT_DESTROY;
    if (render && !clip_rectangle(&r.rectangle))
        return EINVAL;
    if (render && ((wind_spb.sy_tas && wind_spb.sy_owner != rlr) ||
                   !aes_service_window_unlocked(run)))
        return EAGAIN;
    if (t)
        pin(t);
    o = t ? &t->objects[r.object] : NULL;
    wire = t ? (AES_OBJECT *)((AES_TREE_HEADER *)t->blob + 1) : NULL;
    gsx_gclip(&saved);
    clip = r.rectangle;
    if (!clip.g_w || !clip.g_h)
        clip = gl_rscreen;
    else if (!rc_intersect(&gl_rscreen, &clip)) {
        x86_64_aes_tree_unpin(t);
        return EINVAL;
    }
    if (render) {
        wm_update(BEG_UPDATE);          /* prechecked: cannot block */
        gsx_sclip(&clip);
    }
    switch (r.operation) {
    case AES_OBJECT_DESTROY:
        t->retired = TRUE;
        break;
    case AES_OBJECT_READ:
        rc = read_tree(t, r.data, r.bytes);
        break;
    case AES_OBJECT_GET:
        object_record(t, r.object, &result.object);
        result.value = t->count;
        result.index = t->parents[r.object];
        result.key = visible(t, r.object);
        break;
    case AES_OBJECT_DRAW_TREE:
        if (r.value < 0 || r.value > MAX_DEPTH || !clip_rectangle(&r.rectangle))
            rc = EINVAL;
        else
            ob_draw(t->objects, r.object, r.value);
        break;
    case AES_OBJECT_FIND:
        if (r.value < 0 || r.value > MAX_DEPTH || r.rectangle.g_w || r.rectangle.g_h ||
            !clip_rectangle(&r.rectangle))
            rc = EINVAL;
        else
            result.value = ob_find(t->objects, r.object, r.value, r.rectangle.g_x, r.rectangle.g_y);
        break;
    case AES_OBJECT_OFFSET:
        ob_offset(t->objects, r.object, &result.rectangle.g_x, &result.rectangle.g_y);
        result.rectangle.g_w = o->ob_width; result.rectangle.g_h = o->ob_height;
        break;
    case AES_OBJECT_STATE:
        if ((r.value & ~0x7f) || r.extra < 0 || r.extra > 1 || !clip_rectangle(&r.rectangle))
            rc = EINVAL;
        else
            ob_change(t->objects, r.object, r.value, r.extra);
        break;
    case AES_OBJECT_RECTANGLE:
        if (t->menu || !clip_rectangle(&r.rectangle)) { rc = EINVAL; break; }
        memcpy(&old, &o->ob_x, sizeof(old));
        memcpy(&o->ob_x, &r.rectangle, sizeof(old));
        if (!geometry(t)) {
            memcpy(&o->ob_x, &old, sizeof(old)); rc = EINVAL;
        }
        break;
    case AES_OBJECT_CENTER:
        if (t->menu || t->desktop) { rc = EACCDN; break; }
        memcpy(&old, &o->ob_x, sizeof(old));
        ob_center(t->objects, &result.rectangle);
        if (!geometry(t)) {
            memcpy(&o->ob_x, &old, sizeof(old)); rc = EINVAL;
        }
        break;
    case AES_OBJECT_SET_TEXT:
        if ((o->ob_type & 0xff) == G_STRING || (o->ob_type & 0xff) == G_TITLE ||
            (o->ob_type & 0xff) == G_BUTTON)
            capacity = o->ob_spec.free_string == t->payload[r.object].text ?
                ((AES_OBJECT_STRING *)(t->blob + wire[r.object].spec))->capacity : 0;
        else if ((o->ob_type & 0xff) == G_TEXT || (o->ob_type & 0xff) == G_BOXTEXT ||
                 (o->ob_type & 0xff) == G_FTEXT || (o->ob_type & 0xff) == G_FBOXTEXT)
            capacity = ((AES_OBJECT_TEXT *)(t->blob + wire[r.object].spec))->text_capacity;
        if (!capacity || !r.bytes || r.bytes > capacity) { rc = EINVAL; break; }
        if (!kproc_copy_from_user(text, r.data, r.bytes)) { rc = EIMBA; break; }
        if (text[r.bytes - 1]) { rc = EINVAL; break; }
        memcpy(t->payload[r.object].text, text, r.bytes);
        break;
    case AES_OBJECT_EDIT:
        length = strlen(t->payload[r.object].text);
        if (!(o->ob_flags & EDITABLE) || !visible(t, r.object) ||
            r.extra < 0 || r.extra > 3 || r.bytes > length) {
            rc = EINVAL; break;
        }
        index = r.bytes;
        ob_edit(t->objects, r.object, r.value, &index, r.extra);
        result.index = index;
        break;
    case AES_OBJECT_KEY:
        rc = key_event(t, r.object, r.value, &result);
        break;
    case AES_OBJECT_MENU:
        if (modal.tree || r.value < 0 || r.value > 1 || t->pins > 1 || !menu_shape(t)) {
            rc = EINVAL; break;
        }
        for (other = trees; other; other = other->next)
            if (other != t && other->menu && (other->owner != run || other->pins))
                break;
        if (other) { rc = EAGAIN; break; }
        if (r.value) {
            for (other = trees; other; other = other->next)
                if (other != t && other->owner == run)
                    other->menu = FALSE;
            t->menu = TRUE;
            mn_bar(t->objects, TRUE);
        } else if (t->menu) {
            if (gl_mntree == t->objects)
                mn_bar(NULL, FALSE);
            t->menu = FALSE;
        }
        break;
    case AES_OBJECT_DESKTOP:
        if (r.value < 0 || r.value > 1) { rc = EINVAL; break; }
        for (other = trees; other; other = other->next)
            if (other != t && other->desktop && other->owner != run)
                break;
        if (other) { rc = EAGAIN; break; }
        if (r.value) {
            for (other = trees; other; other = other->next)
                if (other != t && other->owner == run)
                    other->desktop = FALSE;
            t->desktop = TRUE;
            wm_snapshot_set(t->objects, r.object);
        } else {
            wm_snapshot_clear(t->objects);
            t->desktop = FALSE;
        }
        break;
    case AES_OBJECT_FORM:
        if (r.value < 0 || r.value > 1) { rc = EINVAL; break; }
        if (r.value) {
            if (modal.tree) { rc = EAGAIN; break; }
            modal.tree = t; pin(t);
            modal.executor = rlr->p_pid;
            modal.native_menu = gl_mntree;
            modal.menu = x86_64_aes_tree_pin(gl_mntree);
            get_mown(&modal.mouse); get_ctrl(&modal.control);
            gl_mntree = NULL;
            ct_chgown(rlr, &gl_rscreen);
        } else if (modal.tree == t)
            modal_end();
        else rc = EACCDN;
        break;
    case AES_OBJECT_MOUSE:
        if ((r.value < 0 || r.value > 7) && r.value != 256 && r.value != 257)
            rc = EINVAL;
        else
            gr_mouse(r.value, NULL);
        break;
    case AES_OBJECT_MOUSE_STATE:
        gr_mkstate(&result.rectangle.g_x, &result.rectangle.g_y, &index, &result.key);
        result.value = index;
        break;
    case AES_OBJECT_METRICS:
        result.value = gl_handle;
        result.rectangle.g_x = gl_wchar; result.rectangle.g_y = gl_hchar;
        result.rectangle.g_w = gl_wbox; result.rectangle.g_h = gl_hbox;
        break;
    default:
        rc = EINVFN;
    }
    if (render) {
        gsx_sclip(&saved);
        wm_update(END_UPDATE);          /* may yield: only pinned state below */
    }
    changed = t && t->retired;
    if (!rc && changed && r.operation != AES_OBJECT_DESTROY)
        rc = EACCDN;
    if (!rc && !kproc_copy_to_user(output, &result, sizeof(result)))
        rc = EIMBA;
    x86_64_aes_tree_unpin(t);
    x86_64_aes_objects_reap();
    return rc;
}
