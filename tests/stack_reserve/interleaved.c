/* Exercise the real AES debug producer with two interleaved process stacks. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

typedef unsigned char UBYTE;
typedef unsigned long ULONG;
typedef struct {
    UBYTE u_super[2560];
    UBYTE *u_spsuper;
} UDA;
typedef struct {
    UDA *p_uda;
} PD;

static PD *rlr;

static void kprintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

#define CONF_DEBUG_AES_STACK 1
#define CONF_DEBUG_STACK_CI_END 0
#include "producer.inc"

int main(void)
{
    static UDA a, b;
    PD pa, pb;
    pa.p_uda = &a;
    pb.p_uda = &b;
    a.u_spsuper = a.u_super + sizeof(a.u_super) - 800;
    b.u_spsuper = b.u_super + sizeof(b.u_super) - 400;

    /* Establish cumulative used=1500, total=1900 in B. */
    rlr = &pb;
    trapaes_debug_enter();
    b.u_spsuper[-1500] = 0;
    trapaes_debug_exit();

    /* A reaches total=2200 but suspends before its exit. */
    rlr = &pa;
    trapaes_debug_enter();
    a.u_spsuper[-1400] = 0;

    /* B enters with only 100 bytes of additional use. Globals holding B's
     * bounds must not cause A's resumed exit to scan this smaller peak. */
    rlr = &pb;
    trapaes_debug_enter();
    b.u_spsuper[-100] = 0;

    rlr = &pa;
    trapaes_debug_exit();
    rlr = &pb;
    trapaes_debug_exit();

    printf("AES end seq=%lu\n", stack_trace_sequence);
    return 0;
}
