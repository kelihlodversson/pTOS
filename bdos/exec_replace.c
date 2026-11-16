/* Executable-replacement admission, using existing PROCESS and cleanup owners.
 * No entry or preparation step can publish a successful exec event. */
#include "emutos.h"
#include "gemerror.h"
#include "kheap.h"
#include "biosext.h"
#include "kproc.h"
#include "proc_clone.h"
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
    BOOL transferred; /* past the no-fail Pexec(200) boundary: retire only */
};

#if CONF_WITH_EXEC_REPLACEMENT_TEST
static UWORD fail_stage;
void kproc_exec_replacement_test_fail_after(UWORD stage) { fail_stage=stage; }
static BOOL injected(UWORD stage)
{
    if (fail_stage!=stage) return FALSE;
    fail_stage=0;return TRUE;
}
#else
#define injected(stage) FALSE
#endif

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
    if (replacement->transferred) panic("exec replacement: abort after transfer");
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
    error=kproc_exec_image_select(owned,replacement->selected,kproc_live_pd(replacement->process));
    if (error) { kproc_release(owned);return error; }
    replacement->image=owned;
    return E_OK;
}

BOOL kproc_exec_replacement_image_ready(const KPROC_EXEC_REPLACEMENT *replacement)
{
    return replacement && !kproc_exec_replacement_validate(replacement) && replacement->image &&
        kproc_exec_image_pending(replacement->image,replacement->selected,TRUE);
}

KPROC *kproc_exec_replacement_image(const KPROC_EXEC_REPLACEMENT *replacement)
{
    return replacement && !replacement->transferred ? replacement->image : NULL;
}
KASPACE *kproc_exec_replacement_selected(const KPROC_EXEC_REPLACEMENT *replacement)
{
    return replacement && !replacement->transferred ? replacement->selected : NULL;
}

/* The no-fail boundary of ksched_exec_replace(), after its own nested scope
 * ended: cancellation protection ends with the old continuation. */
void kproc_exec_replacement_transfer(KPROC_EXEC_REPLACEMENT *replacement)
{
    if (kproc_exec_replacement_validate(replacement) || replacement->transferred)
        panic("exec replacement: unauthenticated transfer");
    ksched_cleanup_end(&replacement->cleanup);
    replacement->transferred = TRUE;
}
/* Quiescent scheduler root: the admission closes with the commit. */
void kproc_exec_replacement_commit(KPROC_EXEC_REPLACEMENT *replacement)
{
    if (!replacement || !replacement->transferred || !replacement->ticket)
        panic("exec replacement: commit without transfer");
    kproc_exec_replacement_drop(replacement->process, replacement->ticket);
    replacement->ticket = 0;
}
/* On the relaunched execution, before user entry. The container now owns the
 * old environment; its disposal releases that with the container's own TPA. */
void kproc_exec_replacement_retire(KPROC_EXEC_REPLACEMENT **handle)
{
    KPROC_EXEC_REPLACEMENT *replacement;
    if (!handle || !*handle) return;
    replacement = *handle;
    if (!replacement->transferred || replacement->ticket)
        panic("exec replacement: retiring an uncommitted receipt");
    *handle = NULL;
    if (replacement->image) {
        proc_dispose_process(replacement->image);
        kproc_release(replacement->image);
    }
    kaspace_put(replacement->selected);
    kaspace_put(replacement->source);
    kproc_release(replacement->process);
    kfree(replacement);
}

#if CONF_WITH_EXEC_REPLACEMENT_TEST
LONG kproc_exec_replacement_probe(UWORD fail_after)
{
    KPROC *source=kproc_retain(run),*foreign=NULL;
    KTHREAD *thread=ksched_exit_thread(run),*prepared_thread=NULL;
    KASPACE_MEMBER *members=NULL;
    KPROC_EXEC_REPLACEMENT *replacement=NULL,*competing=NULL;
    KASPACE *selected=NULL,*original;
    ASPACE *backend;
    UQUAD number,execution;
    ULONG tid;
    WORD pid;
    BOOL protected;
    LONG error=EINVFN;
    if (!source || !thread || (fail_after>4 && (fail_after<0x100 || fail_after>0x105))) goto done;
    original=kproc_active_space(source);
    number=kproc_ref_number(source);pid=kproc_ref_pid(source);tid=kthread_tid(thread);
    execution=ksched_context_id();protected=ksched_cleanup_active();
    backend=aspace_create();
    if (!backend) { error=ENSMEM;goto done; }
    selected=kaspace_adopt(backend);
    if (!selected) { aspace_destroy(backend);error=ENSMEM;goto done; }
    if (fail_after>=0x103) {
        error=proc_create_clone_process(source,0,&foreign);
        if (error) goto done;
        if (fail_after==0x104) {
            error=kproc_clone_prepare_mapping_owners(foreign,source,selected,TRUE,&members);
            if (error) goto done;
        } else {
            prepared_thread=kthread_construct_identity(foreign,fail_after==0x105?original:selected);
            if (!prepared_thread) { error=ENSMEM;goto done; }
        }
    }
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
        if (fail_after>=0x100 && fail_after<=0x102) {
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
    if (members) kproc_abort_mapping_owners(foreign,selected,&members);
    kthread_destroy(prepared_thread);
    if (foreign) { proc_dispose_process(foreign);kproc_release(foreign); }
    kaspace_put(selected);kproc_release(source);
    return error;
}
#endif
