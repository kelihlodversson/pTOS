/* Compile for each user ABI: pointers in the public structure are native. */
#include <stddef.h>
#include "pclone.h"
#include "clone_args.h"

#define ASSERT(name, expr) typedef char assert_##name[(expr) ? 1 : -1]
ASSERT(user_pointer_width, sizeof(void *) == 4);
ASSERT(user_entry_width, sizeof(void (*)(void *)) == 4);
ASSERT(struct_size, sizeof(struct ptos_clone_args) == 28);
ASSERT(wire_size, sizeof(CLONE_USER_V1) == 28);
ASSERT(entry_type, __builtin_types_compatible_p(__typeof__(((CLONE_ARGS *)0)->entry), void (*)(void *)));
ASSERT(argument_type, __builtin_types_compatible_p(__typeof__(((CLONE_ARGS *)0)->argument), void *));
ASSERT(stack_type, __builtin_types_compatible_p(__typeof__(((CLONE_ARGS *)0)->stack_base), void *));
ASSERT(tls_type, __builtin_types_compatible_p(__typeof__(((CLONE_ARGS *)0)->tls), void *));
ASSERT(tid_type, __builtin_types_compatible_p(__typeof__(((CLONE_ARGS *)0)->child_tid), int32_t *));
#define MATCH(field) ASSERT(offset_##field, offsetof(struct ptos_clone_args, field) == offsetof(CLONE_USER_V1, field))
MATCH(flags);
MATCH(entry);
MATCH(argument);
MATCH(stack_base);
MATCH(stack_size);
MATCH(tls);
MATCH(child_tid);
