#define __ASSEMBLER__ 1
#include "x32aes_image.h"
OUTPUT_FORMAT(elf32-x86-64)
SECTIONS
{
    . = X32_AES_IMAGE_BASE;
    .image : {
        LONG(X32_AES_IMAGE_MAGIC)
        LONG(AES_CLIENT_EXPORT_COUNT)
#define AES_ENTRY(index, name) LONG(name)
        AES_CLIENT_EXPORTS(AES_ENTRY)
        *(.text*) *(.rodata*)
    }
    .data : { *(.data*) *(.bss*) *(COMMON) }
    ASSERT(SIZEOF(.data) == 0, "AES client must not contain mutable globals")
    ASSERT(SIZEOF(.image) <= X32_AES_IMAGE_SIZE, "AES client exceeds reserved window")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
