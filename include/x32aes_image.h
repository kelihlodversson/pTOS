/* Private user-code window; never an address constant for applications. */
#ifndef X32AES_IMAGE_H
#define X32AES_IMAGE_H
#include "aes_client_exports.h"
#define X32_AES_IMAGE_BASE 0x0ffd0000
#define X32_AES_IMAGE_SIZE 0x00010000
#define X32_AES_IMAGE_MAGIC 0x41455331
#define X32_AES_IMAGE_HEADER (8 + 4 * AES_CLIENT_EXPORT_COUNT)
#ifndef __ASSEMBLER__
#include "config.h"
#include "portab.h"
#if CONF_WITH_X32_AES
#include "procmem.h"
BOOL x32_aes_image_load(ASPACE *as);
ULONG x32_aes_image_entry(const char *name);
#endif
#endif
#endif
