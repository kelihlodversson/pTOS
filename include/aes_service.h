/* Kernel-private boundary for native x32 AES services.
 * Copyright (C) 2026 The pTOS development team.
 * This file is distributed under the GPL, version 2 or any later version.
 */
#ifndef AES_SERVICE_H
#define AES_SERVICE_H
#include "aes_client.h"
#define AES_SERVICE_CLASS 0x101
#define AES_SERVICE_INIT 0
#define AES_SERVICE_EXIT 1
#define AES_SERVICE_FIND 2
#define AES_SERVICE_WRITE 3
#define AES_SERVICE_READ 4
#define AES_SERVICE_POLL 5
#define AES_SERVICE_UPDATE 6
#if defined(ARCH_X86_64) && !defined(__ILP32__)
#include "bdosdefs.h"
typedef struct aes_client_state AES_CLIENT_STATE;
LONG aes_service(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d);
void aes_service_process_exit(PD *pd);
#endif
#endif
