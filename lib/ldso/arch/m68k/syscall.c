/*
 * GEMDOS transport for the m68k (trap #1), independent of any libc (#694).
 *
 * The frame on the stack is packed by the widths of the parameters, as the
 * calls are declared: a WORD takes one word and a LONG or a pointer two.  The
 * common code passes every argument as a scalar, so the shape of each call
 * that the interpreter and its tests make is named here ('w' a WORD, 'l' a
 * LONG); the calls of the mapping family (Mmap, Munmap, Mprot) are longs
 * throughout, and so is whatever is not listed.  The kernel restores every
 * register but d0 on the way back.
 */
#include "runtime.h"

static const char *shape(uint32_t fn)
{
    switch (fn) {
    case 0x09: return "l";              /* Cconws (string) */
    case 0x3d: return "lw";             /* Fopen (name, mode) */
    case 0x3e: return "w";              /* Fclose (handle) */
    case 0x42: return "lww";            /* Fseek (offset, handle, mode) */
    case 0x4b: return "wlll";           /* Pexec (mode, name, tail, environment) */
    case 0x4c: return "w";              /* Pterm (code) */
    case 0x154: return "wll";           /* Ssystem (mode, arg1, arg2) */
    case 0x17d: return "llw";           /* Maccess (address, length, mode) */
    default: return "llllll";
    }
}

int64_t dl_syscall(uint32_t fn, uint64_t a, uint64_t b, uint64_t c,
                    uint64_t d, uint64_t e, uint64_t f)
{
    register uint32_t d0 __asm__("d0");
    uint16_t w[13] = { 0 };             /* fn, and the most a call takes: six longs */
    const uint64_t args[6] = { a, b, c, d, e, f };
    const char *s = shape(fn);
    uint32_t i, n = 0;

    w[n++] = (uint16_t)fn;
    for (i = 0; s[i] && i < 6; i++) {
        uint32_t v = (uint32_t)args[i];

        if (s[i] == 'l')
            w[n++] = (uint16_t)(v >> 16);
        w[n++] = (uint16_t)v;
    }
    __asm__ volatile (
        "lea 26(%1),%%a0\n\t"
        "move.w -(%%a0),-(%%sp)\n\t" "move.w -(%%a0),-(%%sp)\n\t" "move.w -(%%a0),-(%%sp)\n\t"
        "move.w -(%%a0),-(%%sp)\n\t" "move.w -(%%a0),-(%%sp)\n\t" "move.w -(%%a0),-(%%sp)\n\t"
        "move.w -(%%a0),-(%%sp)\n\t" "move.w -(%%a0),-(%%sp)\n\t" "move.w -(%%a0),-(%%sp)\n\t"
        "move.w -(%%a0),-(%%sp)\n\t" "move.w -(%%a0),-(%%sp)\n\t" "move.w -(%%a0),-(%%sp)\n\t"
        "move.w -(%%a0),-(%%sp)\n\t"
        "trap #1\n\t"
        "lea 26(%%sp),%%sp"
        : "=d" (d0)
        : "a" (w)
        : "a0", "memory", "cc");
    return (int64_t)(int32_t)d0;
}
