/* User-mode BIOS bindings. GPL version 2 or later. */
#ifndef DESK_X32_BIOSBIND_H
#define DESK_X32_BIOSBIND_H
#include "syscall.h"
#define Bconstat(a) desk_syscall(13,1,(a),0,0,0)
#define Bconin(a) desk_syscall(13,2,(a),0,0,0)
#define Bconout(a,b) desk_syscall(13,3,(a),(b),0,0)
#define Bcostat(a) desk_syscall(13,8,(a),0,0,0)
#define Drvmap() desk_syscall(13,10,0,0,0,0)
#define Kbshift(a) desk_syscall(13,11,(a),0,0,0)
static __inline__ LONG Rwabs(WORD flags, long buffer, WORD count, WORD record, WORD drive, LONG large)
{
    LONG args[6];

    args[0] = flags; args[1] = buffer; args[2] = count;
    args[3] = record; args[4] = drive; args[5] = large;
    return desk_syscall(13, 4, (long)args, 0, 0, 0);
}
#endif
