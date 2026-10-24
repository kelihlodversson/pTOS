/* EmuDesk trees stay in user memory; AES receives owned, pointer-free copies.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "bdosbind.h"
#include "string.h"
#include "gemerror.h"
#include "deskaes.h"
#if CONF_WITH_X32_DESKTOP_TEST
#include "deskapp.h"
#include "deskfpd.h"
#include "deskinf.h"
#include "aesdefs.h"
#include "aesext.h"
#include "gsxdefs.h"
#endif

typedef struct { OBJECT *tree; USERBLK *block; } DRAW_CONTEXT;
typedef struct {
    const void *source;
    ULONG bytes, offset;
} BITMAP_COPY;

typedef struct {
    UBYTE *data;
    BITMAP_COPY *bitmaps;
    UWORD bitmap_count, bitmap_capacity;
    ULONG used;
    BOOL failed;
} BUILDER;
static DESK_TREE retained[2];
#if CONF_WITH_X32_DESKTOP_TEST
static BOOL user_drawn, user_reported, probe_drawn, probe_reported;
static GRECT probe_rectangle;
#endif

void desk_tree_checkpoint(void)
{
#if CONF_WITH_X32_DESKTOP_TEST
    if (probe_drawn && !probe_reported) {
        char message[80];

        probe_reported = TRUE;
        sprintf(message, "x32 desktop program TARGET %d %d\r\n",
                probe_rectangle.g_x + 16, probe_rectangle.g_y + probe_rectangle.g_h / 2);
        desk_test_record(message);
    }
    if (user_drawn && !user_reported) {
        user_reported = TRUE;
        desk_test_record("x32 desktop USERDEF PASS\r\n");
    }
#endif
}

LONG desk_object_call(AES_OBJECT_REQUEST *request, AES_OBJECT_RESULT *result)
{
    LONG rc;

    do {
        rc = aes_object(request, result);
        if (rc == EAGAIN && aes_appl_yield() < 0)
            return EACCDN;
    } while (rc == EAGAIN);
    return rc;
}

static ULONG reserve(BUILDER *b, ULONG bytes)
{
    ULONG at = (b->used + 3) & ~3UL;

    if (at > AES_TREE_MAX_BYTES || bytes > AES_TREE_MAX_BYTES - at) {
        b->failed = TRUE;
        return 0;
    }
    memset(b->data + at, 0, bytes);
    b->used = at + bytes;
    return at;
}

static ULONG copy(BUILDER *b, const void *data, ULONG bytes)
{
    ULONG at = reserve(b, bytes);

    if (at && bytes)
        memcpy(b->data + at, data, bytes);
    return at;
}

/* Desktop objects share icon payload pointers. Cache immutable bitmap
 * copies by pointer and byte length; text and editable fields stay private. */
static ULONG bitmap(BUILDER *b, const void *source, ULONG bytes)
{
    UWORD i;
    ULONG at;

    for (i = 0; i < b->bitmap_count; i++)
        if (b->bitmaps[i].source == source && b->bitmaps[i].bytes == bytes)
            return b->bitmaps[i].offset;
    at = copy(b, source, bytes);
    if (at && b->bitmap_count < b->bitmap_capacity) {
        BITMAP_COPY *entry = &b->bitmaps[b->bitmap_count++];

        entry->source = source;
        entry->bytes = bytes;
        entry->offset = at;
    }
    return at;
}

static ULONG string(BUILDER *b, const char *s, ULONG capacity)
{
    ULONG n, at;

    if (!s)
        s = "";
    n = strlen(s) + 1;
    if (!capacity)
        capacity = n;
    if (capacity < n || capacity > AES_TREE_MAX_TEXT) {
        b->failed = TRUE;
        return 0;
    }
    at = reserve(b, capacity);
    if (at)
        memcpy(b->data + at, s, n);
    return at;
}

static ULONG string_spec(BUILDER *b, const char *s)
{
    ULONG at = reserve(b, sizeof(AES_OBJECT_STRING));
    AES_OBJECT_STRING *p = (AES_OBJECT_STRING *)(b->data + at);

    p->capacity = strlen(s ? s : "") + 1;
    p->text = string(b, s, p->capacity);
    return at;
}

