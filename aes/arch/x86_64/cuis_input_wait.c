/* Atomic check/sleep backend, selected only by the x32 CUIS Kconfig option. */
#include "emutos.h"
#include "../../cuis_input.h"
#include "tosvars.h"
#if CONF_WITH_X86_64_MEMTEST
#include "../../../bios/machine/pc-x86_64/pc_x86_64_memtest.h"
void cuis_input_test_wait(void) { x86_64_memtest_cuis_wait(); }
#endif
UQUAD cuis_input_lock(void)
{
    UQUAD flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
}
void cuis_input_unlock(UQUAD flags)
{
    if (flags & 0x200) __asm__ volatile ("sti" ::: "memory");
    else __asm__ volatile ("cli" ::: "memory");
}
void cuis_input_idle(void)
{
    /* STI's interrupt shadow covers HLT: a pending key cannot arrive after
     * the predicate check yet before sleep and leave this waiter stranded. */
    __asm__ volatile ("sti; hlt; cli" ::: "memory");
}
ULONG cuis_input_ticks(void) { return (ULONG)hz_200; }
