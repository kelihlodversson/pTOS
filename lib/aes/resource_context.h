/* Private caller-local resource bindings and ownership. */
#ifndef LIBAES_RESOURCE_CONTEXT_H
#define LIBAES_RESOURCE_CONTEXT_H
#include "libaes.h"
#include "gemrslib.h"
#include "layout.h"
#include "resource_state.h"
typedef struct resource_context {
    struct resource_context *next;
    AES_RESOURCE_STATE *owner;
    RSHDR *image;
    OBJECT **trees;
    UWORD length;
    RSHDR header;
    LAYOUT_METRICS metrics;
    BOOL caller_owned;
} RESOURCE_CONTEXT;
WORD aes_resource_read(RESOURCE_CONTEXT *aes_resource, AESGLOBAL *global, UWORD handle);
void aes_resource_fix(RESOURCE_CONTEXT *aes_resource);
void *aes_resource_addr(RESOURCE_CONTEXT *aes_resource, UWORD type, UWORD index);
void *aes_resource_sub(RESOURCE_CONTEXT *aes_resource, UWORD index, UWORD offset, UWORD size);
void *aes_resource_alloc(LONG bytes, AES_RESOURCE_STATE *owner);
/* Failed discard stays with the allocating context, including after its
 * native session has closed. Discard each allocation only once. */
void *aes_temporary_alloc(LONG bytes, AESGLOBAL *global);
WORD aes_resource_release(void *memory);
WORD aes_resource_discard(void *memory);
LONG aes_resource_read_file(WORD handle, LONG bytes, void *memory);
LONG aes_resource_seek(WORD handle, WORD mode, LONG offset);
CICONBLK **aes_resource_cicons(RSHDR *header);
BOOL aes_resources_reset(AESGLOBAL *global);
#endif
