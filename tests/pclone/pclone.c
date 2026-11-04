/* User-mode Pclone checks through the real GEMDOS trap, not kernel calls. */
#include "test.h"
#include "../../include/pclone.h"

#if defined(TEST_WITH_PCLONE) && defined(TEST_WITH_USER_ASPACE) && (defined(__arm__) || defined(__mc68000__))
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
#else
    register long r0 __asm__("r0") = GEMDOS_PCLONE;
    register const void *r1 __asm__("r1") = args;
    register unsigned long r2 __asm__("r2") = size;
    __asm__ volatile ("svc 1" : "+r"(r0) : "r"(r1), "r"(r2)
                      : "r3", "r12", "lr", "memory", "cc");
    return r0;
#endif
}
static volatile int entry_ran;
static void child_entry(void *argument) { (void)argument; entry_ran = 1; }
#endif

void test_pclone(void);
void test_pclone(void)
{
    ptest_begin("pclone");
#if defined(TEST_WITH_PCLONE) && defined(TEST_WITH_USER_ASPACE) && (defined(__arm__) || defined(__mc68000__))
    {
        static unsigned char stack[256] __attribute__((aligned(16)));
        int32_t tid = 0x12345678;
        struct ptos_clone_args args = { 0 };
        args.entry = child_entry;
        args.stack_base = stack;
        args.stack_size = sizeof(stack);
        args.child_tid = &tid;
        ptest_assert_msg(pclone_trap(&args,sizeof(args)) == -32L,"valid request must reach unavailable backend");
        ptest_assert_msg(pclone_trap(0,sizeof(args)) == -40L,"null structure must fail");
        ptest_assert_msg(pclone_trap((void *)0xffff0000UL,sizeof(args)) == -40L,"inaccessible structure must fail");
        ptest_assert_msg(pclone_trap(&args,sizeof(args)-1) == -25L,"short structure must fail");
        ptest_assert_msg(pclone_trap((void *)0x80001000UL,sizeof(args)) == -40L,"high-bit pointer must not be truncated");
        ptest_assert_msg(pclone_trap(&args,0xffffffffUL) == -40L,"negative-looking size must not wrap");
        args.flags = 1;
        ptest_assert_msg(pclone_trap(&args,sizeof(args)) == -25L,"reserved flag must fail");
        args.flags = 0; args.entry = (void (*)(void *))0xffff0000UL;
        ptest_assert_msg(pclone_trap(&args,sizeof(args)) == -40L,"inaccessible entry must fail");
        args.entry = child_entry; args.child_tid = (int32_t *)0xffff0000UL;
        ptest_assert_msg(pclone_trap(&args,sizeof(args)) == -40L,"unwritable output must fail");
        ptest_assert_msg(tid == 0x12345678,"failure must not publish a child TID");
        ptest_assert_msg(entry_ran == 0,"no child entry may run");
    }
#endif
    ptest_pass();
}
