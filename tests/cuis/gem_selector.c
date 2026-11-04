/* File selection through normal GEM headers in the isolated x32 client. */
#include <gem.h>
#include "portab.h"
#include "ptos.h"
#include "gem_draw_probe.h"
#define VERIFY(c) do { if (!(c)) return 0x8200 | __LINE__; } while (0)
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
int gem_selector_checks(short handle)
{
    short global[15], button, before[3], i, stage;
    short points[3][2] = { {400,300},{500,350},{600,400} };
    char path[128], selection[13], cwd[128], again[128];
    UBYTE dta[64], expected[64];
    UQUAD original_dta = file_call(0x2f,0,0), drive = file_call(0x19,0,0);
    static const char *markers[] = {
        "x32 GEM file selector 0: READY\r\n", "x32 GEM file selector 1: READY\r\n",
        "x32 GEM file selector 2: READY\r\n"
    };
    for (i = 0; i < 15; i++) global[i] = aes_global[i];
    for (i = 0; i < 64; i++) dta[i] = expected[i] = (UBYTE)(i+30);
    VERIFY((LONG)file_call(0x47,(uintptr_t)cwd,0) == 0);
    (void)file_call(0x1a,(uintptr_t)dta,0);
    VERIFY(file_call(0x2f,0,0) == (uintptr_t)dta);
    for (stage = 0; stage < 3; stage++) {
        static const char initial_path[] = "C:\\*.TXT", initial_name[] = "HELLO.TXT";
        for (i = 0; i < (short)sizeof(initial_path); i++) path[i] = initial_path[i];
        for (i = 0; i < (short)sizeof(initial_name); i++) selection[i] = initial_name[i];
        button = 99;
        {
            unsigned length = 0;
            while (markers[stage][length]) length++;
            ptos_write(1,markers[stage],length);
        }
        for (i = 0; i < 3; i++) before[i] = gem_probe_pixel(handle,points[i][0],points[i][1]);
        if (stage == 0) VERIFY(mt_fsel_exinput(path,selection,&button,"Pick a text file",global) == 1);
        else if (stage == 1) VERIFY(fsel_input(path,selection,&button) == 1);
        else VERIFY(fsel_exinput(path,selection,&button,"Again") == 1);
        VERIFY(button == (stage != 1) && equal(path,initial_path) && equal(selection,initial_name));
        VERIFY(file_call(0x2f,0,0) == (uintptr_t)dta && file_call(0x19,0,0) == drive);
        VERIFY((LONG)file_call(0x47,(uintptr_t)again,0) == 0 && equal(cwd,again));
        for (i = 0; i < 64; i++) VERIFY(dta[i] == expected[i]);
        for (i = 0; i < 3; i++) VERIFY(gem_probe_pixel(handle,points[i][0],points[i][1]) == before[i]);
        VERIFY(wind_update(BEG_UPDATE|0x100) == 1 && wind_update(END_UPDATE) == 1);
    }
    (void)file_call(0x1a,original_dta,0);
    VERIFY(file_call(0x2f,0,0) == original_dta);
    {
        static const char pass[] = "x32 GEM file selectors: PASS\r\n";
        ptos_write(1,pass,sizeof(pass)-1);
    }
    return 0;
}
