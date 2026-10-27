/* Resource ownership, lookup, malformed-file and I/O failure contracts. */
#include "gem.h"
#include "gemerror.h"
#include "gem_resources.h"
#include "../../lib/aes/resource_context.h"
#include "../../lib/aes/transport.h"
#include "string.h"
#define VERIFY(c) do { if (!(c)) { __builtin_printf("resource failure: %d\n",__LINE__); return __LINE__; } } while (0)
extern void *fopen(const char *, const char *);
extern size_t fread(void *, size_t, size_t, void *);
extern int fclose(void *);
short aes_global[15] = { 0,0,1 };
static UBYTE disk[16384], original[16384];
static LONG disk_size, position;
static int files, allocations, alloc_after = -1, reads, short_read, close_error, seek_error, graphics_error;
static BOOL present = TRUE, registration_error, invalidate_on_close;
static void *blocks[128];
static int free_after = -1;
static const char *available = "C:\\START\\APP\\GEMTEST.RSC";
static char tried[16][128];
static int attempts;
LONG aes_application_session(short *global) { return global && global[2] == 1 && !registration_error ? 7 : EACCDN; }
BOOL aes_layout_metrics(LAYOUT_METRICS *metrics, WORD *global)
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
    LONG bytes;
    switch (function) {
    case 0x19: return 2;
    case 0x47: strcpy((void *)(uintptr_t)a,"\\START"); return E_OK;
    case 0x3d:
        if (attempts < 16) strcpy(tried[attempts],(void *)(uintptr_t)a);
        attempts++;
        if (!present || strcmp((void *)(uintptr_t)a,available)) return (UQUAD)(QUAD)EFILNF;
        files++; reads = 0; position = 0; return 3;
    case 0x3e:
        if (a != 3 || !files) return (UQUAD)(QUAD)EIHNDL;
        files--;
        if (invalidate_on_close) registration_error = TRUE;
        return close_error ? (UQUAD)(QUAD)EACCDN : E_OK;
    case 0x3f:
        if (a != 3 || !files) return (UQUAD)(QUAD)EIHNDL;
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
int main(int argc, char **argv)
{
    void *file, *address;
    WORD saved[15];
    int i, result, before, size;
    VERIFY(argc == 3);
    file = fopen(argv[1],"rb"); VERIFY(file != NULL);
    disk_size = fread(disk,1,sizeof(disk),file); VERIFY(fclose(file) == 0 && disk_size > 36);
    memcpy(original,disk,sizeof(disk));
    aes_resources_init();
    result = gem_resource_checks(1,8,16);
    if (result) __builtin_printf("GEM resource failure: %x\n",result);
    VERIFY(result == 0 && !files && !allocations);
    /* All allocation failures retain a previously selected resource. */
    VERIFY(rsrc_load("GEMTEST.RSC") == 1); memcpy(saved,aes_global,sizeof(saved));
    before = allocations;
    for (i = 0; i < 4; i++) {
        alloc_after = i;
        VERIFY(rsrc_load("GEMTEST.RSC") == 0 && allocations == before && !files);
        VERIFY(!memcmp(saved,aes_global,sizeof(saved)) && rsrc_gaddr(R_TREE,0,&address) == 1);
    }
    alloc_after = -1;
    for (i = 1; i <= 2; i++) {
        short_read = i;
        VERIFY(rsrc_load("GEMTEST.RSC") == 0 && allocations == before && !files);
    }
    short_read = 0; close_error = 1;
    VERIFY(rsrc_load("GEMTEST.RSC") == 0 && allocations == before && !files);
    close_error = 0; seek_error = 1;
    VERIFY(rsrc_load("GEMTEST.RSC") == 0 && allocations == before && !files);
    seek_error = 0; invalidate_on_close = TRUE;
    VERIFY(rsrc_load("GEMTEST.RSC") == 0 && allocations == before && !files);
    invalidate_on_close = registration_error = FALSE;
    VERIFY(!memcmp(saved,aes_global,sizeof(saved)));
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
    memcpy(aes_global,saved,sizeof(saved)); memcpy(disk,original,sizeof(disk));
    VERIFY(!memcmp(saved,aes_global,sizeof(saved)));
    free_after = 0;
    VERIFY(rsrc_free() == 0 && allocations == before && !memcmp(saved,aes_global,sizeof(saved)));
    free_after = -1;
    /* Header changes must not turn bounds checks into arbitrary table reads. */
    memcpy(&address,aes_global+7,4);
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
    /* Search order: app directory, caller path, drive root, then PATH. */
    available = "GOOD\\GEMTEST.RSC"; attempts = 0;
    VERIFY(rsrc_load("ELSE\\GEMTEST.RSC") == 1);
    VERIFY(attempts == 5 && !strcmp(tried[0],"C:\\START\\APP\\GEMTEST.RSC") &&
        !strcmp(tried[1],"ELSE\\GEMTEST.RSC") && !strcmp(tried[2],"\\GEMTEST.RSC") &&
        !strcmp(tried[3],"BAD\\GEMTEST.RSC") && !strcmp(tried[4],"GOOD\\GEMTEST.RSC"));
    aes_resources_reset(); VERIFY(!allocations && !files && rsrc_free() == 0);
    available = "C:\\START\\APP\\GEMORIG.RSC";
    file = fopen(argv[2],"rb"); VERIFY(file != NULL);
    disk_size = fread(disk,1,sizeof(disk),file); VERIFY(fclose(file) == 0);
    VERIFY(gem_resource_original() == 0 && !allocations && !files);
    graphics_error = 1;
    VERIFY(gem_resource_checks(0,8,16) == 0 && !allocations && !files);
    return 0;
}
