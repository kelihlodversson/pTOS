/* Private native frame drawing over CUIS-owned visible regions. */
#ifndef LIBAES_FRAMES_H
#define LIBAES_FRAMES_H
#include "gemframe.h"
BOOL aes_frame_prepare(WM_FRAME_STATE *state, UWORD kind, const GRECT *border,
                       char *name, char *info, WORD *global);
BOOL aes_frame_paint(LONG native, const WM_FRAME_STATE *state, WORD *global);
BOOL aes_frame_control_inside(const WM_FRAME_STATE *state, WORD object, LONG x, LONG y);
WORD aes_frame_control(const WM_FRAME_STATE *state, LONG x, LONG y);
BOOL aes_frame_paint_control(LONG native, const WM_FRAME_STATE *state, WORD object, BOOL selected, WORD *global);
BOOL aes_frame_move(LONG native, const WM_FRAME_STATE *state, LONG press_x, LONG press_y, GRECT *result, WORD *global);
#endif
