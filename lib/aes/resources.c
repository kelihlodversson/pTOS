/* Standard GEM resources in application memory, using the shared RSC loader.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "libaes.h"
#include "gemerror.h"
#include "transport.h"
#include "application.h"
#include "app_context.h"
#include "resource_context.h"
#include "rsload.h"
#include "gemrcfix.h"
#include "gemlayout.h"
#include "string.h"
#include "launch.h"

extern char *getenv(const char *name);
#define RESOURCE_PATH DL_LAUNCH_PATH
RESOURCE_CONTEXT *aes_resource;
/* Reserve cleanup linkage before allocation can need a failed-free retry.
 * The union keeps returned storage aligned for pointers and 64-bit fields. */
typedef union resource_allocation {
    struct {
        union resource_allocation *next;
        AES_RESOURCE_STATE *owner;
    } cleanup;
    UQUAD alignment;
} RESOURCE_ALLOCATION;

static void *allocate(LONG bytes, AES_RESOURCE_STATE *owner)
{
    UQUAD address;
    RESOURCE_ALLOCATION *allocation;
    if (bytes <= 0 || bytes > 1024L*1024L) return NULL;
    address = aes_memory_call(0x48,(ULONG)bytes+sizeof(*allocation));
    if ((QUAD)address <= 0) return NULL;
    allocation = (RESOURCE_ALLOCATION *)(uintptr_t)address;
    allocation->cleanup.owner = owner;
    return allocation+1;
}
void *aes_temporary_alloc(LONG bytes, AESGLOBAL *global)
{
    return allocate(bytes,&aes_app_context(global)->resources);
}
void *aes_resource_alloc(LONG bytes, AES_RESOURCE_STATE *owner)
{
    return bytes > 65535 ? NULL : allocate(bytes,owner);
}
WORD aes_resource_release(void *memory)
{
    return (WORD)aes_memory_call(0x49,(uintptr_t)((RESOURCE_ALLOCATION *)memory-1));
}
WORD aes_resource_discard(void *memory)
{
    WORD result = aes_resource_release(memory);
    if (result != E_OK) {
        RESOURCE_ALLOCATION *allocation = (RESOURCE_ALLOCATION *)memory-1;
        AES_RESOURCE_STATE *owner = allocation->cleanup.owner;
        allocation->cleanup.next = owner->discarded_allocations;
        owner->discarded_allocations = allocation;
    }
    return result;
}
/* Retired ownership is never considered by selected(). Keep the record
 * until both the image and the record itself have actually been released. */
static BOOL release_resource(RESOURCE_CONTEXT *resource)
{
    if (resource->image && !resource->caller_owned) {
        if (aes_resource_release(resource->image) != E_OK) return FALSE;
        resource->image = NULL;
    }
    return aes_resource_release(resource) == E_OK;
}
static void retire_resource(RESOURCE_CONTEXT *resource)
{
    resource->next = resource->owner->retired_resources;
    resource->owner->retired_resources = resource;
}
static void retry_resource_releases(AES_RESOURCE_STATE *owner)
{
    RESOURCE_CONTEXT **link = &owner->retired_resources, *resource, *next;
    RESOURCE_ALLOCATION **allocation = &owner->discarded_allocations, *block, *following;
    while ((block = *allocation) != NULL) {
        following = block->cleanup.next;
        if (aes_resource_release(block+1) == E_OK) *allocation = following;
        else allocation = &block->cleanup.next;
    }
    while ((resource = *link) != NULL) {
        next = resource->next;
        if (release_resource(resource)) *link = next;
        else link = &resource->next;
    }
}
static void dispose_resource(RESOURCE_CONTEXT *resource)
{
    if (!release_resource(resource)) retire_resource(resource);
}

LONG aes_resource_read_file(WORD handle, LONG bytes, void *memory)
{
    return (LONG)aes_gemdos_call(0x3f,(UWORD)handle,(ULONG)bytes,(uintptr_t)memory);
}
LONG aes_resource_seek(WORD handle, WORD mode, LONG offset)
{
    return (LONG)aes_gemdos_call(0x42,(QUAD)offset,(UWORD)handle,(UWORD)mode);
}
/* A failed Fclose may leave its handle live, or may already have released
 * it. Retry immediately and accept EIHNDL as closed. The owning context must
 * resolve a persistently failing close before opening another resource file;
 * another context neither inherits nor retries this pending close. */
