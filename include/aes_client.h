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
#define AES_EVENT_MESSAGE 0x0010
#define AES_END_UPDATE 0
#define AES_BEG_UPDATE 1
#define AES_END_MCTRL 2
#define AES_BEG_MCTRL 3
#define AES_FEATURE_MESSAGE 1UL
#define AES_FEATURE_UPDATE 2UL

typedef struct {
    UWORD version;
    WORD id;
    UWORD queue_capacity;
    UWORD reserved;
    ULONG features;
} AES_APP_INFO;

typedef struct {
    WORD type;
    WORD sender;                 /* overwritten by the kernel on send */
    WORD reserved;               /* zero: no pointer or extended payload */
    WORD data[5];                /* opaque values, never kernel pointers */
} AES_MESSAGE;

typedef struct {
    UWORD events;
    UWORD reserved;
    AES_MESSAGE message;
} AES_EVENT;

LONG aes_appl_init(const char *name, AES_APP_INFO *info);
LONG aes_appl_exit(void);
LONG aes_appl_find(const char *name);
LONG aes_appl_write(WORD recipient, const AES_MESSAGE *message);
LONG aes_appl_read(AES_MESSAGE *message);
LONG aes_evnt_poll(UWORD mask, AES_EVENT *event);
LONG aes_wind_update(WORD code);

#define AES_ASSERT(name, expr) typedef char aes_assert_##name[(expr) ? 1 : -1]
AES_ASSERT(info_size, sizeof(AES_APP_INFO) == 12);
AES_ASSERT(message_size, sizeof(AES_MESSAGE) == 16);
AES_ASSERT(event_size, sizeof(AES_EVENT) == 20);
AES_ASSERT(info_features, offsetof(AES_APP_INFO, features) == 8);
AES_ASSERT(message_data, offsetof(AES_MESSAGE, data) == 6);
AES_ASSERT(event_message, offsetof(AES_EVENT, message) == 4);
AES_ASSERT(rect_size, sizeof(GRECT) == 8);
#undef AES_ASSERT
#endif
