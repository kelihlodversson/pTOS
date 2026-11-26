/* Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef EXEC_PATH_H
#define EXEC_PATH_H
#include "fs.h"

/* Qualify against the caller's drive and per-drive directory. The caller
 * owns out and retains any hard disk error for cleanup or retry. */
LONG exec_qualify_path(const char *path, char *out, ULONG capacity, DISK_ERROR *error);
#endif
