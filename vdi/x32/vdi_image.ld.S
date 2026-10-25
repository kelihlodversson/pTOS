/* The first word names the typed entry. No mutable global state is allowed. */
#define __ASSEMBLER__ 1
#include "x32vdi_image.h"
OUTPUT_FORMAT(elf32-x86-64)
ENTRY(vdi)
SECTIONS
{
    . = X32_VDI_IMAGE_BASE;
    .image : {
        LONG(vdi)
        *(.text*)
        *(.rodata*)
    }
    .data : { *(.data*) *(.bss*) *(COMMON) }
    ASSERT(SIZEOF(.data) == 0, "VDI client must not contain mutable globals")
    ASSERT(SIZEOF(.image) <= X32_VDI_IMAGE_SIZE, "VDI client exceeds reserved window")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
