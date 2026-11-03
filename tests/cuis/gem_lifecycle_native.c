/* Adversarial native participant: rejection bytes cannot assert success. */
#include "cuis.h"
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
int gem_lifecycle_false_success(void)
{
    short result[8] = { 60,0,0,0,0,0,0,0 }; /* SHUT_COMPLETED as opaque rejection. */
    LONG session = native(CUIS_SESSION_START,0,0,0,0), token;
    if (session < 0) return 0;
    token = native(CUIS_COHORT_QUERY,session,CUIS_COHORT_ID,0,0);
    return token > 0 && native(CUIS_COHORT_REJECT,session,token,(uintptr_t)result,16) == 0;
}
