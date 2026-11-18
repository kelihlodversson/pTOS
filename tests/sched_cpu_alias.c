/* Real CPU storage and frozen run ABI; CPU switching is not executed here. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "cuis/scheduler_irq_model.h"
#include "../bdos/native_sched.c"

extern PD *alias_abi_exchange(PD *replacement, PD ***slot);
void panic(const char *message, ...) { (void)message;abort(); }
int main(void)
{
    PD first={0},second={0};
    PD **slot=NULL;
    /* Inspect physical storage as the OSHEADER/assembly consumers do. C does
     * not know that a linker-defined address aliases another C object's
     * address; do not ask its distinct-object optimizer to prove equality. */
    volatile uintptr_t address=(uintptr_t)&execution_cpu;
    PD * volatile *loaded=(PD * volatile *)(uintptr_t)address;
    execution_cpu.number=17;
    *loaded=&first;
    if(alias_abi_exchange(&second,&slot)!=&first ||
       (uintptr_t)slot!=address || *loaded!=&second ||
       execution_cpu.number!=17) abort();
    /* Also touch the far end: a pointer-sized overlapping ASan alias used to
     * poison these fields despite the CPU object itself being fully live. */
    execution_cpu.stack[ROOT_STACK_BYTES/sizeof(ULONG)-1]=0x713;
    if(execution_cpu.stack[ROOT_STACK_BYTES/sizeof(ULONG)-1]!=0x713) abort();
    puts("CPU-owned run ABI: one storage address, neighboring fields and final stack word: PASS");
    return 0;
}
