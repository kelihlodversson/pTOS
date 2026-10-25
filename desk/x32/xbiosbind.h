/* User-mode XBIOS bindings. GPL version 2 or later. */
#ifndef DESK_X32_XBIOSBIND_H
#define DESK_X32_XBIOSBIND_H
#include "syscall.h"
#define Getrez() desk_syscall(14,4,0,0,0,0)
#define Setcolor(a,b) desk_syscall(14,7,(a),(b),0,0)
#define Random() desk_syscall(14,17,0,0,0,0)
#define Cursconf(a,b) desk_syscall(14,21,(a),(b),0,0)
#define Gettime() desk_syscall(14,23,0,0,0,0)
#define Kbrate(a,b) desk_syscall(14,35,(a),(b),0,0)
#define Dosound(a) desk_syscall(14,32,(a),0,0,0)
#define Setprt(a) desk_syscall(14,33,(a),0,0,0)
#define Blitmode(a) desk_syscall(14,64,(a),0,0,0)
#endif
