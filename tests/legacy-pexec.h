/*
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version. See doc/license.txt for details.
 */
#ifndef __ASSEMBLER__
#include <mint/osbind.h>

/* These probes still exercise the kernel's legacy call, including staged
 * operations, error returns and workers using the basepage entry ABI. */
#undef Pexec
#define Pexec(mode, path, tail, env) \
    trap_1_wwlll((short)0x4b, (short)(mode), (long)(path), (long)(tail), (long)(env))
#endif
