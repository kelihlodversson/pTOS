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
static const uint32_t *client_aux;
static void (*handlers[EXIT_SLOTS])(void);
static uint32_t count;
static int exiting;
static void (*loader_fini)(void);

static const uint32_t *find_auxiliary_vector(char **envp)
{
    uint32_t i;
    for (i = 0; i < 16384 && envp[i]; i++) ;
    if (i == 16384) _Exit(127);
    return (const uint32_t *)(envp + i + 1);
}

static uint32_t auxiliary_value(const uint32_t *aux, uint32_t type)
{
    uint32_t i;
    for (i = 0; i < 32 && aux[i*2]; i++)
        if (aux[i*2] == type) return aux[i*2+1];
    if (i == 32) _Exit(127);
    return 0;
}

/* Called from the executable's first preinitializer, before DT_INIT. */
int __ptos_is_application(char **envp)
{
    uint32_t type = auxiliary_value(find_auxiliary_vector(envp), DL_AT_PTOS_ENTRY_TYPE);
    if (type > DL_ENTRY_ACCESSORY) _Exit(127);
    return type == DL_ENTRY_PROGRAM;
}

/* DT_INIT receives the same vectors as other ELF initializers. This makes
 * getenv/environ available before dependent libraries' constructors run. */
void __ptos_client_init(uint32_t argc, char **argv, char **envp)
{
    client_argc = argc;
    client_argv = argv;
    environ = envp;
    client_aux = find_auxiliary_vector(envp);
    ptos_basepage = auxiliary_value(client_aux, DL_AT_PTOS_BASEPAGE);
    if (!ptos_basepage) _Exit(127);
}

unsigned long __ptos_getauxval(unsigned long type)
{
    return client_aux ? auxiliary_value(client_aux, type) : 0;
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
