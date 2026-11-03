/* Private adapter for the shared native GEM tracking core. */
#ifndef LIBAES_TRACKING_H
#define LIBAES_TRACKING_H
#include "gem.h"
#define AES_TRACK_RUBBER 0
#define AES_TRACK_DRAG 1
#define AES_TRACK_SLIDE 2
typedef struct {
    WORD kind, vertical, mouse[4], result1, result2;
    GRECT box, bounds;
    OBJECT tree[2];
    WORD *global;
} AES_TRACK;
extern AES_TRACK *aes_tracker;
BOOL aes_graphics_track(AES_TRACK *track);
BOOL aes_track_wait(void);
void aes_track_setup(void);
void aes_track_parent(OBJECT *tree, WORD object, GRECT *rect);
void aes_track_child(OBJECT *tree, WORD object, GRECT *rect);
#endif
