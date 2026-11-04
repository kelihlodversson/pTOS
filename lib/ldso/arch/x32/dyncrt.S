/* Standard ELF CRT: keep the kernel stack and pass the loader's RDX callback. */
        .text
        .globl _start
        .type _start,@function
        .hidden main
_start:
#ifdef PTOS_ACCESSORY_CRT
        movw $0, _app(%rip)
#endif
        movl %edx, %esi
        movl %esp, %edi
        leal main(%rip), %edx
        call __ptos_start@PLT
        ud2
        .size _start,.-_start
        /* Source-compatible GEM startup state. A linked C runtime may
         * provide its own strong definition; accessory entry still sets it. */
        .data
        .weak _app
        .hidden _app
        .type _app,@object
        .size _app,2
_app:
#ifdef PTOS_ACCESSORY_CRT
        /* The interpreter invokes initialization arrays before _start. */
        .short 0
#else
        .short 1
#endif
        .section .note.GNU-stack,"",@progbits
