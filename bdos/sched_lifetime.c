/* Kernel controller and internal launch-completion lifetime; no dispatch policy. */
#include "emutos.h"
#include "biosext.h"
#include "ksched.h"
#include "ksched_lifetime.h"
#include "kproc.h"
#include "kheap.h"
#include "kprint.h"
#include "gemerror.h"
#include "procmem.h"
#include "string.h"

static struct { PD *pd; UQUAD generation; ULONG references; } controller;
typedef struct admission {
    struct admission *next;
    UQUAD identity;
    ULONG references;
    KSCHED_WAIT_SEQUENCE changes;
    KPROC *owner; /* completion identity only, never live PROCESS resources */
    UWORD count;
    BOOL abandoned, consumed;
    struct { BOOL complete; LONG status; } result[KSCHED_LIMIT];
} ADMISSION;
static ADMISSION *admissions;
static UQUAD last_admission;

BOOL ksched_controller_install(PD *pd)
{
    UQUAD generation;
    if (!pd || !(generation=procmem_gen(pd)) ||
        !procmem_range_live((VADDR)(uintptr_t)pd,sizeof(*pd))) return FALSE;
    if (controller.pd) return controller.pd==pd && controller.generation==generation;
    pd->p_lowtpa=PTR_TO_USERPTR((UBYTE *)pd);
    pd->p_hitpa=PTR_TO_USERPTR((UBYTE *)pd+sizeof(*pd));
    pd->p_env=PTR_TO_USERPTR(pd->p_cmdlin);
    procmem_pin((VADDR)(uintptr_t)pd,sizeof(*pd),1);
    controller.pd=pd; controller.generation=generation; controller.references=1;
    return TRUE;
}
PD *ksched_controller_pd(void)
{
    if (controller.pd && procmem_gen(controller.pd)!=controller.generation)
        panic("scheduler: kernel controller allocation changed");
    return controller.pd;
}
static ADMISSION *lookup(const KSCHED_ADMISSION *handle)
{
    ADMISSION *p;
    if (handle && handle->identity)
        for (p=admissions;p;p=p->next) if (p->identity==handle->identity) return p;
    return NULL;
}
LONG ksched_admission_create(KPROC *owner,UWORD count,KSCHED_ADMISSION *out)
{
    ADMISSION *p;
    KPROC *retained=NULL;
    if (!out || out->identity || !count || count>KSCHED_LIMIT) return EINVAL;
    if (last_admission==~0ULL) return ERANGE;
    if (!ksched_controller_pd() || controller.references==0xffffffffUL) return EACCDN;
    p=kalloc(sizeof(*p)); if (!p) return ENSMEM;
    if (owner && !(retained=kproc_retain_live(owner))) { kfree(p); return EACCDN; }
    /* Preparation may acquire resources; revalidate before linking identity.
     * The future completion-owner lock covers this final commit, not kalloc
     * or last-reference rollback. Current callers serialize kernel execution. */
    if (last_admission==~0ULL || controller.references==0xffffffffUL ||
        (retained && !kproc_live_pd(retained))) {
        LONG error=last_admission==~0ULL ? ERANGE : EACCDN;
        kfree(p); if (retained) kproc_release(retained);
        return error;
    }
    memset(p,0,sizeof(*p));
    p->owner=retained; p->count=count; p->references=1;
    controller.references++;
    p->identity=++last_admission;
    p->next=admissions; admissions=p;
    out->identity=p->identity;
    return E_OK;
}
BOOL ksched_admission_retain(const KSCHED_ADMISSION *source,KSCHED_ADMISSION *out)
{
    ADMISSION *p;
    if (!out || out->identity || !(p=lookup(source)) || p->references==0xffffffffUL) return FALSE;
    p->references++; out->identity=p->identity; return TRUE;
}
void ksched_admission_release(KSCHED_ADMISSION *handle)
{
    ADMISSION **link;
    UQUAD identity;
    if (!handle) return;
    identity=handle->identity; handle->identity=0;
    for (link=&admissions;*link;link=&(*link)->next) if ((*link)->identity==identity) {
        ADMISSION *p=*link;
        if (!p->references) panic("scheduler: admission reference underflow");
        if (--p->references) return;
        *link=p->next; /* unpublish before any last-reference operation */
        if (controller.references<=1) panic("scheduler: controller reference underflow");
        controller.references--;
        {
            KPROC *owner=p->owner;
            kfree(p);
            if (owner) kproc_release(owner);
        }
        return;
    }
}
BOOL ksched_admission_complete(const KSCHED_ADMISSION *handle,UWORD index,LONG status)
{
    ADMISSION *p=lookup(handle);
    if (!p || index>=p->count || p->result[index].complete || p->consumed) return FALSE;
    p->result[index].status=status; p->result[index].complete=TRUE;
    ksched_sequence_advance(&p->changes);
    return TRUE;
}
LONG ksched_admission_take(const KSCHED_ADMISSION *handle,LONG *results,UWORD count)
{
    ADMISSION *p=lookup(handle);
    UWORD i;
    if (!p || !results || count!=p->count) return EINVAL;
    if (p->abandoned || p->consumed || (p->owner && !kproc_live_pd(p->owner))) return EACCDN;
    for (i=0;i<count;i++) if (!p->result[i].complete) return EAGAIN;
    for (i=0;i<count;i++) results[i]=p->result[i].status;
    p->consumed=TRUE;
    ksched_sequence_advance(&p->changes);
    return E_OK;
}
BOOL ksched_admission_ready(const KSCHED_ADMISSION *handle)
{
    ADMISSION *p=lookup(handle); UWORD i;
    if(!p || p->abandoned || p->consumed || (p->owner && !kproc_live_pd(p->owner))) return TRUE;
    for(i=0;i<p->count;i++) if(!p->result[i].complete) return FALSE;
    return TRUE;
}
BOOL ksched_admission_abandoned(const KSCHED_ADMISSION *handle)
{
    ADMISSION *p=lookup(handle);
    return !p || p->abandoned || (p->owner && !kproc_live_pd(p->owner));
}
void ksched_admission_abandon(const KSCHED_ADMISSION *handle)
{
    ADMISSION *p=lookup(handle);
    if (p && !p->abandoned) { p->abandoned=TRUE; ksched_sequence_advance(&p->changes); }
}
void ksched_admission_owner_exited(KPROC *owner)
{
    ADMISSION *p;
    if (!owner) return;
    for (p=admissions;p;p=p->next) if (p->owner==owner && !p->abandoned) {
        p->abandoned=TRUE; ksched_sequence_advance(&p->changes);
    }
}
PD *ksched_admission_owner_pd(const KSCHED_ADMISSION *handle)
{
    ADMISSION *p=lookup(handle);
    return p && !p->abandoned && p->owner ? kproc_live_pd(p->owner) : NULL;
}

