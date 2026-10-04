/*
 * x32probe_image.c - the ring-3 probe program of the boot self-test
 *
 * Embeds obj/x32/x32probe.elf (tests/x32_probe/) for memtest.c to launch.
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "x32embed.h"

X32_EMBED_IMAGE(x86_64_x32probe_image, x86_64_x32probe_elf, "obj/x32/x32probe.elf")
