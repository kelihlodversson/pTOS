/* Private representation of the Pclone v1 user ABI. */
#ifndef CLONE_ARGS_H
#define CLONE_ARGS_H

#include <stdint.h>

/* The kernel must copy this native-endian 32-bit user layout, not a kernel
 * instance of struct ptos_clone_args, from the calling address space. */
typedef struct {
    uint32_t flags, entry, argument, stack_base, stack_size, tls, child_tid;
} CLONE_USER_V1;

/* Native pointer types describe user addresses. They are untrusted and must
 * never be dereferenced as kernel pointers without validation. */
typedef struct {
    uint32_t flags, stack_size;
    void (*entry)(void *);
    void *argument, *stack_base, *tls;
    int32_t *child_tid;
} CLONE_ARGS;

/* Decode a kernel-owned snapshot. This does not validate or retain user memory. */
void clone_decode_v1(CLONE_ARGS *out, const CLONE_USER_V1 *snapshot);

enum clone_copy_result {
    CLONE_COPY_OK, CLONE_COPY_BAD_SIZE, CLONE_COPY_BAD_ADDRESS
};

/* copy reads from the caller's address space. The output is published only
 * after the known prefix has been copied. Larger structures may append fields. */
enum clone_copy_result clone_copy_v1(CLONE_ARGS *out, uint64_t address,
                                    uint64_t size,
                                    int (*copy)(void *, uint64_t, void *, uint32_t),
                                    void *context);

#endif
