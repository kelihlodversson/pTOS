/* Pclone GEMDOS entry. The backend is not available yet. */
#include "emutos.h"
#include "gemerror.h"
#include "clone.h"
#if CONF_WITH_USER_ASPACE
#include "fs.h"
#include "bdosstub.h"
#include "kproc.h"
#include "clone_args.h"

static int clone_copy(void *context, uint64_t address, void *dst, uint32_t size)
{
    (void)context;
    return kproc_copy_from_user(dst, (VADDR)address, size);
}

static int clone_access(void *context, uintptr_t address, uint32_t size, int kind)
{
    (void)context;
    if (kind == CLONE_ACCESS_EXEC) return kproc_validate_user_exec((VADDR)address);
    return kproc_user_access(run, (VADDR)address, size, TRUE);
}
#endif

long xclone(uint64_t address, uint64_t size)
{
#if CONF_WITH_USER_ASPACE
    CLONE_ARGS args;
    enum clone_copy_result copied;
    enum clone_validate_result valid;
    static const CLONE_ABI abi = { CONF_PCLONE_STACK_ALIGNMENT,
        CONF_PCLONE_MIN_STACK, CONF_PCLONE_ENTRY_ALIGNMENT };
    copied = clone_copy_v1(&args, address, size, clone_copy, NULL);
    if (copied == CLONE_COPY_BAD_SIZE) return EINVAL;
    if (copied != CLONE_COPY_OK) return EIMBA;
    valid = clone_validate_v1(&args, &abi, clone_access, NULL);
    if (valid == CLONE_INVALID) return EINVAL;
    if (valid != CLONE_VALID) return EIMBA;
#else
    (void)address;
    (void)size;
#endif
    /* The execution-context backend is not implemented yet. */
    return EINVFN;
}