static WORD draw_bridge(const AES_OBJECT_DRAW *draw)
{
    const DRAW_CONTEXT *context = (const DRAW_CONTEXT *)(uintptr_t)draw->parameter;
    PARMBLK p;

    p.pb_tree = context->tree;
    p.pb_obj = draw->object;
    p.pb_prevstate = draw->previous;
    p.pb_currstate = draw->current;
    memcpy(&p.pb_x, &draw->rectangle, sizeof(GRECT));
    memcpy(&p.pb_xc, &draw->clip, sizeof(GRECT));
    p.pb_parm = context->block->ub_parm;
#if CONF_WITH_X32_DESKTOP_TEST
    user_drawn = TRUE;
    if (context->block->ub_code == dr_code &&
        !strcmp(((FNODE *)(uintptr_t)context->block->ub_parm)->f_name, "APPTEST.PRG")) {
        probe_drawn = TRUE;
        probe_rectangle = draw->rectangle;
    }
#endif
    return context->block->ub_code(&p);
}

static void icon(BUILDER *b, AES_OBJECT_ICON *p, const ICONBLK *s)
{
    ULONG bytes;

    if (s->ib_wicon <= 0 || s->ib_hicon <= 0 || (s->ib_wicon & 15)) {
        b->failed = TRUE;
        return;
    }
    bytes = (ULONG)s->ib_wicon / 8 * s->ib_hicon;
    p->mask = bitmap(b, s->ib_pmask, bytes);
    p->data = bitmap(b, s->ib_pdata, bytes);
    p->text = string_spec(b, s->ib_ptext);
    p->character = s->ib_char;
    p->character_x = s->ib_xchar; p->character_y = s->ib_ychar;
    memcpy(&p->icon, &s->ib_xicon, sizeof(GRECT));
    memcpy(&p->label, &s->ib_xtext, sizeof(GRECT));
}

static BOOL mark(OBJECT *tree, WORD node, WORD depth, UBYTE *seen, WORD *count)
{
    WORD child, previous = -1;

    if (node < 0 || node >= AES_TREE_MAX_OBJECTS || depth > AES_TREE_MAX_DEPTH || seen[node])
        return FALSE;
    seen[node] = 1;
    if (node >= *count)
        *count = node + 1;
    child = tree[node].ob_head;
    if (child == -1)
        return tree[node].ob_tail == -1;
    while (child != node) {
        if (!mark(tree, child, depth + 1, seen, count))
            return FALSE;
        previous = child;
        child = tree[child].ob_next;
    }
    return previous == tree[node].ob_tail;
}

