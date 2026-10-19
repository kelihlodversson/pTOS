/*
 * vdi_image.c - private, read-only x32 VDI code for imported application calls
 *
 * Copyright (C) 2026 The pTOS development team.
 * This file is distributed under the GPL, version 2 or any later version.
 */
#include "config.h"
#include "portab.h"
#include "x32embed.h"
#include "x32vdi_image.h"

static const PROG_IMAGE *vdi_client_image(void);

X32_EMBED_IMAGE(vdi_client_image, vdi_client_blob, "obj/x32/vdi_client.bin")

ULONG x32_vdi_image_entry(void)
{
    const PROG_IMAGE *image = vdi_client_image();
    ULONG entry;

    if (image->size < sizeof(ULONG) || image->size > X32_VDI_IMAGE_SIZE)
        return 0;
    entry = *(const ULONG *)image->data;
    if (entry < X32_VDI_IMAGE_BASE + sizeof(ULONG) ||
        entry >= X32_VDI_IMAGE_BASE + image->size)
        return 0;
    return entry;
}

BOOL x32_vdi_image_load(ASPACE *as)
{
    const PROG_IMAGE *image = vdi_client_image();

    return x32_vdi_image_entry() &&
        aspace_load_private(as, X32_VDI_IMAGE_BASE, image->size,
                            ASPACE_PROT_USER | ASPACE_PROT_EXEC,
                            image->data, image->size);
}
