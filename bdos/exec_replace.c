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
#if CONF_WITH_EXEC_REPLACE
#include "launch_args.h"
#include "../lib/ldso/startup.h"
#endif
#if CONF_WITH_EXEC_REPLACEMENT_TEST
#include "../include/exec_handoff_test.h"
#endif
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
    if (!replacement || !replacement->cleanup.active ||
        replacement->execution != ksched_context_id() ||
        replacement->process_number != kproc_ref_number(replacement->process) ||
        replacement->tid != kthread_tid(replacement->thread) ||
        ksched_exit_thread(run) != replacement->thread ||
        kthread_process(replacement->thread) != replacement->process ||
        kthread_aspace(replacement->thread) != replacement->source ||
        !kthread_is_executing(replacement->thread) ||
        ksched_cancellation_pending() ||
        !ksched_exec_context(replacement->process) ||
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
    LONG error;
    if (!out || *out || !source || !thread || !selected) return EINVAL;
    original = kproc_active_space(source);
    if (!original || original == selected || !ksched_context_id() ||
        ksched_exit_thread(run) != thread || kthread_process(thread) != source ||
        kthread_aspace(thread) != original || !kthread_is_executing(thread) ||
        !kproc_thread_is_sole(source, thread) || ksched_cancellation_pending() ||
        !ksched_exec_context(source)) return EACCDN;
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

#if CONF_WITH_EXEC_REPLACE
static LONG exec_replace(char *path, char *tail, char *env,
                         const DL_START_VECTORS *vectors, PEXECVE_ARGS **args)
{
    KPROC *source = kproc_retain(run);
    KTHREAD *thread = ksched_exit_thread(run);
    KPROC_EXEC_REPLACEMENT *receipt = NULL;
    KPROC_EXEC_IMAGE *bundle = NULL;
    KASPACE *selected = NULL;
    ASPACE *backend;
    LONG error = EACCDN;
    if (!source || !thread || !path || (!tail && !vectors)) goto done;
    backend = aspace_create();
    if (!backend) { error = ENSMEM; goto done; }
    selected = kaspace_adopt(backend);
    if (!selected) { aspace_destroy(backend); error = ENSMEM; goto done; }
    error = kproc_exec_replacement_prepare(source, thread, selected, &receipt);
    if (error) goto done;
    error = proc_exec_replacement_stage(receipt, path, tail, env, vectors);
    /* Staging has copied all data into the new image. The receipt still
     * protects this execution, and the old stack may disappear at handoff. */
    if (args) pexecve_args_release(args);
    if (!error)
        error = kproc_exec_image_prepare(source, kproc_exec_replacement_image(receipt), selected, &bundle);
    if (!error) {
        /* The receipt and bundle hold their own references from here on;
         * success does not return to release ours. */
        kaspace_put(selected); selected = NULL;
        kproc_release(source); source = NULL;
        error = ksched_exec_replace(&receipt, &bundle);
    }
    kproc_exec_image_abort(&bundle);
    kproc_exec_replacement_abort(&receipt);
done:
    if (args) pexecve_args_release(args);
    kaspace_put(selected);
    kproc_release(source);
    return error;
}

LONG proc_exec_replace(char *path, char *tail, char *env)
{
    return exec_replace(path, tail, env, NULL, NULL);
}

LONG proc_exec_replace_vectors(PEXECVE_ARGS **args)
{
    DL_START_VECTORS vectors;

    vectors.argv = (*args)->argv;
    vectors.envp = (*args)->envp;
    vectors.argc = (*args)->argc;
    vectors.envc = (*args)->envc;
    return exec_replace((char *)(*args)->path, NULL, NULL, &vectors, args);
}
#endif

#if CONF_WITH_EXEC_REPLACEMENT_TEST
static struct { UQUAD baseline; ULONG ready; } exec_wait;
static BOOL exec_wait_ready(void *argument)
{
    KPROC *child = argument;
    BOOL ready = kproc_ref_exec_generation(child) != exec_wait.baseline || kproc_ref_terminal(child);
    if (ready) exec_wait.ready++;
    return ready;
}
LONG kproc_exec_replace_test(VADDR address, ULONG tag)
{
    if (tag >= EXEC_REPLACE_ARM && tag < EXEC_REPLACE_ARM + 0x400) {
        UWORD kind = (UWORD)((tag >> 8) & 3), stage = (UWORD)(tag & 0xff);
        kproc_exec_replacement_test_fail_after(kind == 3 ? stage : 0);
        kproc_exec_image_test_fail_after(kind == 1 ? stage : 0);
        ksched_exec_replace_test_fail_after(kind == 2 ? stage : 0);
        return E_OK;
    }
    if (tag == EXEC_REPLACE_QUERY) {
        EXEC_REPLACE_TEST_STATE state;
        KPROC *self = kproc_retain(run);
        KTHREAD *thread = ksched_exit_thread(run);
        UQUAD number, root, generation;
        BOOL copied;
        if (!self || !thread || address >= USER_VA_LIMIT) { kproc_release(self); return EACCDN; }
        number = kproc_ref_number(self); root = kproc_user_root(run);
        generation = kproc_ref_exec_generation(self);
        state.process[0] = (ULONG)number; state.process[1] = (ULONG)(number >> 32);
        state.root[0] = (ULONG)root; state.root[1] = (ULONG)(root >> 32);
        state.generation[0] = (ULONG)generation; state.generation[1] = (ULONG)(generation >> 32);
        state.pid = (ULONG)(LONG)kproc_ref_pid(self); state.tid = kthread_tid(thread);
        kproc_release(self);
        copied = kproc_copy_to_user_resident(address, &state, sizeof(state));
        return copied ? E_OK : EIMBA;
    }
    if (tag == EXEC_REPLACE_PEEK) {
        KPROC *child;
        PD *pd;
        LONG result=EINVAL;
        if (!address || address>32767UL) return EINVAL;
        child=kproc_test_retain_pid((WORD)address);
        pd=kproc_live_pd(child);
        if (pd && kproc_notification_parent(pd)==run && !kproc_ref_terminal(child))
            result=(LONG)(kproc_ref_exec_generation(child)&0xff);
        kproc_release(child);
        return result;
    }
    if (tag >= EXEC_REPLACE_WAIT && tag <= EXEC_REPLACE_WAIT + 0xff) {
        KPROC *child = kproc_test_retain_pid((WORD)address);
        UQUAD generation;
        BOOL terminal;
        if (!child || kproc_ref_pid(child) == kproc_pid(run)) { kproc_release(child); return EINVAL; }
        exec_wait.baseline = tag & 0xff; exec_wait.ready = 0;
        if (!ksched_wait_ready(exec_wait_ready, child)) { kproc_release(child); return EACCDN; }
        generation = kproc_ref_exec_generation(child); terminal = kproc_ref_terminal(child);
        kproc_release(child);
        return (LONG)((generation & 0xff) | (terminal ? 0x100UL : 0) | ((exec_wait.ready & 0xff) << 16));
    }
    return EINVFN;
}

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
            char *valid_program =
#if CONF_WITH_ELF_LOADER
                "C:\\PIEPROBE.TOS";
#else
                "C:\\EXEC200P.TOS";
#endif
            char *path=fail_after==0x100 ? valid_program :
                       fail_after==0x101 ? "C:\\NO970.TOS" : "C:\\LOADFAIL.TOS";
            error=proc_exec_replacement_stage(replacement,path,"",environment,NULL);
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