/* Attachments store neither CPU continuations nor result destinations. Managed
 * continuations belong to KTHREAD/task storage; public PROCESS zombies are
 * completely independent. Registry/ref changes are cooperatively serialized. */
typedef struct result_waiter {
    struct result_waiter *next;
    UQUAD identity, execution;
    ULONG tid;
    KPROC *actor;
    KSCHED_ADMISSION admission;
} RESULT_WAITER;
static RESULT_WAITER *result_waiters;
static UQUAD last_waiter;

static RESULT_WAITER *find_waiter(const KSCHED_WAITER *handle)
{
    RESULT_WAITER *p;
    if (handle && handle->identity)
        for (p=result_waiters;p;p=p->next) if (p->identity==handle->identity) return p;
    return NULL;
}
LONG ksched_waiter_attach(const KSCHED_ADMISSION *admission,KPROC *actor,
                         ULONG tid,UQUAD execution,UWORD count,KSCHED_WAITER *out)
{
    ADMISSION *record=lookup(admission);
    RESULT_WAITER *p;
    KSCHED_ADMISSION held={0};
    if (!out || out->identity || !record || count!=record->count || !tid || !execution) return EINVAL;
    if (!actor || record->owner!=actor || record->abandoned || record->consumed) return EACCDN;
    if (last_waiter==~0ULL) return ERANGE;
    /* Pin identity before fallible preparation, then revalidate state. */
    if (!ksched_admission_retain(admission,&held)) return EACCDN;
    p=kalloc(sizeof(*p));
    if (!p) { ksched_admission_release(&held); return ENSMEM; }
    memset(p,0,sizeof(*p));
    p->actor=kproc_retain_live(actor);
    if (!p->actor || !kproc_live_pd(actor) || record->abandoned || record->consumed || last_waiter==~0ULL) {
        LONG error=last_waiter==~0ULL ? ERANGE : EACCDN;
        ksched_admission_release(&held);
        if (p->actor) kproc_release(p->actor);
        kfree(p); return error;
    }
    p->admission=held;
    p->tid=tid; p->execution=execution;
    p->identity=++last_waiter;
    p->next=result_waiters; result_waiters=p;
    out->identity=p->identity;
    return E_OK;
}
void *ksched_waiter_context(const KSCHED_WAITER *handle) { return find_waiter(handle); }
void ksched_waiter_observe(void *context, KSCHED_WAIT_OBSERVATION *observation)
{
    RESULT_WAITER *p=context;
    ADMISSION *record=p ? lookup(&p->admission) : NULL;
    if (!record) panic("scheduler: observation without retained admission");
    ksched_sequence_observe(&record->changes,observation);
}
BOOL ksched_waiter_ready(void *context)
{
    RESULT_WAITER *p=context;
    ADMISSION *record;
    UWORD i;
    if (!p) return TRUE;
    record=lookup(&p->admission);
    if (!record || record->abandoned || record->consumed || !kproc_live_pd(p->actor)) return TRUE;
    for (i=0;i<record->count;i++) if (!record->result[i].complete) return FALSE;
    return TRUE;
}
LONG ksched_waiter_take(const KSCHED_WAITER *handle,KPROC *actor,ULONG tid,
                       UQUAD execution,LONG *results,UWORD count)
{
    RESULT_WAITER *p=find_waiter(handle);
    if (!p) return EINVAL;
    if (p->actor!=actor || p->tid!=tid || p->execution!=execution) return EACCDN;
    return ksched_admission_take(&p->admission,results,count);
}
static void dispose_waiter(RESULT_WAITER *p)
{
    ksched_admission_release(&p->admission);
    kproc_release(p->actor);
    kfree(p);
}
void ksched_waiter_detach(KSCHED_WAITER *handle)
{
    RESULT_WAITER **link;
    UQUAD identity;
    if (!handle) return;
    identity=handle->identity; handle->identity=0;
    for (link=&result_waiters;*link;link=&(*link)->next)
        if ((*link)->identity==identity) {
            RESULT_WAITER *p=*link; *link=p->next;
            dispose_waiter(p); return;
        }
}
void ksched_waiter_retire(KPROC *actor,ULONG tid,UQUAD execution)
{
    RESULT_WAITER **link=&result_waiters, *retired=NULL;
    /* Detach the whole matching set before releasing anything. A release may
     * reenter registry operations; never carry a registry link across it. */
    while (*link) {
        RESULT_WAITER *p=*link;
        if (p->actor==actor && p->tid==tid && p->execution==execution)
        {
            *link=p->next; p->next=retired; retired=p;
        }
        else link=&p->next;
    }
    while (retired) {
        RESULT_WAITER *p=retired; retired=p->next;
        dispose_waiter(p);
    }
}
