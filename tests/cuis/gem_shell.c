/* Shell APIs through normal GEM headers, inside isolated dynamic clients. */
#include <mint/basepage.h>
#include <gem.h>
#include "portab.h"
#include "ptos.h"
#include "gem_shell.h"
#define VERIFY(c) do { if (!(c)) return 0x8600 | __LINE__; } while (0)
static UQUAD file_call(ULONG function, UQUAD a, UQUAD b)
{
    register UQUAD ax __asm__("rax") = (1ULL<<32)|function;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"(a),"S"(b) : "rcx","r11","memory","cc");
    return ax;
}
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static void copy(char *dest, const char *src) { while ((*dest++ = *src++) != 0) ; }
int gem_shell_checks(short *global, int child)
{
    char command[260], again[260], tail[128], saved[128], cwd[128], *value;
    UBYTE dta[64], expected[64];
    UQUAD original_dta;
    int i;
    BASEPAGE *basepage = (BASEPAGE *)(uintptr_t)ptos_basepage;
    VERIFY(mt_shel_read(command,tail,global) == 1 && command[0] && command[1] == ':');
    for (i = 0; i < 128; i++) VERIFY((UBYTE)tail[i] == (UBYTE)basepage->p_cmdlin[i]);
    VERIFY(shel_envrn(&value,"PTOS_SHELL_ABSENT=") == 1 && !value);
    if (!child) return 0;
    VERIFY(equal(command,"C:\\APPS\\GEMSHELL.TOS"));
    VERIFY(shel_envrn(&value,"SHELLTEST=") == 1 && value && equal(value,"value"));
    VERIFY(mt_shel_envrn(&value,"SHELLTEST",global) == 1 && value && equal(value,"=value"));
    VERIFY((LONG)file_call(0x47,(uintptr_t)cwd,0) == 0);
    VERIFY((LONG)file_call(0x3b,(uintptr_t)"C:\\WORK",0) == 0);
    VERIFY(shel_read(again,saved) == 1 && equal(command,again));
    for (i = 0; i < 128; i++) VERIFY(tail[i] == saved[i]);
    original_dta = file_call(0x2f,0,0);
    for (i = 0; i < 64; i++) dta[i] = expected[i] = (UBYTE)(i+20);
    (void)file_call(0x1a,(uintptr_t)dta,0);
    VERIFY(file_call(0x2f,0,0) == (uintptr_t)dta);
    copy(again,"SHELL.DAT");
    VERIFY(shel_find(again) == 1 && equal(again,"C:\\APPS\\SHELL.DAT"));
    copy(again,"CURRENT.DAT");
    VERIFY(mt_shel_find(again,global) == 1 && equal(again,"CURRENT.DAT"));
    copy(again,"ROOT.DAT");
    VERIFY(shel_find(again) == 1 && equal(again,"\\ROOT.DAT"));
    copy(again,"SEARCH.DAT");
    VERIFY(shel_find(again) == 1 && equal(again,"C:\\RPATH\\SEARCH.DAT"));
    copy(again,"*.DAT");
    VERIFY(shel_find(again) == 1 && equal(again,"C:\\APPS\\*.DAT"));
    copy(again,"ABSENT.DAT");
    VERIFY(shel_find(again) == 0 && equal(again,"ABSENT.DAT"));
    VERIFY(file_call(0x2f,0,0) == (uintptr_t)dta);
    for (i = 0; i < 64; i++) VERIFY(dta[i] == expected[i]);
    VERIFY((LONG)file_call(0x47,(uintptr_t)again,0) == 0 && equal(again,"\\WORK"));
    (void)file_call(0x1a,original_dta,0);
    VERIFY(file_call(0x2f,0,0) == original_dta && (LONG)file_call(0x3b,(uintptr_t)cwd,0) == 0);
    return 0;
}
