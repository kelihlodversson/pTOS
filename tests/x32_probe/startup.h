/*
 * Standard ELF entry fields used by the freestanding x32 probes.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef X32_PROBE_STARTUP_H
#define X32_PROBE_STARTUP_H

#include "../../lib/ldso/startup.h"

static inline unsigned long x32_probe_basepage(const uint32_t *stack)
{
    const uint32_t *envp = stack + *stack + 2;
    const uint32_t *aux;

    while (*envp)
        envp++;
    for (aux = envp + 1; aux[0]; aux += 2)
        if (aux[0] == DL_AT_PTOS_BASEPAGE)
            return aux[1];
    return 0;
}

#endif
