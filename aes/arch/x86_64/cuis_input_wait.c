/* Atomic check/sleep backend, selected only by the x32 CUIS Kconfig option. */
#include "emutos.h"
#include "../../cuis_input.h"
#include "tosvars.h"
#include "lineavars.h"
#include "../../../bios/bios.h"
#include "../../../vdi/vdi_defs.h"
#if CONF_WITH_X86_64_MEMTEST
#include "../../../bios/machine/pc-x86_64/pc_x86_64_memtest.h"
void cuis_input_test_wait(ULONG events) { x86_64_memtest_cuis_wait(events); }
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

BOOL cuis_input_standard_pointer(void) { return kbdvecs.mousevec == mouse_int; }
BOOL cuis_input_pointer_state(LONG *x, LONG *y, ULONG *buttons)
{
    if (!cuis_input_standard_pointer()) return FALSE;
    *x = linea_vars.GCURX; *y = linea_vars.GCURY;
    *buttons = (ULONG)linea_vars.MOUSE_BT & 3;
    return TRUE;
}
void cuis_input_call_pointer(SBYTE *packet) { kbdvecs.mousevec((UBYTE *)packet); }

void cuis_input_clip_pointer(LONG *x, LONG *y)
{
    LONG right = xres < 0 ? 0 : xres, bottom = yres < 0 ? 0 : yres;
    *x = *x < 0 ? 0 : *x > right ? right : *x;
    *y = *y < 0 ? 0 : *y > bottom ? bottom : *y;
}
