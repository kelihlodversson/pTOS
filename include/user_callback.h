/* Typed x32 callback ABI. All addresses in this interface are user addresses. */
#ifndef USER_CALLBACK_H
#define USER_CALLBACK_H

#define USER_CALLBACK_CLASS 0x102
#define USER_CALLBACK_RETURN 0
#define USER_CALLBACK_CHAIN 1
#define USER_CALLBACK_ALTSTACK 2
#define USER_CALLBACK_IMAGE 0x0ffc0000
#define USER_CALLBACK_STUBS 0x100
#define USER_CALLBACK_STUB_SIZE 32
#define USER_CALLBACK_CAPS 64
#define USER_CALLBACK_DEPTH 8
#define USER_CALLBACK_MIN_STACK 16384
#define USER_CALLBACK_SS_ONSTACK 1
#define USER_CALLBACK_SS_DISABLE 2

#ifndef __ASSEMBLER__
#include "portab.h"

/* ss_sp is the bottom of the allocation; ss_size is its length. */
typedef struct {
    ULONG ss_sp, ss_size, ss_flags;
} USER_CALLBACK_STACK;
typedef char user_callback_stack_size[(sizeof(USER_CALLBACK_STACK) == 12) ? 1 : -1];
typedef char user_callback_stack_flags[(offsetof(USER_CALLBACK_STACK, ss_flags) == 8) ? 1 : -1];

#if defined(__ILP32__) && defined(__x86_64__)
static __inline__ LONG Psigaltstack(const USER_CALLBACK_STACK *stack,
                                   USER_CALLBACK_STACK *old)
{
    UQUAD result = ((UQUAD)USER_CALLBACK_CLASS << 32) | USER_CALLBACK_ALTSTACK;
    __asm__ volatile ("syscall" : "+a"(result)
                      : "D"((UQUAD)(ULONG)stack), "S"((UQUAD)(ULONG)old)
                      : "rcx", "r11", "memory", "cc");
    return (LONG)result;
}
#endif
#endif
#endif
