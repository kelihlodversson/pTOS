/*
 * launch_args.h - kernel-owned launch arguments
 *
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef LAUNCH_ARGS_H
#define LAUNCH_ARGS_H

#include "portab.h"
#include "pexecve.h"
#include "../lib/ldso/startup.h"

typedef struct pexecve_args {
    const char *path;
    const char *const *argv;
    const char *const *envp;
    ULONG argc;
    ULONG envc;
    const unsigned char *classic_tail;
    const unsigned char *guest_tail;
    const char *guest_environment;
    ULONG guest_environment_size;
} PEXECVE_ARGS;

typedef struct {
    ULONG mode, path, argv, envp, tail, environment;
} PEXECVE_GUEST_REQUEST;

/* Copy a bounded string, or a legacy double-NUL environment. On success
 * 'used', when supplied, includes the terminator. The destination may have
 * been partially written on error. */
LONG launch_copy_user_string(UQUAD address, char *buffer, ULONG capacity,
                             BOOL environment, ULONG *used);

/* Capture into an empty output handle. The syscall boundary must keep the
 * caller's execution live through all checked copies and own the resulting
 * allocation until launch consumes it. Release it before a no-return handoff.
 * The view and both vectors remain valid until the owner releases the handle. */
LONG pexecve_args_capture(WORD mode, UQUAD path, UQUAD argv, UQUAD envp,
                          PEXECVE_ARGS **out);
LONG pexecve_guest_args_capture(const PEXECVE_GUEST_REQUEST *request, PEXECVE_ARGS **out);
void pexecve_args_release(PEXECVE_ARGS **args);

static inline DL_START_VECTORS pexecve_start_vectors(const PEXECVE_ARGS *args)
{
    DL_START_VECTORS vectors = {0};

    vectors.argv = args->argv;
    vectors.envp = args->envp;
    vectors.argc = args->argc;
    vectors.envc = args->envc;
    vectors.classic_tail = args->classic_tail;
    vectors.guest_tail = args->guest_tail;
    vectors.guest_environment = args->guest_environment;
    vectors.guest_environment_size = args->guest_environment_size;
    return vectors;
}

#endif
