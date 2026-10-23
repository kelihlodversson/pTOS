/* Validate copied resource bytes before constructing native AES objects.
 * This file is also exercised by the host contract tests.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later.
 */
#include "aes_objects.h"
#include "gemerror.h"

static BOOL span(ULONG at, ULONG size, ULONG bytes, ULONG alignment)
{
    return at && !(at & (alignment - 1)) && at <= bytes && size <= bytes - at;
}

static BOOL string(const UBYTE *blob, ULONG bytes, ULONG at, ULONG capacity,
                    BOOL nonempty)
{
    ULONG i;

    if (!capacity || capacity > AES_TREE_MAX_TEXT || !span(at, capacity, bytes, 1))
        return FALSE;
    for (i = 0; i < capacity; i++)
        if (!blob[at + i])
            return !nonempty || i != 0;
    return FALSE;
}

static BOOL icon(const UBYTE *blob, ULONG bytes, const AES_OBJECT_ICON *p)
{
    ULONG size;

    if (p->reserved || p->character_x < -512 || p->character_x > 512 ||
        p->character_y < -512 || p->character_y > 512 || p->icon.g_w <= 0 || p->icon.g_w > 512 ||
        (p->icon.g_w & 15) || p->icon.g_h <= 0 || p->icon.g_h > 512 ||
        p->icon.g_x < -512 || p->icon.g_x > 512 ||
        p->icon.g_y < -512 || p->icon.g_y > 512 ||
        p->label.g_x < -512 || p->label.g_x > 512 ||
        p->label.g_y < -512 || p->label.g_y > 512 ||
        p->label.g_w < 0 || p->label.g_w > 4096 ||
        p->label.g_h < 0 || p->label.g_h > 256)
        return FALSE;
    size = (ULONG)p->icon.g_w / 8 * p->icon.g_h;
    return span(p->mask, size, bytes, 2) && span(p->data, size, bytes, 2) &&
           span(p->text, sizeof(AES_OBJECT_STRING), bytes, 4) &&
           string(blob, bytes, ((const AES_OBJECT_STRING *)(blob + p->text))->text,
                   ((const AES_OBJECT_STRING *)(blob + p->text))->capacity, FALSE);
}