BOOL desk_tree_create(DESK_TREE *snapshot, OBJECT *source)
{
    UBYTE seen[AES_TREE_MAX_OBJECTS] = { 0 };
    WORD count = 0, i;
    BUILDER b;
    AES_TREE_HEADER *header;
    AES_OBJECT *objects, *o;
    AES_OBJECT_REQUEST request = { 0 };
    AES_OBJECT_RESULT result;
    OBSPEC spec;
    ULONG at;
    LONG rc;

    memset(snapshot, 0, sizeof(*snapshot));
    if (!source || !mark(source, 0, 0, seen, &count))
        return FALSE;
    b.data = (void *)Malloc(AES_TREE_MAX_BYTES);
    if ((intptr_t)b.data <= 0)
        return FALSE;
    b.bitmap_count = 0;
    b.bitmap_capacity = count * 2;
    b.bitmaps = (void *)Malloc(b.bitmap_capacity * sizeof(*b.bitmaps));
    if ((intptr_t)b.bitmaps <= 0) {
        Mfree(b.data);
        return FALSE;
    }
    b.used = 0; b.failed = FALSE;
    reserve(&b, sizeof(*header) + count * sizeof(*objects));
    header = (AES_TREE_HEADER *)b.data;
    objects = (AES_OBJECT *)(header + 1);
    header->version = AES_TREE_VERSION;
    header->count = count;
    for (i = 0; i < count; i++) {
        o = &objects[i];
        o->next = o->head = o->tail = -1;
        o->type = G_IBOX;
        if (!seen[i])
            continue;
        o->next = i ? source[i].ob_next : -1;
        o->head = source[i].ob_head; o->tail = source[i].ob_tail;
        o->type = source[i].ob_type;
        o->flags = source[i].ob_flags & 0x06ff & ~INDIRECT;
        o->state = source[i].ob_state & 0x7f;
        memcpy(&o->rectangle, &source[i].ob_x, sizeof(GRECT));
        spec = (source[i].ob_flags & INDIRECT) ? *source[i].ob_spec.indirect : source[i].ob_spec;
        switch (o->type & 0xff) {
        case G_BOX: case G_IBOX: case G_BOXCHAR:
            o->spec = spec.index;
            break;
        case G_STRING: case G_BUTTON: case G_TITLE:
            o->spec = string_spec(&b, spec.free_string);
            break;
        case G_TEXT: case G_BOXTEXT: case G_FTEXT: case G_FBOXTEXT: {
            TEDINFO *s = spec.tedinfo;
            AES_OBJECT_TEXT *p;

            at = o->spec = reserve(&b, sizeof(*p));
            p = (AES_OBJECT_TEXT *)(b.data + at);
            p->text_capacity = s->te_txtlen > 0 ? s->te_txtlen : strlen(s->te_ptext) + 1;
            /* Noneditable native resource labels may point at a longer
             * dynamic string (the file selector mask uses this). */
            if (!(o->flags & EDITABLE) && p->text_capacity < strlen(s->te_ptext) + 1)
                p->text_capacity = strlen(s->te_ptext) + 1;
            p->template_capacity = s->te_tmplen > 0 ? s->te_tmplen : strlen(s->te_ptmplt) + 1;
            if (!(o->flags & EDITABLE) && p->text_capacity > AES_TREE_MAX_TED_TEXT) {
                /* Only the visible label is copied; retain the full mask in
                 * the shared selector's caller-owned state. */
                p->text_capacity = AES_TREE_MAX_TED_TEXT;
                p->text = reserve(&b, p->text_capacity);
                if (p->text)
                    strlcpy((char *)b.data + p->text, s->te_ptext, p->text_capacity);
            } else
                p->text = string(&b, s->te_ptext, p->text_capacity);
            p->template = string(&b, s->te_ptmplt, p->template_capacity);
            p->validation_capacity = p->template_capacity;
            if (!s->te_pvalid[0] && !(o->flags & EDITABLE) && p->validation_capacity < 2)
                p->validation_capacity = 2;
            if (p->validation_capacity < strlen(s->te_pvalid) + 1)
                p->validation_capacity = strlen(s->te_pvalid) + 1;
            p->validation = string(&b, !s->te_pvalid[0] && !(o->flags & EDITABLE) ?
                                   "X" : s->te_pvalid, p->validation_capacity);
            p->font = s->te_font; p->justification = s->te_just;
            p->colour = s->te_color; p->thickness = s->te_thickness;
            break;
        }
        case G_IMAGE: {
            BITBLK *s = spec.bitblk;
            AES_OBJECT_IMAGE *p;

            at = o->spec = reserve(&b, sizeof(*p));
            p = (AES_OBJECT_IMAGE *)(b.data + at);
            p->row_bytes = s->bi_wb; p->height = s->bi_hl;
            p->x = s->bi_x; p->y = s->bi_y; p->colour = s->bi_color;
            if (s->bi_wb < 0 || s->bi_hl < 0)
                b.failed = TRUE;
            else
                p->data = copy(&b, s->bi_pdata, (ULONG)s->bi_wb * s->bi_hl);
            break;
        }
        case G_ICON:
            at = o->spec = reserve(&b, sizeof(AES_OBJECT_ICON));
            icon(&b, (AES_OBJECT_ICON *)(b.data + at), spec.iconblk);
            break;
        case G_CICON: {
            AES_OBJECT_CICON *p;
            CICON *colour;
            ULONG *link, bytes;
            WORD versions = 0;

            at = o->spec = reserve(&b, sizeof(*p));
            p = (AES_OBJECT_CICON *)(b.data + at);
            icon(&b, &p->mono, &spec.ciconblk->monoblk);
            bytes = (ULONG)p->mono.icon.g_w / 8 * p->mono.icon.g_h;
            link = &p->colours;
            for (colour = spec.ciconblk->mainlist; colour; colour = colour->next_res) {
                AES_OBJECT_COLOUR *c;

                if (++versions > 8 || colour->num_planes < 1 || colour->num_planes > 8) {
                    b.failed = TRUE;
                    break;
                }
                at = *link = reserve(&b, sizeof(*c));
                c = (AES_OBJECT_COLOUR *)(b.data + at);
                c->planes = colour->num_planes;
                c->data = bitmap(&b, colour->col_data, bytes * c->planes);
                c->mask = bitmap(&b, colour->col_mask, bytes);
                if (colour->sel_data) {
                    c->selected_data = bitmap(&b, colour->sel_data, bytes * c->planes);
                    c->selected_mask = bitmap(&b, colour->sel_mask, bytes);
                }
                link = &c->next;
            }
            break;
        }
        case G_USERDEF: {
            AES_OBJECT_USERDEF *p;
            DRAW_CONTEXT *context;

            at = o->spec = reserve(&b, sizeof(*p));
            p = (AES_OBJECT_USERDEF *)(b.data + at);
            at = reserve(&b, sizeof(*context));
            context = (DRAW_CONTEXT *)(b.data + at);
            context->tree = source; context->block = spec.userblk;
            p->handler = (ULONG)(uintptr_t)draw_bridge;
            p->parameter = (ULONG)(uintptr_t)context;
            break;
        }
        default:
            b.failed = TRUE;
        }
    }
    Mfree(b.bitmaps);
    header->bytes = b.used;
    request.operation = AES_OBJECT_CREATE;
    request.data = (ULONG)(uintptr_t)b.data; request.bytes = b.used;
    rc = b.failed ? EINVAL : desk_object_call(&request, &result);
    if (rc < 0) {
        Cconws("EmuDesk: resource snapshot rejected\r\n");
        Mfree(b.data);
        return FALSE;
    }
    snapshot->source = source;
    snapshot->blob = b.data;
    snapshot->bytes = b.used;
    snapshot->handle = result.value;
    return TRUE;
}

