/* Separate toolchain-built ET_DYN files, not kernel-linked library code. */
        .section .rodata.ldso_samples,"a",@progbits
        .balign 4
        .globl dl_test_leaf_start, dl_test_leaf_end
dl_test_leaf_start:
        .incbin "obj/ldso/test/libleaf.so"
dl_test_leaf_end:
        .balign 4
        .globl dl_test_library_start, dl_test_library_end
dl_test_library_start:
        .incbin "obj/ldso/test/libtest.so"
dl_test_library_end:
        .section .note.GNU-stack,"",@progbits
