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
        /* The interpreter calls executable preinitializers with argc/argv/envp.
         * Set the executable's role before its initializers can inspect it. */
.Lentry_type:
        subq $8, %rsp
        movl %edx, %edi
        call __ptos_is_application@PLT
        movw %ax, _app(%rip)
        addq $8, %rsp
        ret
        .section .preinit_array,"aw",@preinit_array
        .balign 4
        .long .Lentry_type
        /* A linked C runtime may supply a strong definition. */
        .data
        .weak _app
        .hidden _app
        .type _app,@object
        .size _app,2
_app:
        .short 1
        .section .note.GNU-stack,"",@progbits
