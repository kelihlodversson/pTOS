/* Adversarial native participant: rejection bytes cannot assert success. */
#include "cuis.h"
#include "gemerror.h"
#include "gem_lifecycle.h"
#include <stdint.h>
static LONG native(ULONG function, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    register UQUAD ax __asm__("rax") = ((UQUAD)CUIS_SERVICE_CLASS << 32) | function;
    register UQUAD fourth __asm__("r10") = d, fifth __asm__("r8") = 0, sixth __asm__("r9") = 0;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"(a), "S"(b), "d"(c), "r"(fourth), "r"(fifth), "r"(sixth)
                      : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
int gem_lifecycle_retired_selectors(void)
{
    static const ULONG retired[]={66,67,68,69,70,79,80,81,82,83,84,85,86,87,90,91,92};
    LONG session=native(CUIS_SESSION_START,0,0,0,0);
    unsigned i;
    if (session<0) return 0;
    for (i=0;i<sizeof(retired)/sizeof(retired[0]);i++)
        if (native(retired[i],session,0,0,0)!=EINVFN) return 0;
    return 1;
}
int gem_lifecycle_false_success(char mode)
{
    short result[64] = { 60,0,0,0,0,0,0,0 }; /* SHUT_COMPLETED as opaque rejection. */
    unsigned bytes = 16;
    LONG session = native(CUIS_SESSION_START,0,0,0,0), token;
    if (mode != 'f') result[0] = 1234;
    if (mode == 's') bytes = 1;
    if (mode == 'o') bytes = 17;
    if (mode == 'l') bytes = sizeof(result);
    if (mode == 'e') { result[0] = 51; result[2] = 1; }
    if (session < 0) return 0;
    token = native(CUIS_STREAM_BATCH,session,CUIS_BATCH_COMMAND(CUIS_BATCH_QUERY,CUIS_BATCH_ID),0,0);
    return token > 0 && native(CUIS_STREAM_BATCH,session,CUIS_BATCH_COMMAND(CUIS_BATCH_SETTLE,token),(uintptr_t)result,bytes) == 0;
}
