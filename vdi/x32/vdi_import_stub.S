/* Link-time type/name declaration. Pexec binds vdi to the user client. */
        .text
        .globl vdi
        .type vdi, @function
vdi:
        ud2
        .size vdi, .-vdi
        .section .note.GNU-stack,"",@progbits
