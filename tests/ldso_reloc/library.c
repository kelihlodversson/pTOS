/* PIC data, bss, GOT/PLT calls and a relocated constructor pointer. */
extern int leaf(void);
int value = 5;
int scratch;

int answer(void)
{
    return leaf() + value + scratch;
}

__attribute__((constructor)) static void init(void)
{
    value = 9;
}