void desk_tree_destroy(DESK_TREE *snapshot)
{
    AES_OBJECT_REQUEST r = { 0 };
    AES_OBJECT_RESULT result;

    if (snapshot->handle) {
        r.operation = AES_OBJECT_DESTROY; r.tree = snapshot->handle;
        desk_object_call(&r, &result);
    }
    if (snapshot->blob)
        Mfree(snapshot->blob);
    memset(snapshot, 0, sizeof(*snapshot));
}

void desk_tree_read(DESK_TREE *snapshot)
{
    AES_OBJECT_REQUEST r = { 0 };
    AES_OBJECT_RESULT result;
    AES_TREE_HEADER *header = (AES_TREE_HEADER *)snapshot->blob;
    AES_OBJECT *o = (AES_OBJECT *)(header + 1);
    UBYTE seen[AES_TREE_MAX_OBJECTS] = { 0 };
    WORD count = 0, i;

    if (!mark(snapshot->source, 0, 0, seen, &count))
        return;
    r.operation = AES_OBJECT_READ; r.tree = snapshot->handle;
    r.data = (ULONG)(uintptr_t)snapshot->blob; r.bytes = snapshot->bytes;
    if (desk_object_call(&r, &result) < 0)
        return;
    for (i = 0; i < header->count; i++) {
        OBSPEC spec;

        if (!seen[i])
            continue;
        snapshot->source[i].ob_state = o[i].state;
        memcpy(&snapshot->source[i].ob_x, &o[i].rectangle, sizeof(GRECT));
        spec = snapshot->source[i].ob_spec;
        if (snapshot->source[i].ob_flags & INDIRECT)
            spec = *spec.indirect;
        if ((o[i].flags & EDITABLE) &&
            ((o[i].type & 0xff) == G_FTEXT || (o[i].type & 0xff) == G_FBOXTEXT)) {
            AES_OBJECT_TEXT *t = (AES_OBJECT_TEXT *)(snapshot->blob + o[i].spec);

            strlcpy(spec.tedinfo->te_ptext, (char *)snapshot->blob + t->text, t->text_capacity);
        }
    }
}

WORD desk_tree_retain(OBJECT *tree, WORD root, BOOL menu, BOOL show)
{
    DESK_TREE next;
    DESK_TREE *old = &retained[menu ? 1 : 0];
    AES_OBJECT_REQUEST r = { 0 };
    AES_OBJECT_RESULT result;

    if (show) {
        if (!desk_tree_create(&next, tree))
            return FALSE;
        next.root = root;
    } else {
        if (!old->handle)
            return TRUE;
        next = *old;
    }
    r.operation = menu ? AES_OBJECT_MENU : AES_OBJECT_DESKTOP;
    r.tree = next.handle; r.object = root; r.value = show;
    if (desk_object_call(&r, &result) < 0) {
        if (show)
            desk_tree_destroy(&next);
        return FALSE;
    }
    desk_tree_destroy(old);
    if (show)
        *old = next;
    return TRUE;
}

