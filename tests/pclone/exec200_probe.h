/* Private Pclone test transport shared by the Pexec(200) native programs. */
#ifndef EXEC200_PROBE_H
#define EXEC200_PROBE_H
#include "../../include/pclone.h"
#include "../../include/exec_handoff_test.h"
#include <mint/osbind.h>

static __attribute__((unused)) long exec200_probe(const void *args, unsigned long tag)
{
#if defined(__mc68000__)
    register long result __asm__("d0");
    __asm__ volatile (
        "move.l %2,-(%%sp)\n\tmove.l %1,-(%%sp)\n\t"
        "move.w #0x190,-(%%sp)\n\ttrap #1\n\tlea 10(%%sp),%%sp"
        : "=d"(result) : "r"(args), "r"(tag)
        : "d1", "d2", "a0", "a1", "a2", "memory", "cc");
    return result;
#elif defined(__arm__)
    register long r0 __asm__("r0") = GEMDOS_PCLONE;
    register const void *r1 __asm__("r1") = args;
    register unsigned long r2 __asm__("r2") = tag;
    __asm__ volatile ("svc 1" : "+r"(r0) : "r"(r1), "r"(r2)
                      : "r3", "r12", "lr", "memory", "cc");
    return r0;
#else
    return trap_1_wll(GEMDOS_PCLONE, (long)args, (long)tag);
#endif
}
/* Pexec(200, path, tail, env), whatever the library's own mode checks. */
static __attribute__((unused)) long exec200_call(const char *path, const char *tail, const char *env)
{
    return Pexec(200, path, tail, env);
}
#endif
