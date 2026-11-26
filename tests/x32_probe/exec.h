/*
 * Vector launch for freestanding x32 test executables.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef X32_PROBE_EXEC_H
#define X32_PROBE_EXEC_H

#include <stdint.h>
#include "../../include/pexecve.h"

static inline long x32_exec_vectors(short mode, const char *path,
                                    const char *const *argv,
                                    const char *const *envp)
{
    register uint64_t code __asm__("rax") = (1ULL << 32) | GEMDOS_PEXECVE;
    register uint64_t environment __asm__("r10") = (uintptr_t)envp;

    __asm__ volatile ("syscall" : "+a"(code)
                      : "D"((long long)mode), "S"((uint64_t)(uintptr_t)path),
                        "d"((uint64_t)(uintptr_t)argv), "r"(environment)
                      : "rcx", "r11", "memory", "cc");
    return (long)code;
}

static inline long x32_exec_argument(short mode, const char *path,
                                     const char *argument, const char *const *envp)
{
    const char *const argv[] = { path, argument, 0 };
    const char *const empty[] = { 0 };

    return x32_exec_vectors(mode, path, argv, envp ? envp : empty);
}

#endif
