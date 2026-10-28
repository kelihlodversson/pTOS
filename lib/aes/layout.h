/* Private workstation metrics shared by caller-local GEM form adapters. */
#ifndef LIBAES_LAYOUT_H
#define LIBAES_LAYOUT_H
#include "portab.h"
typedef struct {
    WORD width, height, wchar, hchar, hbox;
} LAYOUT_METRICS;
BOOL aes_layout_metrics(LAYOUT_METRICS *metrics, WORD *global);
BOOL aes_frame_metrics(WORD *wbox, WORD *hbox, WORD *global);
#endif
