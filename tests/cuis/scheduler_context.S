/* The kernel's private continuation ABI, under fixture-local symbol names.
 * Do not replace libc longjmp or disable hosted fortification: libc rejects
 * switching into a kernel-owned BSS stack, which is not a hosted C stack. */
#define setjmp cuis_root_setjmp
#define longjmp cuis_root_longjmp
#include "../../util/arch/x86_64/setjmp.S"
        .section .note.GNU-stack,"",@progbits
