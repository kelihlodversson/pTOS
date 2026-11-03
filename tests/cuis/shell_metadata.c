/* GEM metadata bounds, failure atomicity and balanced nested ownership. */
#include "gem.h"
#include "transport.h"
#include "gemerror.h"
#include "string.h"
#define VERIFY(c) do { if (!(c)) { __builtin_printf("shell metadata failure: %d\n",__LINE__); return __LINE__; } } while (0)
short aes_global[15] = { 0x400,-1,7 };
static UBYTE shared[4][256];
static LONG sizes[4], depth = 2, writes, calls;
static BOOL fail_lock, fail_read, fail_write, fail_size, fail_release;
LONG aes_application_session(short *global) { return global && global[2] == 7 ? 9 : EACCDN; }
LONG cuis_call6(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e, UQUAD f)
{
    calls++;
    if (a != 9) return EACCDN;
    if (fn == CUIS_DISPLAY_LOCK) {
        if (b) { if (fail_lock) return EAGAIN; depth++; }
        else { if (fail_release) { fail_release = FALSE; return EAGAIN; } depth--; }
        return E_OK;
    }
    if (fn != CUIS_CONTEXT || (b != 1 && b != 2) || f || !depth || e > 256) return EINVAL;
    if (c == CUIS_CONTEXT_SIZE) return fail_size ? EACCDN : sizes[b];
    if (c == CUIS_CONTEXT_WRITE) {
        if (fail_write) return EIMBA;
        memcpy(shared[b],(void *)(uintptr_t)d,e);
        if ((LONG)e > sizes[b]) sizes[b] = e;
        writes++;
        return E_OK;
    }
    if (c == CUIS_CONTEXT_READ) {
        memcpy((void *)(uintptr_t)d,shared[b],e);
        return fail_read ? EIMBA : E_OK;
    }
    return EINVFN;
}
static BOOL untouched(const char *command, const char *directory, const char *scrap)
{
    return !memcmp(command,"KEEP",5) && !memcmp(directory,"SAVED",6) && !memcmp(scrap,"SCRAP",6);
}
int main(void)
{
    char command[14] = "KEEP", directory[115] = "SAVED", scrap[129] = "SCRAP";
    char max_command[13], max_directory[114], max_scrap[128];
    char too_long[129], copy[127];
    short global[15];
    LONG previous, count;
    int i;
    memcpy(global,aes_global,sizeof(global));
    shel_rdef(command,directory);
    VERIFY(!*command && !*directory && scrp_read(scrap) == 1 && !*scrap);
    memset(max_command,'c',sizeof(max_command)); max_command[12] = 0;
    memset(max_directory,'d',sizeof(max_directory)); max_directory[113] = 0;
    memset(max_scrap,'s',sizeof(max_scrap)); max_scrap[127] = 0;
    mt_shel_wdef(max_command,max_directory,global);
    VERIFY(writes == 1 && sizes[1] == 127 && !sizes[2]);
    VERIFY(mt_scrp_write(max_scrap,global) == 1 && sizes[2] == 128);
    command[13] = directory[114] = scrap[128] = 99;
    mt_shel_rdef(command,directory,global);
    VERIFY(mt_scrp_read(scrap,global) == 1);
    VERIFY(!memcmp(command,max_command,13) && !memcmp(directory,max_directory,114) && !memcmp(scrap,max_scrap,128));
    VERIFY(command[13] == 99 && directory[114] == 99 && scrap[128] == 99);
    /* Short replacements must erase the old record, including the other field. */
    shel_wdef("APP.PRG","");
    VERIFY(scrp_write("relative-file.txt") == 1);
    shel_rdef(command,directory);
    VERIFY(!strcmp(command,"APP.PRG") && !*directory && scrp_read(scrap) == 1 && !strcmp(scrap,"relative-file.txt"));
    for (i = 8; i < 127; i++) VERIFY(!shared[1][i]);
    memset(too_long,'x',128); too_long[128] = 0;
    previous = writes;
    shel_wdef(too_long,""); shel_wdef("OK.PRG",too_long);
    shel_wdef(NULL,""); shel_wdef("",NULL);
    VERIFY(!scrp_write(too_long) && !scrp_write(NULL) && !scrp_read(NULL));
    VERIFY(writes == previous);
    strcpy(command,"KEEP"); strcpy(directory,"SAVED"); strcpy(scrap,"SCRAP");
    count = calls;
    mt_shel_rdef(command,directory,NULL); mt_shel_wdef("x","y",NULL);
    VERIFY(!mt_scrp_read(scrap,NULL) && !mt_scrp_write("x",NULL) && calls == count);
    shel_rdef(NULL,directory); shel_rdef(command,NULL);
    VERIFY(untouched(command,directory,scrap) && calls == count);
    fail_lock = TRUE;
    shel_rdef(command,directory); shel_wdef("x","y");
    VERIFY(!scrp_read(scrap) && !scrp_write("x")); fail_lock = FALSE;
    fail_read = TRUE;
    shel_rdef(command,directory); VERIFY(!scrp_read(scrap)); fail_read = FALSE;
    fail_size = TRUE;
    shel_rdef(command,directory); VERIFY(!scrp_read(scrap)); fail_size = FALSE;
    fail_release = TRUE; shel_rdef(command,directory);
    fail_release = TRUE; VERIFY(!scrp_read(scrap));
    VERIFY(untouched(command,directory,scrap));
    memcpy(copy,shared[1],sizeof(copy));
    fail_write = TRUE; shel_wdef("x","y"); VERIFY(!scrp_write("x")); fail_write = FALSE;
    VERIFY(writes == previous && !memcmp(copy,shared[1],sizeof(copy)) && !strcmp((char *)shared[2],"relative-file.txt"));
    /* Raw opaque storage can contain a truncated or unterminated record. */
    sizes[1] = 126; sizes[2] = 127;
    shel_rdef(command,directory); VERIFY(!scrp_read(scrap));
    sizes[1] = sizes[2] = 32768;
    shel_rdef(command,directory); VERIFY(!scrp_read(scrap));
    sizes[1] = sizes[2] = -1;
    shel_rdef(command,directory); VERIFY(!scrp_read(scrap));
    sizes[1] = 127; sizes[2] = 128;
    memset(shared[1],'x',13); memset(shared[2],'x',128);
    shel_rdef(command,directory); VERIFY(!scrp_read(scrap));
    shared[1][0] = 0; memset(shared[1]+13,'x',114);
    shel_rdef(command,directory);
    VERIFY(untouched(command,directory,scrap));
    /* Repair a larger stale record without depending on context truncation. */
    sizes[1] = sizes[2] = 256;
    shel_wdef("FIXED.PRG","Z:\\MISSING"); VERIFY(scrp_write("") == 1);
    shel_rdef(command,directory);
    VERIFY(!strcmp(command,"FIXED.PRG") && !strcmp(directory,"Z:\\MISSING") && scrp_read(scrap) == 1 && !*scrap);
    VERIFY(sizes[1] == 256 && sizes[2] == 256 && depth == 2);
    return 0;
}
