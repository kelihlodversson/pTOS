/* Userland arguments for the existing private B2 test transport. */
#ifndef TEST_IRQ_RESTART_PROBE_H
#define TEST_IRQ_RESTART_PROBE_H
#include <stdint.h>

#define IRT_BEGIN 0xc713b200UL
#define IRT_FINISH 0xc713b201UL
#define IRT_ABORT 0xc713b202UL
#define IRT_CAPTURE_ONLY 0xc713b203UL
#define IRT_QUERY 0xc713b204UL
#define IRT_NO_CREDIT 0xc713b205UL
#define IRT_FAIL_CODE 0xc713b211UL
#define IRT_FAIL_STACK 0xc713b212UL
#define IRT_EMPTY 0
#define IRT_ACKED 5

struct irq_restart_args { uint64_t code, code_bytes, stack, stack_bytes; };
extern char clone_irq_window_begin[],clone_irq_window_end[];
extern char clone_irq_window_alt_begin[],clone_irq_window_alt_end[];

static void irq_restart_prepare_args(struct irq_restart_args *args, uintptr_t code, uintptr_t end)
{
    uintptr_t page=(uintptr_t)args & ~(uintptr_t)4095;
    uintptr_t touch;
    args->code=code;
    args->code_bytes=end-code;
    args->stack=page-8192;
    args->stack_bytes=12288;
    /* Make lazy executable and stack pages resident in USER context. The
     * prepare/IRQ paths must never resolve a demand or private COW fault. */
    for(touch=code&~(uintptr_t)4095;touch<end;touch+=4096)
        (void)*(volatile const unsigned char *)touch;
    for(touch=page-8192;touch<=page;touch+=4096) {
        volatile unsigned char *byte=(volatile unsigned char *)touch;
        unsigned char value=*byte;
        *byte=value;
    }
}
#endif
