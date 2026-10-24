/*
 * Standard ELF client startup and normal exit, separate from legacy libcmini.
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "ptos.h"
#include "../runtime.h"

#define EXIT_SLOTS 32

char **environ;
uint32_t ptos_basepage;
static uint32_t client_argc;
static char **client_argv;
static void (*handlers[EXIT_SLOTS])(void);
static uint32_t count;
static int exiting;
static void (*loader_fini)(void);

/* DT_INIT receives the same vectors as other ELF initializers. This makes
 * getenv/environ available before dependent libraries' constructors run. */
void __ptos_client_init(uint32_t argc, char **argv, char **envp)
{
    const uint32_t *aux;
    uint32_t i;
    client_argc = argc;
    client_argv = argv;
    environ = envp;
    for (i = 0; i < 16384 && envp[i]; i++) ;
    if (i == 16384) _Exit(127);
    aux = (const uint32_t *)(envp + i + 1);
    for (i = 0; i < 32 && aux[0]; i++, aux += 2)
        if (aux[0] == DL_AT_PTOS_BASEPAGE) ptos_basepage = aux[1];
    if (i == 32 || !ptos_basepage) _Exit(127);
}

int32_t ptos_write(int handle, const void *buffer, uint32_t length)
{
    return (int32_t)dl_syscall(0x40, handle, length, (uintptr_t)buffer, 0, 0, 0);
}

int atexit(void (*function)(void))
{
    if (!function || count == EXIT_SLOTS) return -1;
    handlers[count++] = function;
    return 0;
}

void _Exit(int status)
{
    dl_syscall(0x4c, (uint32_t)status, 0, 0, 0, 0, 0);
    for (;;) ;
}

void exit(int status)
{
    if (exiting) _Exit(status);
    exiting = 1;
    /* Pop before calling: callbacks may register another callback. */
    while (count) handlers[--count]();
    if (loader_fini) loader_fini();
    while (count) handlers[--count]();
    _Exit(status);
}

void __ptos_start(const uint32_t *stack, void (*finalizer)(void),
                    int (*main_function)(int, char **, char **))
{
    if (!stack || !finalizer || !main_function || stack[0] != client_argc ||
        (const void *)(stack + 1) != (const void *)client_argv) _Exit(127);
    loader_fini = finalizer;
    exit(main_function((int)client_argc, client_argv, environ));
}
