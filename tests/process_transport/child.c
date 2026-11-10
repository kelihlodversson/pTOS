/* Tiny real PROCESS payload; avoid recursively launching the test driver. */
#include <mint/osbind.h>
#include <mint/basepage.h>
int main(void)
{
    if (_base->p_cmdlin[0] && _base->p_cmdlin[1]=='F')
        *(volatile unsigned long *)0x100UL=0x1234UL;
    Pterm((short)0xffff);
    return 93;
}