static BOOL retry_resource_close(AES_RESOURCE_STATE *owner)
{
    LONG result;
    if (!owner->close_pending) return TRUE;
    result = (LONG)aes_gemdos_call(0x3e,(UWORD)owner->pending_close,0,0);
    if (result != E_OK && result != EIHNDL) {
        /* A close can release the descriptor and still report a flush error.
         * Probe before returning control to a caller that could reopen it.
         * Only EIHNDL proves release; other seek errors retain ownership. */
        if (aes_resource_seek((WORD)owner->pending_close,1,0) != EIHNDL) return FALSE;
    }
    owner->close_pending = FALSE;
    return TRUE;
}
static BOOL close_resource_file(AES_RESOURCE_STATE *owner)
{
    LONG result = (LONG)aes_gemdos_call(0x3e,(UWORD)owner->pending_close,0,0);
    if (result == E_OK || result == EIHNDL) owner->close_pending = FALSE;
    else retry_resource_close(owner);
    /* Recovery must not turn an unsuccessful load into a successful one. */
    return result == E_OK;
}

CICONBLK **aes_resource_cicons(RSHDR *header)
{
    LONG *extension;
    if (!(header->rsh_vrsn & NEW_FORMAT_RSC)) return NULL;
    extension = (LONG *)((char *)header+header->rsh_rssize);
    return extension[1] > 0 ? (CICONBLK **)((char *)header+extension[1]) : NULL;
}

static LONG open_joined(const char *directory, size_t length, const char *name)
{
    char path[RESOURCE_PATH];
    size_t n = strlen(name), i;
    BOOL separator = length && directory[length-1] != '\\' && directory[length-1] != '/' && directory[length-1] != ':';
    if (length+n+separator >= sizeof(path)) return EPTHNF;
    for (i = 0; i < length; i++) path[i] = directory[i] == '/' ? '\\' : directory[i];
    if (separator) path[length++] = '\\';
    memcpy(path+length,name,n+1);
    return (LONG)aes_gemdos_call(0x3d,(uintptr_t)path,0,0);
}
static LONG open_resource(const char *name, AESGLOBAL *global)
{
    const char *p, *base = name, *path;
    const char *application_directory = aes_shell_state(global)->directory;
    LONG handle;
    size_t n = 0;
    while (n < RESOURCE_PATH && name[n]) n++;
    if (!n || n == RESOURCE_PATH) return EFILNF;
    for (p = name; *p; p++) if (*p == '\\' || *p == '/' || *p == ':') base = p+1;
    if (application_directory[0]) {
        handle = open_joined(application_directory,strlen(application_directory),base);
        if (handle >= 0) return handle;
    }
    handle = open_joined("",0,name);
    if (handle >= 0) return handle;
    handle = open_joined("\\",1,base);
    if (handle >= 0) return handle;
    path = getenv("PATH");
    while (path && *path) {
        for (p = path; *p && *p != ';' && *p != ','; p++) ;
        handle = open_joined(path,p-path,base);
        if (handle >= 0) return handle;
        if (!*p) break;
        path = p+1;
    }
    return EFILNF;
}

/* The caller selects resources through its public typed fields. */
static void publish(AESGLOBAL *global, RESOURCE_CONTEXT *resource)
{
    global->ap_ptree = resource ? resource->trees : NULL;
    global->ap_rscmem = resource ? resource->image : NULL;
    global->ap_rsclen = resource ? resource->length : 0;
}
static RESOURCE_CONTEXT **selected(AESGLOBAL *global, BOOL tree_table)
{
    RESOURCE_CONTEXT **link;
    for (link = &aes_app_context(global)->resources.resources; *link; link = &(*link)->next)
        if (tree_table ? (*link)->trees == global->ap_ptree :
            (*link)->image == global->ap_rscmem) break;
    return link;
}

/* Validate reachable sibling rings before any normal AES traversal can loop
 * on a malformed disk tree. Duplicate tree-table entries share one preflight. */
