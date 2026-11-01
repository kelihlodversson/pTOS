/* Native x32 AES clients: no AESPB or writable shared AES globals.
 * Copyright (C) 2026 The pTOS development team.
 * This file is distributed under the GPL, version 2 or any later version.
 */
#ifndef AES_CLIENT_H
#define AES_CLIENT_H
#include "portab.h"
#include "grect.h"
#include "rectfunc.h"

#define AES_CLIENT_VERSION 0x0100
#define AES_NAME_MAX 8
#define AES_MESSAGE_QUEUE 8
#define AES_EVENT_KEYBOARD 0x0001
#define AES_EVENT_BUTTON 0x0002
#define AES_EVENT_MOUSE1 0x0004
#define AES_EVENT_MOUSE2 0x0008
#define AES_EVENT_MESSAGE 0x0010
#define AES_EVENT_TIMER 0x0020
#define AES_EVENT_ALL 0x003f
#define AES_EVENT_POLL 1
#define AES_END_UPDATE 0
#define AES_BEG_UPDATE 1
#define AES_END_MCTRL 2
#define AES_BEG_MCTRL 3
#define AES_FEATURE_MESSAGE 1UL
#define AES_FEATURE_UPDATE 2UL
#define AES_FEATURE_WAIT 4UL
#define AES_FEATURE_INPUT 8UL
#define AES_FEATURE_WINDOW 16UL
#define AES_FEATURE_OBJECT 32UL
#define AES_WINDOW_TEXT_MAX 127
#define AES_WINDOW_OPEN 1
#define AES_WINDOW_NAME 0
#define AES_WINDOW_INFO_LINE 1
#define AES_WINDOW_HSLIDE 0
#define AES_WINDOW_VSLIDE 1
#define AES_WINDOW_HSLSIZ 2
#define AES_WINDOW_VSLSIZ 3
#define AES_WINDOW_BORDER 0
#define AES_WINDOW_WORK 1
/* Window gadgets retain their native AES bit assignments. */
#define AES_WINDOW_NAME_GADGET 0x0001
#define AES_WINDOW_CLOSER 0x0002
#define AES_WINDOW_FULLER 0x0004
#define AES_WINDOW_MOVER 0x0008
#define AES_WINDOW_INFO_GADGET 0x0010
#define AES_WINDOW_SIZER 0x0020
#define AES_WINDOW_UPARROW 0x0040
#define AES_WINDOW_DNARROW 0x0080
#define AES_WINDOW_VSLIDE_GADGET 0x0100
#define AES_WINDOW_LFARROW 0x0200
#define AES_WINDOW_RTARROW 0x0400
#define AES_WINDOW_HSLIDE_GADGET 0x0800

typedef struct {
    UWORD version;
    WORD id;
    UWORD queue_capacity;
    UWORD reserved;
    ULONG features;
} AES_APP_INFO;

/* Distinct from GEMlib's AES_MESSAGE capability-selector macro. */
typedef struct {
    WORD type;
    WORD sender;                 /* overwritten by the kernel on send */
    WORD reserved;               /* zero: no pointer or extended payload */
    WORD data[5];                /* opaque values, never kernel pointers */
} AES_CLIENT_MESSAGE;

typedef struct {
    UWORD events;
    UWORD reserved;
    AES_CLIENT_MESSAGE message;
} AES_EVENT;

typedef struct {
    WORD outside;               /* 0: enter rectangle; 1: leave rectangle */
    GRECT rectangle;
} AES_EVENT_RECT;

typedef struct {
    UWORD mask, flags;           /* AES_EVENT_*; flags: 0 (wait) or POLL */
    UWORD clicks, button_mask, button_state, reserved;
    AES_EVENT_RECT mouse1, mouse2;
    ULONG timer_ms;              /* 0: immediate, maximum 0x7fffffff */
} AES_EVENT_REQUEST;

typedef struct {
    UWORD events, reserved;
    WORD mouse_x, mouse_y;
    UWORD buttons, modifiers, key, clicks;
    AES_CLIENT_MESSAGE message;
} AES_INPUT_EVENT;

