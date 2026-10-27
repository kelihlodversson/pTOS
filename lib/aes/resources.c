/* Standard GEM resources in application memory, using the shared RSC loader.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gem.h"
#include "gemerror.h"
#include "transport.h"
#include "application.h"
#include "resource_context.h"
#include "rsload.h"
#include "gemrcfix.h"
#include "gemlayout.h"
#include "string.h"
#include "ptos.h"

extern char *getenv(const char *name);
#define RESOURCE_PATH 128
RESOURCE_CONTEXT *aes_resource;
static RESOURCE_CONTEXT *resources;
static RESOURCE_CONTEXT *retired_resources;
/* Reserve cleanup linkage before allocation can need a failed-free retry.
 * The union keeps returned storage aligned for pointers and 64-bit fields. */
typedef union resource_allocation {
    union resource_allocation *next;
    UQUAD alignment;
} RESOURCE_ALLOCATION;
static RESOURCE_ALLOCATION *discarded_allocations;
static LONG pending_close = -1;
static char application_directory[RESOURCE_PATH];

void *aes_resource_alloc(LONG bytes)
{
    UQUAD address;
    RESOURCE_ALLOCATION *allocation;
    if (bytes <= 0 || bytes > 65535) return NULL;
    address = aes_memory_call(0x48,(ULONG)bytes+sizeof(*allocation));
    if ((QUAD)address <= 0) return NULL;
    allocation = (RESOURCE_ALLOCATION *)(uintptr_t)address;
    return allocation+1;
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
        allocation->next = discarded_allocations;
        discarded_allocations = allocation;
    }
    return result;
}
/* Retired ownership is never considered by selected(). Keep the record
 * until both the image and the record itself have actually been released. */
