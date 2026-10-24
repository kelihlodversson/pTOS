/* Enter a callback with only typed arguments and zeroed scratch/FPU state. */
#include "asmdefs.h"
#include "../../../bios/arch/x86_64/gdt.h"

        .text
        .globl C_SYM(x86_64_callback_enter)
C_SYM(x86_64_callback_enter):
        /* rdi=handler, rsi=stack, edx=a, ecx=b. Caller selected CR3/GS. */
        pushq $X86_64_USER_DATA_SEL
        pushq %rsi
        pushq $0x202
        pushq $X86_64_USER_CODE_SEL
        pushq %rdi
        fxrstor (%r8)
        movl %edx, %edi
        movl %ecx, %esi
        xorl %eax, %eax
        xorl %ebx, %ebx
        xorl %ecx, %ecx
        xorl %edx, %edx
        xorl %ebp, %ebp
        xorl %r8d, %r8d
        xorl %r9d, %r9d
        xorl %r10d, %r10d
        xorl %r11d, %r11d
        xorl %r12d, %r12d
        xorl %r13d, %r13d
        xorl %r14d, %r14d
        xorl %r15d, %r15d
        iretq
