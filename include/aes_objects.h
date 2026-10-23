/* Pointer-free, native-byte-order x32 AES resource snapshots.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later.
 */
#ifndef AES_OBJECTS_H
#define AES_OBJECTS_H
#include "aes_client.h"

#define AES_TREE_VERSION 1
#define AES_TREE_MAX_BYTES (256UL * 1024)
#define AES_TREE_MAX_OBJECTS 2048
/* Capacities include the terminator. Native AES intin has 128 WORDs;
 * TED scratch strings have 81 bytes. ob_edit also terminates its expanded
 * validation at template_capacity, requiring one additional byte there. */
#define AES_TREE_MAX_TEXT 128
#define AES_TREE_MAX_TED_TEXT 81
#define AES_TREE_MAX_EDIT_TEMPLATE 80
#define AES_TREE_MAX_DEPTH 8

/* Every offset is relative to the start of the blob. There are no process
 * pointers except USERDEF.handler, which is an explicitly checked callback. */
typedef struct {
    ULONG version, bytes;
    UWORD count, reserved;
    ULONG flags;
} AES_TREE_HEADER;

typedef struct {
    WORD next, head, tail;
    UWORD type, flags, state;
    ULONG spec;
    GRECT rectangle;
} AES_OBJECT;

typedef struct { ULONG text, capacity; } AES_OBJECT_STRING;
typedef struct {
    ULONG text, template, validation, validation_capacity;
    WORD font, justification, colour, thickness, text_capacity, template_capacity;
} AES_OBJECT_TEXT;
typedef struct {
    ULONG mask, data, text;
    WORD character, character_x, character_y;
    GRECT icon, label;
    UWORD reserved;
} AES_OBJECT_ICON;
typedef struct {
    ULONG data;
    WORD row_bytes, height, x, y, colour, reserved;
} AES_OBJECT_IMAGE;
typedef struct { ULONG handler, parameter; } AES_OBJECT_USERDEF;
typedef struct {
    WORD planes, reserved;
    ULONG data, mask, selected_data, selected_mask, next;
} AES_OBJECT_COLOUR;
typedef struct { AES_OBJECT_ICON mono; ULONG colours; } AES_OBJECT_CICON;

/* USERDEF receives only values on its own user stack; neither a native
 * tree nor a supervisor parameter-block address is exposed. */
typedef struct {
    ULONG tree, parameter;
    WORD object;
    UWORD previous, current, reserved;
    GRECT rectangle, clip;
} AES_OBJECT_DRAW;
typedef WORD AES_OBJECT_CALLBACK(const AES_OBJECT_DRAW *draw);

enum {
    AES_OBJECT_CREATE = 1, AES_OBJECT_DESTROY, AES_OBJECT_READ,
    AES_OBJECT_DRAW_TREE, AES_OBJECT_FIND, AES_OBJECT_OFFSET,
    AES_OBJECT_STATE, AES_OBJECT_RECTANGLE, AES_OBJECT_SET_TEXT,
    AES_OBJECT_EDIT, AES_OBJECT_CENTER, AES_OBJECT_KEY,
    AES_OBJECT_MENU, AES_OBJECT_DESKTOP, AES_OBJECT_FORM,
    AES_OBJECT_GET, AES_OBJECT_MOUSE, AES_OBJECT_MOUSE_STATE, AES_OBJECT_METRICS
};
/* operation determines the meaning of value/extra, rectangle and data.
 * Unused fields must be zero. READ writes bytes into data. CREATE and TEXT
 * copy bytes from data. Result never contains a native pointer. */
typedef struct {
    ULONG tree;
    UWORD operation, object;
    WORD value, extra;
    GRECT rectangle;
    ULONG data, bytes;
} AES_OBJECT_REQUEST;
typedef struct {
    LONG value;
    GRECT rectangle;
    WORD key, index;
    AES_OBJECT object;
} AES_OBJECT_RESULT;

/* Native object type/flag/state numbers are the familiar GEM values.
 * A formatted text template must have room for every editable character.
 * This helper requires count WORDs of caller-provided scratch storage. */
LONG aes_tree_validate(const void *blob, ULONG bytes, WORD *parents);
LONG aes_object(const AES_OBJECT_REQUEST *request, AES_OBJECT_RESULT *result);
/* Client-side modal loop: no native form continuation survives a user wait. */
LONG aes_form_do(ULONG tree, WORD start);

typedef char aes_object_size[(sizeof(AES_OBJECT) == 24) ? 1 : -1];
typedef char aes_object_request_size[(sizeof(AES_OBJECT_REQUEST) == 28) ? 1 : -1];
typedef char aes_object_result_size[(sizeof(AES_OBJECT_RESULT) == 40) ? 1 : -1];
typedef char aes_object_draw_size[(sizeof(AES_OBJECT_DRAW) == 32) ? 1 : -1];
typedef char aes_object_icon_size[(sizeof(AES_OBJECT_ICON) == 36) ? 1 : -1];
#endif
