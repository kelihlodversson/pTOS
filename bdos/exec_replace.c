/* Executable-replacement admission, using existing PROCESS and cleanup owners.
 * No entry or preparation step can publish a successful exec event. */
#include "emutos.h"
#include "gemerror.h"
#include "kheap.h"
#include "biosext.h"
#include "kproc.h"
#include "kthread.h"
#include "kaspace.h"
#include "ksched.h"
#include "exec_replace.h"
#include "string.h"
extern PD *run;

struct kproc_exec_replacement {
    KPROC *process;
    KPROC *image; /* existing unstarted loader container, never a new execution */
    KTHREAD *thread; /* borrowed from the protected current execution */
    KASPACE *source, *selected;
    UQUAD process_number, execution, ticket;
    ULONG tid;
    KSCHED_CLEANUP_SCOPE cleanup;
};

static UWORD fail_stage;
void kproc_exec_replacement_test_fail_after(UWORD stage) { fail_stage=stage; }
static BOOL injected(UWORD stage)
{
    if (fail_stage!=stage) return FALSE;
    fail_stage=0;return TRUE;
}

LONG kproc_exec_replacement_validate(const KPROC_EXEC_REPLACEMENT *replacement)
{
    PD *controller;
    if (!replacement || !replacement->cleanup.active ||
        replacement->execution != ksched_context_id() ||
        replacement->process_number != kproc_ref_number(replacement->process) ||
        replacement->tid != kthread_tid(replacement->thread) ||
        ksched_exit_thread(run) != replacement->thread ||
        kthread_process(replacement->thread) != replacement->process ||
        kthread_aspace(replacement->thread) != replacement->source ||
        !kthread_is_executing(replacement->thread) ||
        ksched_cancellation_pending() ||
        !ksched_clone_context(replacement->process, &controller) ||
        !kproc_exec_replacement_matches(replacement->process, replacement->thread,
                    replacement->source, replacement->selected,replacement->image,replacement->ticket))
        return EACCDN;
    return E_OK;
}

void kproc_exec_replacement_abort(KPROC_EXEC_REPLACEMENT **handle)
{
    KPROC_EXEC_REPLACEMENT *replacement;
    if (!handle || !*handle) return;
    replacement = *handle;
    if (!replacement->cleanup.active || replacement->execution != ksched_context_id() ||
        ksched_exit_thread(run) != replacement->thread)
        panic("exec replacement: abort outside owning execution");
    *handle = NULL;
    /* Keep the reversible admission closed through every root destructor.
     * The caller still owns the selected root; releasing our reference cannot
     * consume the caller's loader image or its rollback rights. */
    if (replacement->image) {
        proc_dispose_process(replacement->image);
        kproc_release(replacement->image);
    }
    kaspace_put(replacement->selected);
    kaspace_put(replacement->source);
    kproc_exec_replacement_drop(replacement->process, replacement->ticket);
    kproc_release(replacement->process);
    ksched_cleanup_end(&replacement->cleanup);
    kfree(replacement);
}

LONG kproc_exec_replacement_prepare(KPROC *source, KTHREAD *thread, KASPACE *selected,
                                  KPROC_EXEC_REPLACEMENT **out)
{
    KPROC_EXEC_REPLACEMENT *replacement;
    KASPACE *original;
    PD *controller;
    LONG error;
    if (!out || *out || !source || !thread || !selected) return EINVAL;
    original = kproc_active_space(source);
    if (!original || original == selected || !ksched_context_id() ||
        ksched_exit_thread(run) != thread || kthread_process(thread) != source ||
        kthread_aspace(thread) != original || !kthread_is_executing(thread) ||
        !kproc_thread_is_sole(source, thread) || ksched_cancellation_pending() ||
        !ksched_clone_context(source, &controller)) return EACCDN;
    replacement = kalloc(sizeof(*replacement));
    if (!replacement) return ENSMEM;
    memset(replacement, 0, sizeof(*replacement));
    ksched_cleanup_begin(&replacement->cleanup);
    replacement->thread = thread;
    replacement->execution = ksched_context_id();
    replacement->process_number = kproc_ref_number(source);
    replacement->tid = kthread_tid(thread);
    error = kproc_exec_replacement_claim(source, thread, selected, &replacement->ticket);
    if (error) goto failed;
    replacement->process = kproc_retain_live(source);
    if (!replacement->process) { error = ENSMEM; goto failed; }
    if (injected(1)) { error=ENSMEM;goto failed; }
    replacement->source = kaspace_share(original);
    if (injected(2)) { error=ENSMEM;goto failed; }
    replacement->selected = kaspace_share(selected);
    if (!replacement->source || !replacement->selected) { error = ENSMEM; goto failed; }
    if (injected(3)) { error=ENSMEM;goto failed; }
    error = kproc_exec_replacement_validate(replacement);
    if (error) goto failed;
    if (injected(4)) { error=ENSMEM;goto failed; }
    *out = replacement;
    return E_OK;
failed:
    kaspace_put(replacement->selected);
    kaspace_put(replacement->source);
    if (replacement->ticket) kproc_exec_replacement_drop(source, replacement->ticket);
    kproc_release(replacement->process);
    ksched_cleanup_end(&replacement->cleanup);
    kfree(replacement);
    return error;
}