static BOOL valid_trees(RESOURCE_CONTEXT *resource, BOOL offsets)
{
    typedef struct { WORD object, next, last; } FRAME;
    RSHDR *header = resource->image;
    OBJECT *objects = (OBJECT *)((char *)header+header->rsh_object), *tree;
    FRAME *stack;
    UBYTE *visited, *validated;
    LONG i, root, count, depth;
    WORD node, child;
    BOOL ok = FALSE;
    if (!header->rsh_ntree) return TRUE; /* Free strings/images need no tree. */
    if (header->rsh_nobs <= 0) return FALSE;
    stack = aes_resource_alloc((LONG)header->rsh_nobs*(sizeof(FRAME)+2),resource->owner);
    if (!stack) return FALSE;
    visited = (UBYTE *)(stack+header->rsh_nobs); validated = visited+header->rsh_nobs;
    memset(validated,0,header->rsh_nobs);
    for (i = 0; i < header->rsh_ntree; i++) {
        tree = resource->trees[i];
        if (offsets) tree = (OBJECT *)((char *)header+(uintptr_t)tree);
        root = tree-objects;
        if (root < 0 || root >= header->rsh_nobs) goto done;
        if (validated[root]) continue;
        for (count = 1; root+count <= header->rsh_nobs; count++)
            if (tree[count-1].ob_flags & LASTOB) break;
        if (root+count > header->rsh_nobs) goto done;
        memset(visited,0,count); visited[0] = 1;
        depth = 0; stack[0].object = 0; stack[0].next = tree[0].ob_head; stack[0].last = NIL;
        while (depth >= 0) {
            node = stack[depth].object; child = stack[depth].next;
            if (child == NIL) {
                if (tree[node].ob_head != NIL || tree[node].ob_tail != NIL) goto done;
                depth--; continue;
            }
            if (child == node) {
                if (stack[depth].last == NIL || stack[depth].last != tree[node].ob_tail) goto done;
                depth--; continue;
            }
            if (child < 0 || child >= count || visited[child]) goto done;
            visited[child] = 1;
            stack[depth].next = tree[child].ob_next; stack[depth].last = child;
            depth++;
            stack[depth].object = child; stack[depth].next = tree[child].ob_head; stack[depth].last = NIL;
        }
        validated[root] = 1;
    }
    ok = TRUE;
done:
    aes_resource_discard(stack);
    return ok;
}
WORD mt_rsrc_load(const char *name, AESGLOBAL *global)
{
    RESOURCE_CONTEXT *resource, *saved = aes_resource;
    AES_RESOURCE_STATE *owner;
    LONG handle, session;
    AESGLOBAL loaded = { 0 };
    BOOL ok;
    if (!name) return 0;
    session = aes_application_session(global);
    if (session < 0) return 0;
    owner = &aes_app_context(global)->resources;
    if (!retry_resource_close(owner)) return 0;
    retry_resource_releases(owner);
    resource = aes_resource_alloc(sizeof(*resource),owner);
    if (!resource) return 0;
    memset(resource,0,sizeof(*resource));
    resource->owner = owner;
    if (!aes_layout_metrics(&resource->metrics,global)) { dispose_resource(resource); return 0; }
    handle = open_resource(name,global);
    if (handle < 0) { dispose_resource(resource); return 0; }
    owner->pending_close = handle; owner->close_pending = TRUE;
    aes_resource = resource;
    ok = aes_resource_read(&loaded,(UWORD)handle) != 0;
    resource->image = loaded.ap_rscmem;
    resource->trees = loaded.ap_ptree;
    resource->length = loaded.ap_rsclen;
    if (!close_resource_file(owner)) ok = FALSE;
    if (ok) { resource->header = *resource->image; ok = valid_trees(resource,FALSE); }
    if (ok && aes_application_session(global) != session) ok = FALSE;
    if (ok) {
        aes_resource_fix();
        resource->next = owner->resources; owner->resources = resource;
        publish(global,resource);
    } else {
        dispose_resource(resource);
    }
    aes_resource = saved;
    return ok ? 1 : 0;
}


