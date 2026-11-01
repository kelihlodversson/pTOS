/* GEM buffer sizes, atomic output and balanced nested display scopes. */
#include "gem.h"
#include "transport.h"
#include "gemerror.h"
#include "string.h"
#define VERIFY(c) do { if (!(c)) { __builtin_printf("shell buffer failure: %d\n",__LINE__); return __LINE__; } } while (0)
extern void *malloc(__SIZE_TYPE__ bytes);
extern void free(void *memory);
short aes_global[15] = { 0x400,-1,7 };
static UBYTE shared[32767], pattern[32767], output[32769];
static LONG size, depth = 2, allocations;
static BOOL fail_alloc, fail_lock, fail_read, fail_write, fail_size, fail_release;
LONG aes_application_session(short *global) { return global && global[2] == 7 ? 9 : EACCDN; }
void *aes_temporary_alloc(LONG bytes)
{
    void *value = fail_alloc ? NULL : malloc(bytes);
    if (value) allocations++;
    return value;
}
WORD aes_resource_discard(void *value) { allocations--; free(value); return E_OK; }
LONG cuis_call6(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e, UQUAD f)
{
    if (a != 9) return EACCDN;
    if (fn == CUIS_DISPLAY_LOCK) {
        if (b) { if (fail_lock) return EAGAIN; depth++; }
        else { if (fail_release) { fail_release = FALSE; return EAGAIN; } depth--; }
        return E_OK;
    }
    if (fn != CUIS_CONTEXT || b || f || !depth) return EINVAL;
    if (c == CUIS_CONTEXT_SIZE) return fail_size ? EACCDN : size;
    if (c == CUIS_CONTEXT_WRITE) {
        if (fail_write) return EIMBA;
        memcpy(shared,(void *)(uintptr_t)d,e);
        if ((LONG)e > size) size = e;
        return E_OK;
    }
    if (c == CUIS_CONTEXT_READ) {
        memcpy((void *)(uintptr_t)d,shared,e);
        return fail_read ? EIMBA : E_OK; /* A partial copy must not affect caller output. */
    }
    return EINVFN;
}
int main(void)
{
    short global[15];
    int i;
    memcpy(global,aes_global,sizeof(global));
    VERIFY(shel_get(NULL,SHEL_BUFSIZE) == 4192 && depth == 2);
    memset(output,0x55,sizeof(output));
    VERIFY(mt_shel_get((char *)output+1,5000,global) == 1);
    VERIFY(output[0] == 0x55 && output[4193] == 0x55);
    for (i = 1; i <= 4192; i++) VERIFY(!output[i]);
    for (i = 0; i < 32767; i++) pattern[i] = (UBYTE)(i*13+5);
    VERIFY(shel_put((char *)pattern,6000) == 1 && shel_get(NULL,SHEL_BUFSIZE) == 6000);
    VERIFY(mt_shel_put("ABCD",4,global) == 1);
    VERIFY(mt_shel_get((char *)output,6000,global) == 1 && !memcmp(output,"ABCD",4));
    VERIFY(!memcmp(output+4,pattern+4,5996));
    VERIFY(shel_put((char *)pattern,32767) == 1 && mt_shel_get(NULL,SHEL_BUFSIZE,global) == 32767);
    VERIFY(shel_get((char *)output,32767) == 1 && !memcmp(pattern,output,32767));
    VERIFY(shel_put(NULL,0) == 1 && shel_get(NULL,0) == 1);
    VERIFY(!shel_get(NULL,1) && !shel_put(NULL,1) && !shel_get((char *)output,-2) && !shel_put("x",-1));
    VERIFY(!mt_shel_get((char *)output,4,NULL) && !mt_shel_put("x",1,NULL));
    memset(output,0x55,sizeof(output));
    fail_alloc = TRUE; VERIFY(!shel_get((char *)output,16)); fail_alloc = FALSE;
    fail_lock = TRUE; VERIFY(!shel_get((char *)output,16) && !shel_put("x",1)); fail_lock = FALSE;
    fail_read = TRUE; VERIFY(!shel_get((char *)output,16)); fail_read = FALSE;
    fail_size = TRUE; VERIFY(!shel_get((char *)output,16) && !shel_get(NULL,SHEL_BUFSIZE)); fail_size = FALSE;
    fail_write = TRUE; VERIFY(!shel_put("x",1) && shared[0] == pattern[0]); fail_write = FALSE;
    fail_release = TRUE; VERIFY(!shel_get((char *)output,16));
    for (i = 0; i < (int)sizeof(output); i++) VERIFY(output[i] == 0x55);
    VERIFY(depth == 2 && !allocations);
    return 0;
}