LONG desk_tree_operation(OBJECT *tree, WORD object, UWORD operation,
                         WORD value, WORD extra, const GRECT *rectangle,
                         AES_OBJECT_RESULT *result)
{
    DESK_TREE local, *snapshot = &local;
    AES_OBJECT_REQUEST r = { 0 };
    WORD i;
    LONG rc;

    for (i = 0; i < 2; i++)
        if (retained[i].source == tree) {
            if (!desk_tree_retain(tree, retained[i].root, i != 0, TRUE))
                return EINVAL;
            snapshot = &retained[i];
            break;
        }
    if (snapshot == &local && !desk_tree_create(snapshot, tree))
        return EINVAL;
    r.tree = snapshot->handle; r.object = object; r.operation = operation;
    r.value = value; r.extra = extra;
    if (rectangle)
        r.rectangle = *rectangle;
    rc = desk_object_call(&r, result);
    if (operation == AES_OBJECT_EDIT || operation == AES_OBJECT_CENTER)
        desk_tree_read(snapshot);
    if (snapshot == &local)
        desk_tree_destroy(snapshot);
    return rc;
}

void desk_trees_exit(void)
{
    desk_tree_destroy(&retained[0]);
    desk_tree_destroy(&retained[1]);
}

#if CONF_WITH_X32_DESKTOP_TEST
/* A near-capacity screen tree with shared 32x32 icon assets. The old
 * serializer overflowed its byte budget well before this object count. */
void desk_tree_test(void)
{
    static BOOL done;
    static WORD mask[64], data[64];
    ICONBLK icon;
    OBJECT *tree;
    DESK_TREE snapshot;
    AES_OBJECT_REQUEST request = { 0 };
    AES_OBJECT_RESULT result;
    WORD i;
    BOOL ok = FALSE;

    if (done)
        return;
    done = TRUE;
    tree = (void *)Malloc(1901 * sizeof(*tree));
    if ((intptr_t)tree <= 0) {
        desk_test_record("x32 desktop shared icon snapshot FAIL\r\n");
        return;
    }
    memset(tree, 0, 1901 * sizeof(*tree));
    memset(&icon, 0, sizeof(icon));
    for (i = 0; i < 64; i++) {
        mask[i] = 0xffff;
        data[i] = 0xaaaa;
    }
    icon.ib_pmask = mask; icon.ib_pdata = data;
    icon.ib_ptext = "LONGNAME.INF";
    icon.ib_wicon = icon.ib_hicon = 32;
    icon.ib_ytext = 32; icon.ib_wtext = 96; icon.ib_htext = gl_hchar;
    tree[0].ob_next = -1;
    tree[0].ob_head = 1; tree[0].ob_tail = 1900;
    tree[0].ob_type = G_IBOX;
    tree[0].ob_width = gl_width; tree[0].ob_height = gl_height;
    for (i = 1; i <= 1900; i++) {
        tree[i].ob_next = i == 1900 ? 0 : i + 1;
        tree[i].ob_head = tree[i].ob_tail = -1;
        tree[i].ob_type = G_ICON;
        tree[i].ob_flags = SELECTABLE;
        tree[i].ob_spec.iconblk = &icon;
        tree[i].ob_x = 4; tree[i].ob_y = gl_hbox + 4;
        tree[i].ob_width = 96; tree[i].ob_height = 32 + gl_hchar;
    }
    tree[1900].ob_flags |= LASTOB;
    if (desk_tree_create(&snapshot, tree)) {
        request.tree = snapshot.handle;
        request.operation = AES_OBJECT_FIND;
        request.value = AES_TREE_MAX_DEPTH;
        request.rectangle.g_x = 5; request.rectangle.g_y = gl_hbox + 5;
        ok = desk_object_call(&request, &result) == E_OK && result.value == 1900;
        request.operation = AES_OBJECT_DRAW_TREE;
        request.object = 1900; request.value = 0;
        /* Keep the test drawing outside the icon; no visible desktop damage. */
        r_set(&request.rectangle, 0, 0, 1, 1);
        ok = desk_object_call(&request, &result) == E_OK && ok;
        desk_tree_destroy(&snapshot);
    }
    Mfree(tree);
    desk_test_record(ok ? "x32 desktop shared icon snapshot PASS\r\n" :
                          "x32 desktop shared icon snapshot FAIL\r\n");
}
#endif