typedef struct {
    WORD handle, owner;
    UWORD kind, flags;
    GRECT full, work, current, previous;
    WORD hslide, vslide, hslsiz, vslsiz, top, reserved;
} AES_WINDOW_INFO;

LONG aes_appl_init(const char *name, AES_APP_INFO *info);
LONG aes_appl_exit(void);
LONG aes_appl_find(const char *name);
LONG aes_appl_write(WORD recipient, const AES_CLIENT_MESSAGE *message);
LONG aes_appl_read(AES_CLIENT_MESSAGE *message);
LONG aes_evnt_poll(UWORD mask, AES_EVENT *event);
LONG aes_wind_update(WORD code);
/* Cooperative waits/yields are available to loaded AES accessories and
 * ordinary graphical processes launched by the AES shell.
 * WAIT accepts AES_EVENT_MESSAGE; delivery copies to the original caller. */
LONG aes_evnt_wait(UWORD mask, AES_EVENT *event);
LONG aes_appl_yield(void);
LONG aes_evnt_multi(const AES_EVENT_REQUEST *request, AES_INPUT_EVENT *event);
LONG aes_wind_create(UWORD kind, const GRECT *full);
LONG aes_wind_open(WORD handle, const GRECT *rectangle);
LONG aes_wind_close(WORD handle);
LONG aes_wind_delete(WORD handle);
LONG aes_wind_get(WORD handle, AES_WINDOW_INFO *info);
LONG aes_wind_set_rect(WORD handle, const GRECT *rectangle);
LONG aes_wind_set_text(WORD handle, UWORD field, const char *text);
LONG aes_wind_top(WORD handle);
LONG aes_wind_slider(WORD handle, UWORD field, WORD value);
LONG aes_wind_rect(WORD handle, UWORD next, GRECT *rectangle);
LONG aes_wind_calc(UWORD mode, UWORD kind, const GRECT *input, GRECT *output);
LONG aes_wind_find(WORD x, WORD y);

#define AES_ASSERT(name, expr) typedef char aes_assert_##name[(expr) ? 1 : -1]
AES_ASSERT(info_size, sizeof(AES_APP_INFO) == 12);
AES_ASSERT(message_size, sizeof(AES_CLIENT_MESSAGE) == 16);
AES_ASSERT(event_size, sizeof(AES_EVENT) == 20);
AES_ASSERT(info_features, offsetof(AES_APP_INFO, features) == 8);
AES_ASSERT(message_data, offsetof(AES_CLIENT_MESSAGE, data) == 6);
AES_ASSERT(event_message, offsetof(AES_EVENT, message) == 4);
AES_ASSERT(rect_size_event, sizeof(AES_EVENT_RECT) == 10);
AES_ASSERT(request_size, sizeof(AES_EVENT_REQUEST) == 36);
AES_ASSERT(request_timer, offsetof(AES_EVENT_REQUEST, timer_ms) == 32);
AES_ASSERT(input_size, sizeof(AES_INPUT_EVENT) == 32);
AES_ASSERT(input_message, offsetof(AES_INPUT_EVENT, message) == 16);
AES_ASSERT(window_size, sizeof(AES_WINDOW_INFO) == 52);
AES_ASSERT(window_work, offsetof(AES_WINDOW_INFO, work) == 16);
AES_ASSERT(window_top, offsetof(AES_WINDOW_INFO, top) == 48);
AES_ASSERT(window_align, __alignof__(AES_WINDOW_INFO) == 2);
AES_ASSERT(rect_size, sizeof(GRECT) == 8);
AES_ASSERT(info_align, __alignof__(AES_APP_INFO) == 4);
AES_ASSERT(message_align, __alignof__(AES_CLIENT_MESSAGE) == 2);
AES_ASSERT(event_align, __alignof__(AES_EVENT) == 2);
AES_ASSERT(rect_align, __alignof__(GRECT) == 2);
#undef AES_ASSERT
#endif
