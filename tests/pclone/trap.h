/* Existing architecture-native Pclone test transport. */
#ifndef TEST_PCLONE_TRAP_H
#define TEST_PCLONE_TRAP_H
static long pclone_trap(const void *args, unsigned long size)
{
#if defined(__mc68000__)
    register long result __asm__("d0");
    __asm__ volatile (
        "move.l %2,-(%%sp)\n\t"
        "move.l %1,-(%%sp)\n\t"
        "move.w #0x190,-(%%sp)\n\t"
        "trap #1\n\t"
        "lea 10(%%sp),%%sp"
        : "=d"(result) : "r"(args), "r"(size)
        : "d1", "d2", "a0", "a1", "a2", "memory", "cc");
    return result;
#elif defined(__arm__)
    register long r0 __asm__("r0") = GEMDOS_PCLONE;
    register const void *r1 __asm__("r1") = args;
    register unsigned long r2 __asm__("r2") = size;
    __asm__ volatile ("svc 1" : "+r"(r0) : "r"(r1), "r"(r2)
                      : "r3", "r12", "lr", "memory", "cc");
    return r0;
#else
    return trap_1_wll(GEMDOS_PCLONE,(long)args,(long)size);
#endif
}
/* #1015: a managed THREAD's Pexec(0) child is an anchor. The regression
 * kernels can keep it on the nested carrier, the launcher continuation the
 * nested suites test; PCLONE_NESTED_CARRIER says whether they can. */
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
#define PCLONE_NESTED_CARRIER 1
static inline void pclone_nest_synchronous(int on)
{
    (void)pclone_trap(0, on ? 0xc8920722UL : 0xc8920723UL);
}
#else
#define PCLONE_NESTED_CARRIER 0
static inline void pclone_nest_synchronous(int on) { (void)on; }
#endif
#endif
