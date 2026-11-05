#include "emutos.h"
#include "gemerror.h"
#include "kheap.h"
#include "kaspace.h"
#include "thread_startup.h"
#if CONF_WITH_NATIVE_SCHED
#include "thread_startup_backend.h"
struct thread_startup_draft {
    KASPACE *space;
    THREAD_STARTUP_ARGS args;
    void *image;
};
#endif

LONG thread_startup_prepare(KASPACE *space, const THREAD_STARTUP_ARGS *args,
                            THREAD_STARTUP_DRAFT **result)
{
    if (!result || *result || !space || !args) return EINVAL;
#if CONF_WITH_NATIVE_SCHED
    {
        THREAD_STARTUP_DRAFT *draft;
        LONG error;
        error = thread_startup_arch_validate(kaspace_backend(space), args);
        if (error) return error;
        draft = kalloc(sizeof(*draft));
        if (!draft) return ENSMEM;
        draft->space = kaspace_share(space);
        if (!draft->space) { kfree(draft); return ENSMEM; }
        draft->args = *args;
        draft->image = NULL;
        error = thread_startup_arch_prepare(kaspace_backend(space), &draft->args, &draft->image);
        if (error) {
            kaspace_put(draft->space); kfree(draft); return error;
        }
        *result = draft;
        return E_OK;
    }
#else
    return EINVFN;
#endif
}

LONG thread_startup_validate(const THREAD_STARTUP_DRAFT *draft)
{
    if (!draft) return EINVAL;
#if CONF_WITH_NATIVE_SCHED
    return thread_startup_arch_validate(kaspace_backend(draft->space), &draft->args);
#else
    return EINVFN;
#endif
}

void thread_startup_abort(THREAD_STARTUP_DRAFT **result)
{
    if (!result || !*result) return;
#if CONF_WITH_NATIVE_SCHED
    {
        THREAD_STARTUP_DRAFT *draft = *result;
        *result = NULL;
        thread_startup_arch_abort(draft->image);
        kaspace_put(draft->space);
        kfree(draft);
    }
#endif
}

const void *thread_startup_image(const THREAD_STARTUP_DRAFT *draft)
{
#if CONF_WITH_NATIVE_SCHED
    return draft ? draft->image : NULL;
#else
    (void)draft;
    return NULL;
#endif
}