WORD mt_rsrc_rcfix(void *image, AESGLOBAL *global)
{
    RSHDR *header = image, *staged;
    UBYTE *roles;
    RESOURCE_CONTEXT *resource, *p;
    AES_RESOURCE_STATE *owner;
    OBJECT *objects;
    LONG session;
    WORD i;
    BOOL ok;
    if (!header || (uintptr_t)header % __alignof__(void *)) return 0;
    session = aes_application_session(global);
    if (session < 0 || header->rsh_rssize < sizeof(*header)) return 0;
    owner = &aes_app_context(global)->resources;
    /* Fixing a live image twice must never relocate pointers or scale twice.
     * GEM callers release the selected resource before installing rcfix data. */
    if (*selected(global,FALSE)) return 0;
    for (p = owner->resources; p; p = p->next)
        if (p->image == image) return 0;
    retry_resource_releases(owner);
    resource = aes_resource_alloc(sizeof(*resource),owner);
    if (!resource) return 0;
    memset(resource,0,sizeof(*resource));
    resource->owner = owner;
    resource->caller_owned = TRUE;
    resource->image = image;
    resource->length = header->rsh_rssize;
    resource->trees = (OBJECT **)((char *)image+header->rsh_trindex);
    resource->header = *header;
    if (!aes_layout_metrics(&resource->metrics,global)) { dispose_resource(resource); return 0; }
    staged = aes_resource_alloc(header->rsh_rssize,owner);
    if (!staged) { dispose_resource(resource); return 0; }
    roles = aes_resource_alloc(header->rsh_rssize,owner);
    if (!roles) {
        aes_resource_discard(staged); dispose_resource(resource); return 0;
    }
    memcpy(staged,image,header->rsh_rssize);
    ok = rs_rcfix_native(header,staged,roles) && valid_trees(resource,TRUE);
    if (ok && aes_application_session(global) != session) ok = FALSE;
    if (ok) {
        objects = (OBJECT *)((char *)staged+staged->rsh_object);
        for (i = 0; i < staged->rsh_nobs; i++)
            rs_obfix_metrics(objects,i,resource->metrics.wchar,resource->metrics.hchar,resource->metrics.width);
        /* All fallible work precedes the in-place commit. Pointer targets
         * already refer to caller storage, never to the temporary copy. */
        memcpy(image,staged,header->rsh_rssize);
        resource->next = owner->resources; owner->resources = resource;
        publish(global,resource);
    } else dispose_resource(resource);
    aes_resource_discard(roles);
    aes_resource_discard(staged);
    return ok ? 1 : 0;
}


WORD mt_rsrc_free(AESGLOBAL *global)
{
    RESOURCE_CONTEXT **link, *resource;
    if (aes_application_session(global) < 0) return 0;
    link = selected(global,FALSE); resource = *link;
    if (!resource || (!resource->caller_owned &&
        aes_resource_release(resource->image) != E_OK)) return 0;
    *link = resource->next;
    resource->image = NULL;
    dispose_resource(resource);
    publish(global,NULL);
    return 1;
}

BOOL aes_resources_reset(AESGLOBAL *global)
{
    AES_RESOURCE_STATE *owner = &aes_app_context(global)->resources;
    RESOURCE_CONTEXT *next;
    retry_resource_close(owner);
    while (owner->resources) {
        next = owner->resources->next;
        retire_resource(owner->resources);
        owner->resources = next;
    }
    retry_resource_releases(owner);
    return !owner->close_pending && !owner->retired_resources && !owner->discarded_allocations;
}
static BOOL address_index(const RSHDR *header, WORD type, WORD index)
{
    WORD count;
    switch (type) {
    case R_TREE: count = header->rsh_ntree; break;
    case R_OBJECT: case R_OBSPEC: count = header->rsh_nobs; break;
    case R_TEDINFO: case R_TEPTEXT: case R_TEPTMPLT: case R_TEPVALID: count = header->rsh_nted; break;
    case R_ICONBLK: case R_IBPMASK: case R_IBPDATA: case R_IBPTEXT: count = header->rsh_nib; break;
    case R_BITBLK: case R_BIPDATA: count = header->rsh_nbb; break;
    case R_STRING: case R_FRSTR: count = header->rsh_nstring; break;
    case R_IMAGEDATA: case R_FRIMG: count = header->rsh_nimages; break;
    default: return FALSE;
    }
    return index >= 0 && index < count;
}
WORD mt_rsrc_gaddr(WORD type, WORD index, void *address, AESGLOBAL *global)
{
    RESOURCE_CONTEXT *resource, *saved = aes_resource;
    void *value = (void *)-1L;
    if (!address) return 0;
    if (aes_application_session(global) >= 0) {
        resource = *selected(global,type == R_TREE);
        if (resource && !memcmp(resource->image,&resource->header,sizeof(resource->header)) &&
            address_index(&resource->header,type,index)) {
            aes_resource = resource;
            value = aes_resource_addr(type,index);
            aes_resource = saved;
        }
    }
    memcpy(address,&value,sizeof(value));
    return value != (void *)-1L;
}

WORD mt_rsrc_saddr(WORD type, WORD index, void *address, AESGLOBAL *global)
{
    void *slot;
    if (!mt_rsrc_gaddr(type,index,&slot,global) || !slot) return 0;
    memcpy(slot,&address,sizeof(address));
    return 1;
}