LONG kproc_exec_replacement_load_begin(KPROC_EXEC_REPLACEMENT *replacement,KPROC *image)
{
    KPROC *owned;
    LONG error;
    if (kproc_exec_replacement_validate(replacement) || replacement->image ||
        !image || image==replacement->process) return EACCDN;
    owned=kproc_retain_live(image);
    if (!owned) return ENSMEM;
    error=kproc_exec_image_select(owned,replacement->selected);
    if (error) { kproc_release(owned);return error; }
    replacement->image=owned;
    return E_OK;
}

BOOL kproc_exec_replacement_image_ready(const KPROC_EXEC_REPLACEMENT *replacement)
{
    return replacement && !kproc_exec_replacement_validate(replacement) && replacement->image &&
        kproc_exec_image_pending(replacement->image,replacement->selected,TRUE);
}

LONG kproc_exec_replacement_probe(UWORD fail_after)
{
    KPROC *source=kproc_retain(run);
    KTHREAD *thread=ksched_exit_thread(run);
    KPROC_EXEC_REPLACEMENT *replacement=NULL,*competing=NULL;
    KASPACE *selected=NULL,*original;
    ASPACE *backend;
    UQUAD number,execution;
    ULONG tid;
    WORD pid;
    BOOL protected;
    LONG error=EINVFN;
    if (!source || !thread || (fail_after>4 && (fail_after<0x100 || fail_after>0x102))) goto done;
    original=kproc_active_space(source);
    number=kproc_ref_number(source);pid=kproc_ref_pid(source);tid=kthread_tid(thread);
    execution=ksched_context_id();protected=ksched_cleanup_active();
    backend=aspace_create();
    if (!backend) { error=ENSMEM;goto done; }
    selected=kaspace_adopt(backend);
    if (!selected) { aspace_destroy(backend);error=ENSMEM;goto done; }
    kproc_exec_replacement_test_fail_after(fail_after<=4 ? fail_after : 0);
    error=kproc_exec_replacement_prepare(source,thread,selected,&replacement);
    kproc_exec_replacement_test_fail_after(0);
    if (!error) {
        if (kproc_exec_replacement_prepare(source,thread,selected,&competing)!=EACCDN || competing)
            panic("exec replacement: admitted competing staging");
        /* Existing scheduler saves/restores this same kernel continuation;
         * staging is neither a blocking lock nor a fabricated replacement. */
        ksched_yield();
        if (kproc_exec_replacement_validate(replacement))
            panic("exec replacement: staging lost executing owner across yield");
        if (fail_after>=0x100) {
            char environment[]="STAGE=970\0";
            char *path=fail_after==0x100 ? "C:\\PIEPROBE.TOS" :
                       fail_after==0x101 ? "C:\\NO970.TOS" : "C:\\LOADFAIL.TOS";
            error=proc_exec_replacement_stage(replacement,path,"",environment);
            if ((fail_after==0x100 && error==E_OK &&
                    !kproc_exec_replacement_image_ready(replacement)) ||
                (fail_after!=0x100 && error==E_OK))
                panic("exec replacement: loader staging readiness mismatch");
        }
    }
    kproc_exec_replacement_abort(&replacement);
    if (kproc_ref_number(source)!=number || kproc_ref_pid(source)!=pid ||
        kthread_tid(thread)!=tid || ksched_exit_thread(run)!=thread ||
        ksched_context_id()!=execution || kproc_active_space(source)!=original ||
        kthread_aspace(thread)!=original || !kthread_is_executing(thread) ||
        ksched_cleanup_active()!=protected || kproc_ref_terminal(source))
        panic("exec replacement: aborted staging changed execution or completion");
done:
    kproc_exec_replacement_abort(&replacement);
    kaspace_put(selected);kproc_release(source);
    return error;
}
