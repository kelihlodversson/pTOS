/* Tiny real PROCESS payload; avoid recursively launching the test driver. */
#include "../entry_args.h"
#include <mint/osbind.h>
#include <mint/basepage.h>
int main(int argc, char **argv)
{
    if (test_entry_argument(argc, argv)[0] == 'F')
        *(volatile unsigned long *)0x100UL=0x1234UL;
    Pterm((short)0xffff);
    return 93;
}
