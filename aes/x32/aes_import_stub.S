/* Link-time declarations only; Pexec supplies the real user-code entries. */
#include "aes_client_exports.h"
        .text
#define AES_STUB(index, name) .globl name; .type name, @function; name:; ud2; .size name, .-name;
AES_CLIENT_EXPORTS(AES_STUB)
        .section .note.GNU-stack,"",@progbits
