/* Capture the process-entry ABI before the C prologue. */
    .text
    .globl _start
_start:
    movq %rdi, entry_base(%rip)
    movq %rsi, entry_type(%rip)
    movq %rsp, entry_stack(%rip)
    movw %cs, entry_cs(%rip)
    jmp desktop_test_start
    .section .note.GNU-stack,"",@progbits
