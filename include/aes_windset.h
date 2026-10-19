/* Native WIND_SET pointer fields, in units of WORDs.  The classic 32-bit
 * layout remains pointer[2], scalar[2]; LP64 uses pointer[4], scalar[2].
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later.
 */
#ifndef AES_WINDSET_H
#define AES_WINDSET_H

#include "portab.h"

#define AES_WINDSET_PTR_WORDS (sizeof(void *) / sizeof(WORD))
#define AES_WINDSET_PTR_NINTIN (2 + AES_WINDSET_PTR_WORDS + 2)

#endif
