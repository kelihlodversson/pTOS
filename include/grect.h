/* Shared rectangle representation for kernel and native client libraries.
 * Copyright (C) 2026 The pTOS development team.
 * This file is distributed under the GPL, version 2 or any later version.
 */
#ifndef GRECT_H
#define GRECT_H
#include "portab.h"
typedef struct {
    WORD g_x, g_y, g_w, g_h;
} GRECT;
#endif