LONG aes_tree_validate(const void *data, ULONG bytes, WORD *parents)
{
    const UBYTE *blob = data;
    const AES_TREE_HEADER *header = data;
    const AES_OBJECT *objects;
    const AES_OBJECT *p;
    ULONG i, n, j, limit;
    WORD child, previous, at;
    LONG x, y;

    if (bytes < sizeof(*header) || bytes > AES_TREE_MAX_BYTES ||
        header->version != AES_TREE_VERSION || header->bytes != bytes ||
        header->reserved || header->flags || !header->count ||
        header->count > AES_TREE_MAX_OBJECTS)
        return EINVAL;
    n = header->count;
    if (n * sizeof(*objects) > bytes - sizeof(*header))
        return EINVAL;
    objects = (const AES_OBJECT *)(header + 1);
    for (i = 0; i < n; i++)
        parents[i] = -1;
    for (i = 0; i < n; i++) {
        p = &objects[i];
        if (p->next < -1 || p->next >= (LONG)n || p->head < -1 ||
            p->head >= (LONG)n || p->tail < -1 || p->tail >= (LONG)n ||
            (p->flags & ~0x06ff) || (p->state & ~0x007f) ||
            (p->rectangle.g_w < 0 || p->rectangle.g_w > 10000) ||
            (p->rectangle.g_h < 0 || p->rectangle.g_h > 10000) ||
            ((p->head == -1) != (p->tail == -1)))
            return EINVAL;
        child = p->head;
        previous = -1;
        limit = 0;
        while (child != -1 && child != (WORD)i) {
            if (!child || ++limit > n || parents[child] != -1)
                return EINVAL;
            parents[child] = i;
            previous = child;
            child = objects[child].next;
            if (child < 0 || child >= (LONG)n)
                return EINVAL;
        }
        if ((p->head != -1 && (child != (WORD)i || previous != p->tail)))
            return EINVAL;
    }
    if (objects[0].next != -1 || parents[0] != -1)
        return EINVAL;
    for (i = 0; i < n; i++) {
        p = &objects[i];
        x = y = 0;
        at = i;
        for (j = 0; at != -1 && j <= AES_TREE_MAX_DEPTH; j++) {
            x += objects[at].rectangle.g_x;
            y += objects[at].rectangle.g_y;
            at = parents[at];
        }
        if (at != -1 || x < -10000 || x > 10000 || y < -10000 || y > 10000 ||
            x + p->rectangle.g_w > 20000 || y + p->rectangle.g_h > 20000)
            return EINVAL;
        switch (p->type & 0xff) {
        case 20: case 25: case 27: {     /* box, invisible box, box character */
            LONG thickness = (int8_t)(p->spec >> 16);

            if (thickness < -16 || thickness > 16)
                return EINVAL;
            break;
        }
        case 26: case 28: case 32: {    /* button, string, title */
            const AES_OBJECT_STRING *s;

            if (!span(p->spec, sizeof(*s), bytes, 4))
                return EINVAL;
            s = (const AES_OBJECT_STRING *)(blob + p->spec);
            if (!string(blob, bytes, s->text, s->capacity, FALSE))
                return EINVAL;
            break;
        }
        case 21: case 22: case 29: case 30: {
            const AES_OBJECT_TEXT *t;
            ULONG length = 0, slots = 0;

            if (!span(p->spec, sizeof(*t), bytes, 4))
                return EINVAL;
            t = (const AES_OBJECT_TEXT *)(blob + p->spec);
            if ((t->font != 3 && t->font != 5) || t->justification < 0 ||
                t->justification > 2 || t->thickness < -16 || t->thickness > 16 ||
                !string(blob, bytes, t->text, t->text_capacity, FALSE) ||
                !string(blob, bytes, t->template, t->template_capacity, FALSE) ||
                !string(blob, bytes, t->validation, t->validation_capacity, TRUE))
                return EINVAL;
            while (blob[t->text + length])
                length++;
            for (j = 0; blob[t->template + j]; j++)
                if (blob[t->template + j] == '_')
                    slots++;
            if ((p->flags & 8) && (slots < (ULONG)t->text_capacity - 1 || length > slots))
                return EINVAL;
            break;
        }
        case 23: {                      /* image */
            const AES_OBJECT_IMAGE *image;

            if (!span(p->spec, sizeof(*image), bytes, 4))
                return EINVAL;
            image = (const AES_OBJECT_IMAGE *)(blob + p->spec);
            if (image->reserved || image->row_bytes <= 0 || (image->row_bytes & 1) ||
                image->row_bytes > 128 || image->height <= 0 || image->height > 512 ||
                image->x || image->y || image->colour < 0 ||
                image->colour > 255 ||
                !span(image->data, (ULONG)image->row_bytes * image->height, bytes, 2))
                return EINVAL;
            break;
        }
        case 24:
            if (!span(p->spec, sizeof(AES_OBJECT_USERDEF), bytes, 4) || (p->flags & 8))
                return EINVAL;
            break;
        case 31: case 33: {
            const AES_OBJECT_ICON *mono;
            ULONG next = 0, size;

            if (!span(p->spec, (p->type & 0xff) == 33 ? sizeof(AES_OBJECT_CICON) : sizeof(*mono), bytes, 4))
                return EINVAL;
            mono = (const AES_OBJECT_ICON *)(blob + p->spec);
            if (!icon(blob, bytes, mono))
                return EINVAL;
            size = (ULONG)mono->icon.g_w / 8 * mono->icon.g_h;
            if ((p->type & 0xff) == 33)
                next = ((const AES_OBJECT_CICON *)mono)->colours;
            for (j = 0; next && j < 8; j++) {
                const AES_OBJECT_COLOUR *c;

                if (!span(next, sizeof(*c), bytes, 4))
                    return EINVAL;
                c = (const AES_OBJECT_COLOUR *)(blob + next);
                if (c->planes < 1 || c->planes > 32 || c->reserved ||
                    !span(c->data, size * c->planes, bytes, 2) ||
                    !span(c->mask, size, bytes, 2) ||
                    (!!c->selected_data != !!c->selected_mask) ||
                    (c->selected_data && (!span(c->selected_data, size * c->planes, bytes, 2) ||
                                          !span(c->selected_mask, size, bytes, 2))))
                    return EINVAL;
                next = c->next;
            }
            if (next)
                return EINVAL;
            break;
        }
        default:
            return EINVFN;
        }
        if ((p->flags & 8) && (p->type != 29 && p->type != 30))
            return EINVAL;
    }
    return E_OK;
}
