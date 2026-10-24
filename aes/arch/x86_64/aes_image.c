/* Private executable client pages, with an explicit typed export table.
 * Copyright (C) 2026 The pTOS development team.
 * This file is distributed under the GPL, version 2 or any later version.
 */
#include "config.h"
#include "portab.h"
#include "string.h"
#include "x32embed.h"
#include "x32aes_image.h"

static const PROG_IMAGE *aes_client_image(void);
X32_EMBED_IMAGE(aes_client_image, aes_client_blob, "obj/x32/aes_client.bin")

static BOOL valid_image(const PROG_IMAGE *image)
{
    const ULONG *header = (const ULONG *)image->data;
    ULONG i;

    if (image->size < X32_AES_IMAGE_HEADER || image->size > X32_AES_IMAGE_SIZE ||
        header[0] != X32_AES_IMAGE_MAGIC || header[1] != AES_CLIENT_EXPORT_COUNT)
        return FALSE;
    for (i = 0; i < AES_CLIENT_EXPORT_COUNT; i++)
        if (header[2+i] < X32_AES_IMAGE_BASE + X32_AES_IMAGE_HEADER ||
            header[2+i] >= X32_AES_IMAGE_BASE + image->size)
            return FALSE;
    return TRUE;
}

ULONG x32_aes_image_entry(const char *name)
{
#define AES_NAME(index, symbol) #symbol,
    static const char *const names[] = { AES_CLIENT_EXPORTS(AES_NAME) };
#undef AES_NAME
    const PROG_IMAGE *image = aes_client_image();
    ULONG i;

    if (!valid_image(image))
        return 0;
    for (i = 0; i < AES_CLIENT_EXPORT_COUNT; i++)
        if (!strcmp(name, names[i]))
            return ((const ULONG *)image->data)[2+i];
    return 0;
}

BOOL x32_aes_image_load(ASPACE *as)
{
    const PROG_IMAGE *image = aes_client_image();

    return valid_image(image) &&
        aspace_load_private(as, X32_AES_IMAGE_BASE, image->size,
                            ASPACE_PROT_USER | ASPACE_PROT_EXEC,
                            image->data, image->size);
}
