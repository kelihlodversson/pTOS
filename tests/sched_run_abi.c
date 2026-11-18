/* Separate translation unit, as the frozen BIOS/GEMDOS run consumers are. */
#include "emutos.h"
#include "bdosdefs.h"
extern PD *run;
PD *alias_abi_exchange(PD *replacement, PD ***slot);
PD *alias_abi_exchange(PD *replacement, PD ***slot)
{
    PD *previous=run;
    *slot=&run;
    run=replacement;
    return previous;
}
