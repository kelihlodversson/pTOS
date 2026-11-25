/*
 * pexecve.h - pTOS vector launch ABI constants
 *
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef PEXECVE_H
#define PEXECVE_H

/* pTOS extension beyond the FreeMiNT table and pTOS's 0x190-0x192 calls. */
#define GEMDOS_PEXECVE      0x193
#define PEXECVE_PATH_MAX    1024UL
#define PEXECVE_ARG_MAX     65536UL
#define PEXECVE_MAX_ENTRIES 1024UL

#endif
