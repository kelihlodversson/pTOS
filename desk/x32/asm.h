/* User-mode GEMDOS bindings for the x32 desktop. GPL version 2 or later. */
#ifndef DESK_X32_ASM_H
#define DESK_X32_ASM_H
#include "arch/x86_64/asm.h"
long trap1(WORD function, ...);
#define trap1_pexec(mode,path,tail,env) trap1(0x4b, (long)(mode), (long)(path), (long)(tail), (long)(env))
#endif
