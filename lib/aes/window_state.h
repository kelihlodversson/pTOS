/* Retained window metadata and control state for one userspace AES context. */
#ifndef LIBAES_WINDOW_STATE_H
#define LIBAES_WINDOW_STATE_H
#include "libaes.h"
#include "../../aes/shared/gemslidegeom.h"
#define GEM_WINDOW_LIMIT 64
struct gem_window {
    short id;
    WORD top_colours[NUM_ELEM], bottom_colours[NUM_ELEM];
    UWORD kind;
    char *name, *info;
    LONG native, drag_x, drag_y, arrow_tick;
    BOOL open, control_selected;
    WORD control, control_message, control_action;
    WORD hslide, vslide, hslsiz, vslsiz;
    WM_SLIDER_GEOMETRY drag_slider;
    GRECT control_rectangle;
    struct visible_region *regions;
    GRECT current, previous, full;
};
typedef struct {
    struct gem_window entries[GEM_WINDOW_LIMIT];
    BOOL frame_notice_blocked, frame_display_blocked;
    ULONG frame_overlays, frame_dialogs;
    BOOL control_display_blocked, control_pipe_blocked;
} AES_WINDOW_STATE;
#endif
