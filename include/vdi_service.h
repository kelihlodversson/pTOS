/*
 * vdi_service.h - typed supervisor services used by the x32 VDI client
 *
 * Copyright (C) 2026 The pTOS development team.
 * This file is distributed under the GPL, version 2 or any later version.
 */
#ifndef VDI_SERVICE_H
#define VDI_SERVICE_H

#include "config.h"
#include "vdi_user.h"
#include "bdosdefs.h"

enum { VDI_CALLBACK_TIMER, VDI_CALLBACK_BUTTON, VDI_CALLBACK_MOTION,
       VDI_CALLBACK_CURSOR, VDI_CALLBACK_WHEEL, VDI_CALLBACK_COUNT };

#define VDI_SERVICE_CLASS 0x100UL
#define VDI_SERVICE_OPEN  0
#define VDI_SERVICE_CLOSE 1
#define VDI_SERVICE_LINE  2
#define VDI_SERVICE_CLIP  3
#define VDI_SERVICE_COLOR 4
#define VDI_SERVICE_CHECK 5
#define VDI_SERVICE_VECTOR 6
#define VDI_SERVICE_PIXEL 7
#define VDI_SERVICE_DRAW 8
#define VDI_SERVICE_SCREEN_COPY 9
#define VDI_SERVICE_MAX_TEXT 256
#define VDI_SERVICE_MAX_POINTS 256
#define VDI_SERVICE_MAX_BITMAP (1024UL * 1024UL)

typedef struct {
    WORD handle;
    WORD intout[45];
    WORD ptsout[12];
} VDI_SERVICE_WORKSTATION;

/* Caller-local descriptor snapshot. This request never crosses the
 * supervisor boundary; the client executes memory rasters in ring 3. */
typedef struct {
    WORD handle, operation, mode, foreground, background, reserved;
    WORD xy[8];
    VDI_USER_FDB source, destination;
} VDI_SERVICE_RASTER_REQUEST;

typedef struct {
    ULONG old_handler;
    WORD tick_ms, reserved;
} VDI_SERVICE_VECTOR_RESULT;

/* Pointer-free drawing request. Counts are determined by the allowlisted
 * operation, not trusted as native VDI array bounds. */
typedef struct {
    WORD code, subcode, nintin, nptsin;
    WORD intin[VDI_SERVICE_MAX_TEXT];
    WORD ptsin[4];
} VDI_SERVICE_DRAW_REQUEST;

typedef struct {
    WORD nintout, nptsout;
    WORD intout[45], ptsout[12];
} VDI_SERVICE_DRAW_RESULT;

typedef struct {
    WORD mode, reserved;
    WORD xy[8];
} VDI_SERVICE_SCREEN_REQUEST;

typedef char vdi_draw_request_size[(sizeof(VDI_SERVICE_DRAW_REQUEST) == 528) ? 1 : -1];
typedef char vdi_draw_result_size[(sizeof(VDI_SERVICE_DRAW_RESULT) == 118) ? 1 : -1];
typedef char vdi_screen_request_size[(sizeof(VDI_SERVICE_SCREEN_REQUEST) == 20) ? 1 : -1];

LONG vdi_user_draw_shape(const VDI_SERVICE_DRAW_REQUEST *request,
                         WORD *nintout, WORD *nptsout);

typedef char vdi_service_workstation_size[(sizeof(VDI_SERVICE_WORKSTATION) == 116) ? 1 : -1];
typedef char vdi_service_vector_size[(sizeof(VDI_SERVICE_VECTOR_RESULT) == 8) ? 1 : -1];

/* Caller-local memory raster operations: shape checks values and computes
 * spans; execute only touches the separate, validated bitmap snapshots. */
LONG vdi_user_raster_shape(VDI_SERVICE_RASTER_REQUEST *request,
                           ULONG *source_bytes, ULONG *destination_bytes);
void vdi_user_raster_execute(const VDI_SERVICE_RASTER_REQUEST *request,
                             const UBYTE *source, UBYTE *destination);

#if CONF_WITH_X32_VDI && !defined(__ILP32__)
LONG vdi_service(ULONG function, UQUAD a, UQUAD b, UQUAD c, UQUAD d);
void vdi_service_process_exit(PD *pd);
void vdi_service_physical_state(BOOL opened);
#if CONF_WITH_CUIS
BOOL vdi_service_preserve_console(void);
void vdi_service_prepare_physical_open(void);
#if CONF_WITH_X86_64_MEMTEST
BOOL vdi_service_test_reset(void);
BOOL vdi_service_test_reopen(void);
void vdi_service_test_watch(volatile ULONG *pixel);
BOOL vdi_service_test_preserved(void);
#endif
#endif
#if CONF_WITH_USER_CALLBACKS
void vdi_service_callback_work(void);
void vdi_service_callback_event(UWORD signature, LONG a, LONG b);
#endif
#endif

#endif
