/*
 * argv_legacy.h - checked conversion of legacy launch arguments
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ARGV_LEGACY_H
#define ARGV_LEGACY_H

#include "../lib/ldso/startup.h"

#define LEGACY_START_ENV_MAX 32768U
/* One slot per minimally sized environment/payload string, plus the
 * classic tail's program name and up to 63 single-token arguments. */
#define LEGACY_START_SLOTS(envsize) ((envsize) / 2 + 64 + 2)

typedef struct {
    DL_START_VECTORS vectors;
    char text[128];
} LEGACY_START_ARGS;

/* Decode the current GEMDOS-to-ELF compatibility input. The view borrows
 * filename, environment and caller workspace until they are released.
 * Short tails use the ELF compatibility grammar (spaces/tabs, single and
 * double quotes). Extended ARGV can name empty arguments via NULL indices. */
int legacy_start_vectors(LEGACY_START_ARGS *out, const char *filename,
                         const unsigned char *tail, uint32_t tailsize,
                         const unsigned char *env, uint32_t envsize,
                         const char **slots, uint32_t capacity);

#endif
