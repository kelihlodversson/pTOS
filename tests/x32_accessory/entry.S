/* Capture the producer ABI before either libcmini startup can change it. */
        .text
        .globl acc_probe_entry
        .type acc_probe_entry,@function
acc_probe_entry:
        mov %rdi, acc_entry_base(%rip)
        mov %rsi, acc_entry_type(%rip)
        mov %rsp, acc_entry_stack(%rip)
        mov %cs, %ax
        mov %ax, acc_entry_cs(%rip)
        jmp _start
        .section .note.GNU-stack,"",@progbits
