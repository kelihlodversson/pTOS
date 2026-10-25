/* Process-entry contract: RDI = basepage, RSP + 8 aligned to 16 bytes. */
    .text
    .globl _start
    .type _start, @function
_start:
    xorl %ebp, %ebp
    jmp desk_user_start
    .size _start, . - _start
    .section .note.GNU-stack,"",@progbits
