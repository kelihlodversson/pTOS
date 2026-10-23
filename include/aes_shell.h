/* Bounded shell state and display coordination for associated x32 clients.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#ifndef AES_SHELL_H
#define AES_SHELL_H
#include "aes_client.h"
enum {
    AES_SHELL_WRITE = 1, AES_SHELL_GET, AES_SHELL_PUT,
    AES_SHELL_WDEF, AES_SHELL_DCLICK, AES_SHELL_DAMAGE, AES_SHELL_KEY
};
#define AES_SHELL_PATH_MAX 128
#define AES_SHELL_TAIL_BYTES 128
#define AES_SHELL_BUFFER_MAX 4192
typedef struct {
    UWORD operation;
    WORD value, extra, reserved;
    ULONG data, second, bytes;
    GRECT rectangle;
} AES_SHELL_REQUEST;
LONG aes_shell(const AES_SHELL_REQUEST *request);
typedef char aes_shell_request_size[(sizeof(AES_SHELL_REQUEST) == 28) ? 1 : -1];
#endif
