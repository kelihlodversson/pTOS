/* Compile for each user ABI: pointers in the public structure are native. */
#include <stddef.h>
#include "pclone.h"
#include "clone_args.h"

#define ASSERT(name, expr) typedef char assert_##name[(expr) ? 1 : -1]
ASSERT(user_pointer_width, sizeof(void *) == 4);
ASSERT(user_entry_width, sizeof(void (*)(void *)) == 4);
ASSERT(struct_size, sizeof(struct ptos_clone_args) == 32);
ASSERT(wire_size, sizeof(CLONE_USER_V1) == 32);
ASSERT(struct_alignment, __alignof__(struct ptos_clone_args) == __alignof__(uint32_t));
ASSERT(wire_alignment, __alignof__(CLONE_USER_V1) == __alignof__(struct ptos_clone_args));
ASSERT(pointer_alignment, __alignof__(void *) == __alignof__(uint32_t));
#ifdef EXPECTED_ALIGNMENT
ASSERT(target_alignment, __alignof__(struct ptos_clone_args) == EXPECTED_ALIGNMENT);
#endif
ASSERT(entry_type, __builtin_types_compatible_p(__typeof__(((CLONE_ARGS *)0)->entry), void (*)(void *)));
ASSERT(argument_type, __builtin_types_compatible_p(__typeof__(((CLONE_ARGS *)0)->argument), void *));
ASSERT(stack_type, __builtin_types_compatible_p(__typeof__(((CLONE_ARGS *)0)->stack_base), void *));
ASSERT(tls_type, __builtin_types_compatible_p(__typeof__(((CLONE_ARGS *)0)->tls), void *));
ASSERT(tid_type, __builtin_types_compatible_p(__typeof__(((CLONE_ARGS *)0)->child_tid), int32_t *));
#define MATCH(field, offset) ASSERT(offset_##field, offsetof(struct ptos_clone_args, field) == (offset) && offsetof(CLONE_USER_V1, field) == (offset))
MATCH(flags, 0);
MATCH(entry, 4);
MATCH(argument, 8);
MATCH(stack_base, 12);
MATCH(stack_size, 16);
MATCH(tls, 20);
MATCH(child_tid, 24);
MATCH(child_pid, 28);
ASSERT(prefix_size, PTOS_CLONE_ARGS_MIN_SIZE == 28);
