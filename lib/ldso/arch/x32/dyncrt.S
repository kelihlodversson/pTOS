/* Standard ELF CRT: keep the kernel stack and pass the loader's RDX callback. */
        .text
        .globl _start
        .type _start,@function
        .hidden main
_start:
        movl %edx, %esi
        movl %esp, %edi
        leal main(%rip), %edx
        call __ptos_start@PLT
        ud2
        .size _start,.-_start
        .section .note.GNU-stack,"",@progbits
