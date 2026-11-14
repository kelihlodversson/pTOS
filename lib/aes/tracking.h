/* Private adapter for the shared native GEM tracking core. */
#ifndef LIBAES_TRACKING_H
#define LIBAES_TRACKING_H
#include "libaes.h"
#include "render_context.h"
#define AES_TRACK_RUBBER 0
#define AES_TRACK_DRAG 1
#define AES_TRACK_SLIDE 2
typedef struct aes_track {
    WORD kind, vertical, mouse[4], result1, result2;
    GRECT box, bounds;
    OBJECT tree[2];
    AESGLOBAL *global;
} AES_TRACK;
BOOL aes_graphics_track(AES_TRACK *track);
BOOL aes_track_wait(PAINTER *aes_painter);
void aes_track_setup(PAINTER *aes_painter);
void aes_track_parent(PAINTER *aes_painter, OBJECT *tree, WORD object, GRECT *rect);
void aes_track_child(OBJECT *tree, WORD object, GRECT *rect);
#endif
