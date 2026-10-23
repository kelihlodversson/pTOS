/* Freestanding x32 EmuDesk process runtime. GPL version 2 or later. */
#include "emutos.h"
#include "bdosbind.h"
#include "obdefs.h"
#include "deskmain.h"
#include "syscall.h"
#include "asm.h"
#include <stdarg.h>

extern void *desk_basepage;
void desk_user_start(PD *basepage, long entry_type) NORETURN;
void desk_user_start(PD *basepage, long entry_type)
{
    (void)entry_type;
    desk_basepage = basepage;
    deskmain();
    Pterm0();
    for (;;)
        ;
}

/* Every supported variadic call has an explicit arity. Unused registers
 * are always zero, including the no-argument calls. */
long trap1(WORD function, ...)
{
    va_list ap;
    long args[4] = { 0, 0, 0, 0 };
    WORD n, i;

    switch (function) {
    case 0x00: case 0x07: case 0x0b: case 0x19: case 0x2f: case 0x4f:
        n = 0; break;
    case 0x06: case 0x09: case 0x0e: case 0x1a: case 0x39: case 0x3a:
    case 0x3b: case 0x3e: case 0x41: case 0x48: case 0x49: case 0x4c:
        n = 1; break;
    case 0x36: case 0x3c: case 0x3d: case 0x44: case 0x47: case 0x4e:
        n = 2; break;
    case 0x3f: case 0x40: case 0x42: case 0x43: case 0x4a: case 0x56:
    case 0x57: case 0x154:
        n = 3; break;
    case 0x4b:
        n = 4; break;
    default:
        return -32;
    }
    va_start(ap, function);
    for (i = 0; i < n; i++)
        args[i] = va_arg(ap, long);
    va_end(ap);
    return desk_syscall(1, (UWORD)function, args[0], args[1], args[2], args[3]);
}

/* Resource paths supplied by EmuDesk are already absolute boot-drive paths. */
WORD sh_find(char *path);
WORD sh_find(char *path)
{
    return Fsfirst(path, FA_RO | FA_HIDDEN | FA_SYSTEM) == 0;
}
