/* Resource ownership, lookup, malformed-file and I/O failure contracts. */
#include "gem.h"
#include "gemerror.h"
#include "gem_resources.h"
#include "../../lib/aes/resource_context.h"
#include "../../lib/aes/app_context.h"
#include "../../lib/aes/transport.h"
#include "string.h"
#define VERIFY(c) do { if (!(c)) { __builtin_printf("resource failure: %d\n",__LINE__); return __LINE__; } } while (0)
extern void *fopen(const char *, const char *);
extern size_t fread(void *, size_t, size_t, void *);
extern int fclose(void *);
static UBYTE disk[16384], original[16384];
static LONG disk_size, position;
static int files, allocations, alloc_after = -1, reads, short_read, close_error, seek_error, graphics_error;
static int close_keeps_handle, close_calls;
static BOOL open_handles[16];
static LONG stuck_handle = -1;
static BOOL present = TRUE, registration_error, invalidate_on_close;
static void *blocks[128];
static int free_after = -1;
static const char *available = "C:\\START\\APP\\GEMTEST.RSC";
static char tried[16][128];
static int attempts;
LONG aes_application_session(AESGLOBAL *global) { return global && global->ap_id == 1 && !registration_error ? 7 : EACCDN; }
BOOL aes_layout_metrics(LAYOUT_METRICS *metrics, AESGLOBAL *global)
{
    if (graphics_error || aes_application_session(global) < 0) return FALSE;
    metrics->wchar = 8; metrics->hchar = 16; metrics->hbox = 19;
    metrics->width = 640; metrics->height = 480;
    return TRUE;
}
unsigned long aes_test_getauxval(unsigned long type) { return type == 31 ? (uintptr_t)"APP\\TEST.TOS" : 0; }
char *aes_test_getenv(const char *name) { return !strcmp(name,"PATH") ? "BAD;GOOD,OTHER" : NULL; }
UQUAD aes_memory_call(ULONG function, UQUAD argument)
{
    int i;
    void *p;
    if (function == 0x48) {
        if (!alloc_after) return (UQUAD)(QUAD)ENSMEM;
        if (alloc_after > 0) alloc_after--;
        for (i = 0; i < 128 && blocks[i]; i++) ;
        if (i == 128) return (UQUAD)(QUAD)ENSMEM;
        p = __builtin_malloc(argument);
        if (!p) return (UQUAD)(QUAD)ENSMEM;
        blocks[i] = p; allocations++;
        return (uintptr_t)p;
    }
    if (function == 0x49) {
        p = (void *)(uintptr_t)argument;
        if (!free_after) return (UQUAD)(QUAD)EACCDN;
        if (free_after > 0) free_after--;
        for (i = 0; i < 128 && blocks[i] != p; i++) ;
        if (i == 128 || !p) return (UQUAD)(QUAD)EIMBA;
        blocks[i] = NULL; allocations--; __builtin_free(p);
        return E_OK;
    }
    return (UQUAD)(QUAD)EINVFN;
}
UQUAD aes_gemdos_call(ULONG function, UQUAD a, UQUAD b, UQUAD c)
{
    LONG bytes, handle;
    switch (function) {
    case 0x19: return 2;
    case 0x47: strcpy((void *)(uintptr_t)a,"\\START"); return E_OK;
    case 0x3d:
        if (attempts < 16) strcpy(tried[attempts],(void *)(uintptr_t)a);
        attempts++;
        if (!present || strcmp((void *)(uintptr_t)a,available)) return (UQUAD)(QUAD)EFILNF;
        for (handle = 0; handle < 16 && open_handles[handle]; handle++) ;
        if (handle == 16) return (QUAD)ENHNDL;
        open_handles[handle] = TRUE; files++; reads = 0; position = 0; return handle+3;
    case 0x3e:
        close_calls++;
        if (a < 3 || a >= 19 || !open_handles[a-3]) return (UQUAD)(QUAD)EIHNDL;
        if (close_keeps_handle && (stuck_handle < 0 || a == (UQUAD)stuck_handle)) {
            if (close_keeps_handle > 0) close_keeps_handle--;
            return (UQUAD)(QUAD)EACCDN;
        }
        open_handles[a-3] = FALSE; files--;
        if (invalidate_on_close) registration_error = TRUE;
        return close_error ? (UQUAD)(QUAD)EACCDN : E_OK;
    case 0x3f:
        if (a < 3 || a >= 19 || !open_handles[a-3]) return (UQUAD)(QUAD)EIHNDL;
        bytes = b;
        if (bytes > disk_size-position) bytes = disk_size-position;
        if (++reads == short_read && bytes) bytes--;
        memcpy((void *)(uintptr_t)c,disk+position,bytes); position += bytes;
        return bytes;
    case 0x42:
        if (seek_error) return (UQUAD)(QUAD)EACCDN;
        position = (LONG)a;
        return position;
    default: return (UQUAD)(QUAD)EINVFN;
    }
}
static void disk_word(int offset, unsigned int value) { disk[offset] = value>>8; disk[offset+1] = value; }
static void disk_long(int offset, unsigned long value) { disk_word(offset,value>>16); disk_word(offset+2,value); }
/* Independent ownership uses the same mocked native session deliberately. */
static int context_checks(void)
{
    AES_APP_CONTEXT left = { 0 }, right = { 0 };
    AESGLOBAL first = { .ap_id = 1, .ap_3resv = &left };
    AESGLOBAL second = { .ap_id = 1, .ap_3resv = &right }, alias, foreign;
    void *address, *a, *b;
    int calls, opens;
    VERIFY(mt_rsrc_load("GEMTEST.RSC",&first) == 1);
    VERIFY(mt_rsrc_load("GEMTEST.RSC",&second) == 1 && allocations == 4);
    alias = first; foreign = first; foreign.ap_3resv = second.ap_3resv;
    VERIFY(mt_rsrc_gaddr(R_TREE,0,&address,&alias) == 1 && address == first.ap_ptree[0]);
    VERIFY(!mt_rsrc_gaddr(R_TREE,0,&address,&foreign) && !mt_rsrc_free(&foreign));
    /* Public selectors select another owned image through the same alias. */
    VERIFY(mt_rsrc_load("GEMTEST.RSC",&alias) == 1 && allocations == 6);
    VERIFY(first.ap_rscmem != alias.ap_rscmem && first.ap_ptree != alias.ap_ptree);
    VERIFY(mt_rsrc_free(&alias) && allocations == 4);
    VERIFY(mt_rsrc_gaddr(R_TREE,0,&address,&first) == 1);
    free_after = 0;
    VERIFY(!aes_resources_reset(&alias) && allocations == 4);
    VERIFY(!mt_rsrc_gaddr(R_TREE,0,&address,&first));
    free_after = -1;
    VERIFY(aes_resources_reset(&second) && allocations == 2);
    VERIFY(mt_rsrc_load("GEMTEST.RSC",&second) == 1 && allocations == 4);
    VERIFY(aes_resources_reset(&first) && allocations == 2);
    VERIFY(mt_rsrc_gaddr(R_TREE,0,&address,&second) == 1);
    /* Failed temporary releases stay with their allocating context. */
    a = aes_temporary_alloc(32,&first); b = aes_temporary_alloc(48,&second);
    VERIFY(a && b && allocations == 4);
    free_after = 0;
    VERIFY(aes_resource_discard(a) != E_OK && aes_resource_discard(b) != E_OK);
    free_after = -1;
    VERIFY(aes_resources_reset(&alias) && allocations == 3);
    VERIFY(mt_rsrc_gaddr(R_TREE,0,&address,&second) == 1);
    VERIFY(aes_resources_reset(&second) && !allocations);
    /* A failed close in one context cannot block or be retried by another. */
    close_keeps_handle = -1; stuck_handle = 3;
    VERIFY(!mt_rsrc_load("GEMTEST.RSC",&first) && files == 1);
    calls = close_calls;
    VERIFY(aes_resources_reset(&second) && close_calls == calls && files == 1);
    VERIFY(mt_rsrc_load("GEMTEST.RSC",&second) == 1 && files == 1);
    opens = attempts;
    VERIFY(!mt_rsrc_load("GEMTEST.RSC",&alias) && attempts == opens && files == 1);
    VERIFY(aes_resources_reset(&second) && !allocations && files == 1);
    close_keeps_handle = 0; stuck_handle = -1;
    VERIFY(aes_resources_reset(&first) && !files && !allocations);
    return 0;
}
int main(int argc, char **argv)
{
    void *file, *address;
    AESGLOBAL saved = { 0 };
    AES_APP_CONTEXT context = { 0 };
    int i, result, before, size;
    aes_global = (AESGLOBAL){ .ap_id = 1, .ap_3resv = &context };
    VERIFY(argc == 3);
    file = fopen(argv[1],"rb"); VERIFY(file != NULL);
    disk_size = fread(disk,1,sizeof(disk),file); VERIFY(fclose(file) == 0 && disk_size > 36);
    memcpy(original,disk,sizeof(disk));
    aes_resources_init();
    VERIFY(context_checks() == 0);
    result = gem_resource_native(1,8,16);
    VERIFY(result == 0 && !allocations);
    for (i = 0; i < 4; i++) {
        alloc_after = i;
        VERIFY(gem_resource_native_failure() == 0 && !allocations);
    }
    alloc_after = -1;
    free_after = 0;
    VERIFY(gem_resource_native(1,8,16) == 0 && allocations > 0);
    aes_resources_reset(&aes_global); VERIFY(allocations > 0);
    free_after = -1;
    aes_resources_reset(&aes_global); VERIFY(!allocations);
    result = gem_resource_checks(1,8,16);
    if (result) __builtin_printf("GEM resource failure: %x\n",result);
    VERIFY(result == 0 && !files && !allocations);
    /* All allocation failures retain a previously selected resource. */
    VERIFY(rsrc_load("GEMTEST.RSC") == 1); memcpy(&saved,&aes_global,sizeof(saved));
    before = allocations;
    for (i = 0; i < 4; i++) {
        alloc_after = i;
        VERIFY(rsrc_load("GEMTEST.RSC") == 0 && allocations == before && !files);
        VERIFY(!memcmp(&saved,&aes_global,sizeof(saved)) && rsrc_gaddr(R_TREE,0,&address) == 1);
    }
    alloc_after = -1;
    for (i = 1; i <= 2; i++) {
        short_read = i;
        VERIFY(rsrc_load("GEMTEST.RSC") == 0 && allocations == before && !files);
    }
    short_read = 0; close_error = 1; close_calls = 0;
    VERIFY(rsrc_load("GEMTEST.RSC") == 0 && allocations == before && !files && close_calls == 2);
    /* The previous error released the handle: recovery saw EIHNDL.
     * Also exercise an interrupted close that leaves the handle live. */
    close_error = 0; close_keeps_handle = 1; close_calls = 0;
    VERIFY(rsrc_load("GEMTEST.RSC") == 0 && close_calls == 2 && !files && allocations == before);
    VERIFY(!memcmp(&saved,&aes_global,sizeof(saved)));
    /* Persistent close errors retain one handle and prevent further opens. */
    close_keeps_handle = -1; close_calls = 0;
    VERIFY(rsrc_load("GEMTEST.RSC") == 0 && close_calls == 2 && files == 1 && allocations == before);
    for (i = 0; i < 3; i++) {
        int opens = attempts;
        VERIFY(rsrc_load("GEMTEST.RSC") == 0 && attempts == opens && files == 1 && allocations == before);
    }
    VERIFY(!memcmp(&saved,&aes_global,sizeof(saved)));
    close_keeps_handle = 0;
    VERIFY(rsrc_load("GEMTEST.RSC") == 1 && !files);
    VERIFY(rsrc_free() == 1 && allocations == before);
    memcpy(&aes_global,&saved,sizeof(saved));
    /* Session cleanup retries a retained handle, even after an earlier
     * cleanup attempt failed, without discarding its ownership record. */
    close_keeps_handle = -1;
    VERIFY(rsrc_load("GEMTEST.RSC") == 0 && files == 1);
    aes_resources_reset(&aes_global); VERIFY(files == 1 && !allocations);
    close_keeps_handle = 0;
    aes_resources_reset(&aes_global); VERIFY(!files && !allocations);
    VERIFY(rsrc_load("GEMTEST.RSC") == 1);
    memcpy(&saved,&aes_global,sizeof(saved)); before = allocations;
    seek_error = 1;
    VERIFY(rsrc_load("GEMTEST.RSC") == 0 && allocations == before && !files);
    seek_error = 0; invalidate_on_close = TRUE;
    VERIFY(rsrc_load("GEMTEST.RSC") == 0 && allocations == before && !files);
    invalidate_on_close = registration_error = FALSE;
    VERIFY(!memcmp(&saved,&aes_global,sizeof(saved)));
    present = FALSE;
    VERIFY(rsrc_load("GEMTEST.RSC") == 0 && rsrc_gaddr(R_TREE,0,&address) == 1);
    present = TRUE;
    /* Truncations, invalid references, nonterminated text and cyclic trees. */
    size = disk_size;
    for (i = 0; i < size; i++) {
        disk_size = i;
        VERIFY(rsrc_load("GEMTEST.RSC") == 0 && allocations == before && !files);
    }
    disk_size = size;
    disk_word(20,0xffff);
    VERIFY(rsrc_load("GEMTEST.RSC") == 0 && allocations == before);
    memcpy(disk,original,sizeof(disk)); disk_long(36+24+12,0xffffff00UL);
    VERIFY(rsrc_load("GEMTEST.RSC") == 0 && allocations == before);
    memcpy(disk,original,sizeof(disk));
    {
        int ted = ((int)disk[4]<<8)|disk[5];
        unsigned long text = ((unsigned long)disk[ted]<<24)|((unsigned long)disk[ted+1]<<16)|((unsigned long)disk[ted+2]<<8)|disk[ted+3];
        memset(disk+text,'X',7);
        VERIFY(rsrc_load("GEMTEST.RSC") == 0 && allocations == before);
    }
    memcpy(disk,original,sizeof(disk)); disk_word(36+24,1);
    VERIFY(rsrc_load("GEMTEST.RSC") == 0 && allocations == before);
    memcpy(disk,original,sizeof(disk));
    /* A resource may contain only free strings, without any object tree. */
    for (i = 20; i <= 32; i += 2) disk_word(i,i == 30 ? 2 : 0);
    VERIFY(rsrc_load("GEMTEST.RSC") == 1);
    VERIFY(rsrc_gaddr(R_STRING,1,&address) == 1 && !strcmp(address,"Intact"));
    VERIFY(rsrc_gaddr(R_TREE,0,&address) == 0 && rsrc_free() == 1 && allocations == before);
    memcpy(&aes_global,&saved,sizeof(saved)); memcpy(disk,original,sizeof(disk));
    VERIFY(!memcmp(&saved,&aes_global,sizeof(saved)));
    free_after = 0;
    VERIFY(rsrc_free() == 0 && allocations == before && !memcmp(&saved,&aes_global,sizeof(saved)));
    free_after = -1;
    /* Header changes must not turn bounds checks into arbitrary table reads. */
    address = aes_global.ap_rscmem;
    if (sizeof(void *) == 4) {
        RSHDR *header = address;
        header->rsh_nobs++;
        VERIFY(rsrc_gaddr(R_OBJECT,8,&address) == 0);
        header->rsh_nobs--;
    }
    VERIFY(rsrc_free() == 1 && !allocations);
    /* Two indirect colour icons alias the same disk specification slot.
     * Include a direct reference to the same icon as well. */
    {
        OBJECT *tree;
        CICONBLK *colour;
        int slot = ((int)disk[36+2*24+12]<<24) |
            ((int)disk[36+2*24+13]<<16) |
            ((int)disk[36+2*24+14]<<8) | disk[36+2*24+15];
        int extension = (size+3)&~3;
        int table = extension+12, block = table+8;
        disk_word(0,4); disk_word(34,extension);
        memset(disk+size,0,block+54-size);
        disk_long(extension,block+54); disk_long(extension+4,table);
        disk_long(table,block); disk_long(table+4,0xffffffffUL);
        disk_word(block+22,16); disk_word(block+24,1);
        disk_word(block+38,0xa55a); disk_word(block+40,0xffff);
        disk_long(slot,0);
        disk_word(36+2*24+6,G_CICON); disk_word(36+3*24+6,G_CICON);
        disk_word(36+5*24+6,G_CICON); disk_long(36+5*24+12,0);
        disk_size = block+54;
        VERIFY(rsrc_load("GEMTEST.RSC") == 1);
        VERIFY(rsrc_gaddr(R_TREE,0,&address) == 1); tree = address;
        VERIFY(tree[2].ob_spec.indirect == tree[3].ob_spec.indirect);
        colour = tree[2].ob_spec.indirect->ciconblk;
        VERIFY(colour == tree[3].ob_spec.indirect->ciconblk &&
            colour == tree[5].ob_spec.ciconblk);
        VERIFY(colour->monoblk.ib_wicon == 16 &&
            colour->monoblk.ib_pdata[0] == (WORD)0xa55a &&
            colour->monoblk.ib_pmask[0] == (WORD)0xffff);
        VERIFY(rsrc_free() == 1 && !allocations && !files);
        /* Invalid indices must fail before any pointer fixup. */
        disk_long(slot,1);
        VERIFY(rsrc_load("GEMTEST.RSC") == 0 && !allocations && !files);
        memcpy(disk,original,sizeof(disk)); disk_size = size;
    }
    /* Failed reset releases retain ownership, but never remain selectable.
     * Cover an image pinned across resets, then a metadata-only failure. */
    for (i = 0; i < 2; i++) {
        AESGLOBAL stale = { 0 };
        VERIFY(rsrc_load("GEMTEST.RSC") == 1);
        memcpy(&stale,&aes_global,sizeof(stale));
        free_after = i;
        aes_resources_reset(&aes_global);
        VERIFY(allocations == 2-i && !files);
        VERIFY(mt_rsrc_gaddr(R_TREE,0,&address,&stale) == 0 &&
            mt_rsrc_free(&stale) == 0);
        aes_resources_reset(&aes_global); VERIFY(allocations == 2-i);
        free_after = -1;
        if (!i) {
            aes_resources_reset(&aes_global); VERIFY(!allocations);
            VERIFY(mt_rsrc_gaddr(R_TREE,0,&address,&stale) == 0);
        }
        /* A new load also retries retired metadata without selecting it. */
        VERIFY(rsrc_load("GEMTEST.RSC") == 1 && allocations == 2);
        aes_resources_reset(&aes_global); VERIFY(!allocations);
        VERIFY(rsrc_gaddr(R_TREE,0,&address) == 0);
    }
    /* Explicit free may release the image but fail to release its record. */
    VERIFY(rsrc_load("GEMTEST.RSC") == 1);
    free_after = 1;
    VERIFY(rsrc_free() == 1 && allocations == 1);
    VERIFY(rsrc_gaddr(R_TREE,0,&address) == 0);
    free_after = -1;
    aes_resources_reset(&aes_global); VERIFY(!allocations);
    /* Failed temporary frees retain input, validation scratch and rejected
     * images without requiring a new allocation to record ownership. */
    for (i = 0; i < 4; i++) {
        free_after = i == 1 ? 1 : 0;
        if (i == 2) disk_long(36+24+12,0xffffff00UL);
        if (i == 3) short_read = 2;
        VERIFY(rsrc_load("GEMTEST.RSC") == (i < 2 ? 1 : 0));
        VERIFY(allocations == (i == 0 ? 4 : i == 1 ? 3 : i == 2 ? 3 : 2));
        free_after = 0;
        aes_resources_reset(&aes_global);
        VERIFY(rsrc_gaddr(R_TREE,0,&address) == 0);
        free_after = -1; short_read = 0;
        memcpy(disk,original,sizeof(disk));
        aes_resources_reset(&aes_global); VERIFY(!allocations && !files);
        aes_resources_reset(&aes_global); VERIFY(!allocations);
    }
    /* Search order: app directory, caller path, drive root, then PATH. */
    available = "GOOD\\GEMTEST.RSC"; attempts = 0;
    VERIFY(rsrc_load("ELSE\\GEMTEST.RSC") == 1);
    VERIFY(attempts == 5 && !strcmp(tried[0],"C:\\START\\APP\\GEMTEST.RSC") &&
        !strcmp(tried[1],"ELSE\\GEMTEST.RSC") && !strcmp(tried[2],"\\GEMTEST.RSC") &&
        !strcmp(tried[3],"BAD\\GEMTEST.RSC") && !strcmp(tried[4],"GOOD\\GEMTEST.RSC"));
    aes_resources_reset(&aes_global); VERIFY(!allocations && !files && rsrc_free() == 0);
    available = "C:\\START\\APP\\GEMORIG.RSC";
    file = fopen(argv[2],"rb"); VERIFY(file != NULL);
    disk_size = fread(disk,1,sizeof(disk),file); VERIFY(fclose(file) == 0);
    VERIFY(gem_resource_original() == 0 && !allocations && !files);
    graphics_error = 1;
    VERIFY(gem_resource_native(0,8,16) == 0 && !allocations);
    VERIFY(gem_resource_checks(0,8,16) == 0 && !allocations && !files);
    return 0;
}