static BOOL release_resource(RESOURCE_CONTEXT *resource)
{
    if (resource->native.ap_rscmem && !resource->caller_owned) {
        if (aes_resource_release(resource->native.ap_rscmem) != E_OK) return FALSE;
        resource->native.ap_rscmem = NULL;
    }
    return aes_resource_release(resource) == E_OK;
}
static void retire_resource(RESOURCE_CONTEXT *resource)
{
    resource->next = retired_resources;
    retired_resources = resource;
}
static void retry_resource_releases(void)
{
    RESOURCE_CONTEXT **link = &retired_resources, *resource, *next;
    RESOURCE_ALLOCATION **allocation = &discarded_allocations, *block, *following;
    while ((block = *allocation) != NULL) {
        following = block->next;
        if (aes_resource_release(block+1) == E_OK) *allocation = following;
        else allocation = &block->next;
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
 * it. Retry without opening anything in between, and accept EIHNDL as closed.
 * Keep a persistently failing handle for the next load/session cleanup rather
 * than accumulating more open files on repeated failed loads. */
static BOOL retry_resource_close(void)
{
    LONG result;
    if (pending_close < 0) return TRUE;
    result = (LONG)aes_gemdos_call(0x3e,(UWORD)pending_close,0,0);
    if (result != E_OK && result != EIHNDL) return FALSE;
    pending_close = -1;
    return TRUE;
}
static BOOL close_resource_file(void)
{
    LONG result = (LONG)aes_gemdos_call(0x3e,(UWORD)pending_close,0,0);
    if (result == E_OK || result == EIHNDL) pending_close = -1;
    else retry_resource_close();
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

/* Capture the launch directory before application code can change cwd.
 * AT_EXECFN contains the actual supplied filename, independent of ARGV[0]. */
void aes_resources_init(void)
{
    const char *name = (const char *)(uintptr_t)__ptos_getauxval(31), *p, *base;
    char cwd[RESOURCE_PATH], directory[RESOURCE_PATH];
    LONG drive;
    size_t prefix, length, i, at = 0;
    application_directory[0] = 0;
    if (!name) return;
    length = strlen(name);
    if (length >= RESOURCE_PATH) return;
    base = name;
    for (p = name; *p; p++) if (*p == '\\' || *p == '/' || *p == ':') base = p+1;
    prefix = base-name;
    drive = (LONG)aes_gemdos_call(0x19,0,0,0);
    if (length >= 2 && name[1] == ':') {
        drive = (name[0] >= 'a' && name[0] <= 'z' ? name[0]-'a' : name[0]-'A');
        name += 2; prefix = prefix >= 2 ? prefix-2 : 0;
    }
    if (drive < 0 || drive > 25) return;
    directory[at++] = 'A'+drive; directory[at++] = ':';
    if (*name != '\\' && *name != '/') {
        if ((LONG)aes_gemdos_call(0x47,(uintptr_t)cwd,drive+1,0) != E_OK) return;
        length = strlen(cwd);
        if (length+at+prefix+2 >= RESOURCE_PATH) return;
        if (!length || (cwd[0] != '\\' && cwd[0] != '/')) directory[at++] = '\\';
        for (i = 0; i < length; i++) directory[at++] = cwd[i] == '/' ? '\\' : cwd[i];
        if (directory[at-1] != '\\') directory[at++] = '\\';
    }
    if (at+prefix >= RESOURCE_PATH) return;
    for (i = 0; i < prefix; i++) directory[at++] = name[i] == '/' ? '\\' : name[i];
    directory[at] = 0;
    memcpy(application_directory,directory,at+1);
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
static LONG open_resource(const char *name)
{
    const char *p, *base = name, *path;
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

/* Resource global pointers are the standard two-WORD, 32-bit GEM fields. */
static ULONG global_pointer(const WORD *global, WORD offset)
{
    ULONG value;
    memcpy(&value,global+offset,sizeof(value));
    return value;
}
static void publish(WORD *global, RESOURCE_CONTEXT *resource)
{
    ULONG value = resource ? (ULONG)(uintptr_t)resource->native.ap_ptree : 0;
    memcpy(global+5,&value,sizeof(value));
    value = resource ? (ULONG)(uintptr_t)resource->native.ap_rscmem : 0;
    memcpy(global+7,&value,sizeof(value));
    global[9] = resource ? resource->native.ap_rsclen : 0;
}
static RESOURCE_CONTEXT **selected(WORD *global, BOOL tree_table)
{
    RESOURCE_CONTEXT **link;
    ULONG image = global_pointer(global,7), trees = global_pointer(global,5);
    for (link = &resources; *link; link = &(*link)->next)
        if (tree_table ? (ULONG)(uintptr_t)(*link)->native.ap_ptree == trees :
            (ULONG)(uintptr_t)(*link)->native.ap_rscmem == image) break;
    return link;
}

/* Validate reachable sibling rings before any normal AES traversal can loop
 * on a malformed disk tree. Duplicate tree-table entries share one preflight. */
static BOOL valid_trees(RESOURCE_CONTEXT *resource, BOOL offsets)
{
    typedef struct { WORD object, next, last; } FRAME;
    RSHDR *header = resource->native.ap_rscmem;
    OBJECT *objects = (OBJECT *)((char *)header+header->rsh_object), *tree;
    FRAME *stack;
    UBYTE *visited, *validated;
    LONG i, root, count, depth;
    WORD node, child;
    BOOL ok = FALSE;
    if (!header->rsh_ntree) return TRUE; /* Free strings/images need no tree. */
    if (header->rsh_nobs <= 0) return FALSE;
    stack = aes_resource_alloc((LONG)header->rsh_nobs*(sizeof(FRAME)+2));
    if (!stack) return FALSE;
    visited = (UBYTE *)(stack+header->rsh_nobs); validated = visited+header->rsh_nobs;
    memset(validated,0,header->rsh_nobs);
    for (i = 0; i < header->rsh_ntree; i++) {
        tree = resource->native.ap_ptree[i];
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
WORD mt_rsrc_load(const char *name, WORD *global)
{
    RESOURCE_CONTEXT *resource, *saved = aes_resource;
    LONG handle, session;
    BOOL ok;
    if (!name) return 0;
    session = aes_application_session(global);
    if (session < 0 || !retry_resource_close()) return 0;
    retry_resource_releases();
    resource = aes_resource_alloc(sizeof(*resource));
    if (!resource) return 0;
    memset(resource,0,sizeof(*resource));
    if (!aes_layout_metrics(&resource->metrics,global)) { dispose_resource(resource); return 0; }
    handle = open_resource(name);
    if (handle < 0) { dispose_resource(resource); return 0; }
    pending_close = handle;
    aes_resource = resource;
    ok = aes_resource_read(&resource->native,(UWORD)handle) != 0;
    if (!close_resource_file()) ok = FALSE;
    if (ok) { resource->header = *resource->native.ap_rscmem; ok = valid_trees(resource,FALSE); }
    if (ok && aes_application_session(global) != session) ok = FALSE;
    if (ok) {
        aes_resource_fix();
        resource->next = resources; resources = resource;
        publish(global,resource);
    } else {
        dispose_resource(resource);
    }
    aes_resource = saved;
    return ok ? 1 : 0;
}
WORD rsrc_load(const char *name) { return mt_rsrc_load(name,aes_global); }

WORD mt_rsrc_rcfix(void *image, WORD *global)
{
    RSHDR *header = image, *staged;
    RESOURCE_CONTEXT *resource, *p;
    OBJECT *objects;
    LONG session;
    WORD i;
    BOOL ok;
    if (!header || (uintptr_t)header % __alignof__(void *)) return 0;
    session = aes_application_session(global);
    if (session < 0 || header->rsh_rssize < sizeof(*header)) return 0;
    /* Fixing a live image twice must never relocate pointers or scale twice.
     * GEM callers release the selected resource before installing rcfix data. */
    if (*selected(global,FALSE)) return 0;
    for (p = resources; p; p = p->next)
        if (p->native.ap_rscmem == image) return 0;
    retry_resource_releases();
    resource = aes_resource_alloc(sizeof(*resource));
    if (!resource) return 0;
    memset(resource,0,sizeof(*resource));
    resource->caller_owned = TRUE;
    resource->native.ap_rscmem = image;
    resource->native.ap_rsclen = header->rsh_rssize;
    resource->native.ap_ptree = (OBJECT **)((char *)image+header->rsh_trindex);
    resource->header = *header;
    if (!aes_layout_metrics(&resource->metrics,global)) { dispose_resource(resource); return 0; }
    staged = aes_resource_alloc(header->rsh_rssize);
    if (!staged) { dispose_resource(resource); return 0; }
    memcpy(staged,image,header->rsh_rssize);
    ok = rs_rcfix_native(header,staged) && valid_trees(resource,TRUE);
    if (ok && aes_application_session(global) != session) ok = FALSE;
    if (ok) {
        objects = (OBJECT *)((char *)staged+staged->rsh_object);
        for (i = 0; i < staged->rsh_nobs; i++)
            rs_obfix_metrics(objects,i,resource->metrics.wchar,resource->metrics.hchar,resource->metrics.width);
        /* All fallible work precedes the in-place commit. Pointer targets
         * already refer to caller storage, never to the temporary copy. */
        memcpy(image,staged,header->rsh_rssize);
        resource->next = resources; resources = resource;
        publish(global,resource);
    } else dispose_resource(resource);
    aes_resource_discard(staged);
    return ok ? 1 : 0;
}
WORD rsrc_rcfix(void *image) { return mt_rsrc_rcfix(image,aes_global); }

WORD mt_rsrc_free(WORD *global)
{
    RESOURCE_CONTEXT **link, *resource;
    if (aes_application_session(global) < 0) return 0;
    link = selected(global,FALSE); resource = *link;
    if (!resource || (!resource->caller_owned &&
        aes_resource_release(resource->native.ap_rscmem) != E_OK)) return 0;
    *link = resource->next;
    resource->native.ap_rscmem = NULL;
    dispose_resource(resource);
    publish(global,NULL);
    return 1;
}
WORD rsrc_free(void) { return mt_rsrc_free(aes_global); }
void aes_resources_reset(void)
{
    RESOURCE_CONTEXT *next;
    retry_resource_close();
    while (resources) {
        next = resources->next;
        retire_resource(resources);
        resources = next;
    }
    retry_resource_releases();
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
WORD mt_rsrc_gaddr(WORD type, WORD index, void *address, WORD *global)
{
    RESOURCE_CONTEXT *resource, *saved = aes_resource;
    void *value = (void *)-1L;
    if (!address) return 0;
    if (aes_application_session(global) >= 0) {
        resource = *selected(global,type == R_TREE);
        if (resource && !memcmp(resource->native.ap_rscmem,&resource->header,sizeof(resource->header)) &&
            address_index(&resource->header,type,index)) {
            aes_resource = resource;
            value = aes_resource_addr(type,index);
            aes_resource = saved;
        }
    }
    memcpy(address,&value,sizeof(value));
    return value != (void *)-1L;
}
WORD rsrc_gaddr(WORD type, WORD index, void *address) { return mt_rsrc_gaddr(type,index,address,aes_global); }
WORD mt_rsrc_saddr(WORD type, WORD index, void *address, WORD *global)
{
    void *slot;
    if (!mt_rsrc_gaddr(type,index,&slot,global) || !slot) return 0;
    memcpy(slot,&address,sizeof(address));
    return 1;
}
WORD rsrc_saddr(WORD type, WORD index, void *address) { return mt_rsrc_saddr(type,index,address,aes_global); }
