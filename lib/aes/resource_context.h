/* Private caller-local resource bindings and ownership. */
#ifndef LIBAES_RESOURCE_CONTEXT_H
#define LIBAES_RESOURCE_CONTEXT_H
#include "gem.h"
#include "gemrslib.h"
#include "layout.h"
typedef struct resource_context {
    struct resource_context *next;
    AESGLOBAL native;
    RSHDR header;
    LAYOUT_METRICS metrics;
    BOOL caller_owned;
} RESOURCE_CONTEXT;
extern RESOURCE_CONTEXT *aes_resource;
WORD aes_resource_read(AESGLOBAL *global, UWORD handle);
void aes_resource_fix(void);
void *aes_resource_addr(UWORD type, UWORD index);
void *aes_resource_sub(UWORD index, UWORD offset, UWORD size);
void *aes_resource_alloc(LONG bytes);
void *aes_temporary_alloc(LONG bytes);
WORD aes_resource_release(void *memory);
WORD aes_resource_discard(void *memory);
LONG aes_resource_read_file(WORD handle, LONG bytes, void *memory);
LONG aes_resource_seek(WORD handle, WORD mode, LONG offset);
CICONBLK **aes_resource_cicons(RSHDR *header);
void aes_resources_init(void);
void aes_resources_reset(void);
#endif
