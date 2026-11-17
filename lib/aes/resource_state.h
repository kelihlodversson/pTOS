/* Resource ownership and failed-cleanup retries for one AES context. */
#ifndef LIBAES_RESOURCE_STATE_H
#define LIBAES_RESOURCE_STATE_H
#include "emutos.h"
typedef struct {
    struct resource_context *resources, *retired_resources;
    union resource_allocation *discarded_allocations;
    LONG pending_close;
    BOOL close_pending;
} AES_RESOURCE_STATE;
#endif
