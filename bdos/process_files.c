/* One PROCESS-local descriptor namespace over the canonical SFT owner.
 * Serialized cooperative publication; never a lock across backend callbacks. */
#include "emutos.h"
#include "process_files.h"
#include "kproc.h"
#include "kheap.h"
#include "gemerror.h"
#include "console.h"
#include "string.h"
#include "biosext.h"
#include "bdosstub.h"
#if CONF_WITH_MULTITHREADING
#include "ksched.h"
#endif

#define FILE_EMPTY 0
#define FILE_LIVE 1
#define FILE_OPENING 2
struct kproc_file_snapshot {
    KPROC *source,*target;
    PROCESS_FILES *table;
    UQUAD number,ticket,epoch,execution,target_epoch,target_before;
    UWORD policy;
    BOOL prepared,sealed,committed,preparing,cancelled;
    KPROC_SEARCH_SNAPSHOT *search;
    PROCESS_FILE_BINDING binding[NUMHANDLES];
    FS_FILE_REFERENCE references[NUMHANDLES];
};
static UQUAD file_id;
static BOOL next_id(UQUAD *out)
{
    if (file_id==~(UQUAD)0) return FALSE;
    *out=++file_id;return TRUE;
}
static UQUAD execution_id(void)
{
#if CONF_WITH_MULTITHREADING
    return ksched_context_id();
#else
    return 0;
#endif
}
static BOOL valid_descriptor(WORD descriptor)
{ return descriptor>=0 && descriptor<NUMHANDLES; }
static BOOL valid_backend(WORD handle)
{ return handle>=NUMSTD && handle<NUMHANDLES; }
static void mirror(PROCESS_FILES *table,WORD descriptor,WORD handle)
{
    if (descriptor<NUMSTD && table->standard) table->standard[descriptor]=(SBYTE)handle;
}
static LONG mutate(PROCESS_FILES *table,UQUAD *generation)
{
    if (!table || table->retiring || table->snapshot ||
        !kproc_file_references_allowed(kproc_live_pd(table->owner))) return EACCDN;
    if (table->exhausted || !next_id(generation) || !next_id(&table->epoch)) {
        table->exhausted=TRUE;return EAGAIN;
    }
    return E_OK;
}
BOOL process_files_init(PROCESS_FILES *table,KPROC *owner,SBYTE *standard)
{
    WORD i;
    ULONG references[OPNFILES];
    PD *pd=kproc_live_pd(owner);
    memset(table,0,sizeof(*table));table->owner=owner;
    table->number=kproc_ref_number(owner);table->standard=standard;
    if (!pd || !next_id(&table->epoch)) return FALSE;
    /* A retained PE_GO basepage can already own legacy explicit descriptors.
     * Existing canonical bindings and its own standard aliases explain all
     * other ordinary references; f_own alone is never an ownership proof. */
    kproc_standard_file_refs(references);
    for (i=0;i<NUMSTD;i++) {
        PROCESS_FILE_BINDING *binding=&table->binding[i];
        WORD h=standard[i];
        if (!h) continue;
        if (h>=0) {
            if (!valid_backend(h) || !sft[h-NUMSTD].f_incarnation || !fs_sft_accessible(h-NUMSTD)) return FALSE;
            references[h-NUMSTD]++;
        } else if (!IS_BIOS_HANDLE(h)) return FALSE;
        if (!next_id(&binding->generation)) return FALSE;
        binding->handle=h;binding->state=FILE_LIVE;
        if (h>=NUMSTD) binding->incarnation=sft[h-NUMSTD].f_incarnation;
    }
    for (i=0;i<OPNFILES;i++) if (references[i] && (sft[i].f_use<=0 ||
        (ULONG)sft[i].f_use<references[i]+sft[i].f_staged_refs+sft[i].f_cleanup_refs)) return FALSE;
    for (i=0;i<OPNFILES;i++) {
        FTAB *slot=&sft[i];
        PROCESS_FILE_BINDING *binding=&table->binding[i+NUMSTD];
        if (slot->f_own!=pd || slot->f_use<=0 ||
            (ULONG)slot->f_use<=references[i]+slot->f_staged_refs+slot->f_cleanup_refs) continue;
        if (!slot->f_incarnation || !fs_sft_accessible(i) || !next_id(&binding->generation)) return FALSE;
        /* Legacy Fdup uses a separate SFT slot, so one residual ordinary
         * reference is the sole explicit binding for this legacy owner. */
        binding->handle=(WORD)(i+NUMSTD);binding->incarnation=slot->f_incarnation;
        binding->state=FILE_LIVE;
    }
    return TRUE;
}
BOOL process_files_managed(PD *pd) { return kproc_files(pd)!=NULL; }
LONG process_files_change_admission(PD *pd)
{
    PROCESS_FILES *table=kproc_files(pd);
    if (!table) return E_OK;
    if (table->retiring || table->snapshot || !kproc_file_references_allowed(pd)) return EACCDN;
    return table->exhausted?EAGAIN:E_OK;
}
LONG process_files_operation_begin(PD *pd,PROCESS_FILE_OPERATION *operation)
{
    PROCESS_FILES *table=kproc_files(pd);
    KPROC *owner;
    UQUAD generation;
    LONG error;
    if (!operation || operation->table || operation->owner) return EINVAL;
    if (!table) return E_OK; /* established boot/legacy caller */
    if (table->operations==~(ULONG)0) return EAGAIN;
    owner=kproc_retain_live(table->owner);
    if (!owner) return EACCDN;
    error=mutate(table,&generation);
    if (error) { kproc_release(owner);return error; }
    table->operations++;
    operation->table=table;operation->owner=owner;
    return E_OK;
}
LONG process_files_search_begin(PD *pd,PROCESS_FILE_OPERATION *operation)
{
    PROCESS_FILES *table=kproc_files(pd);
    LONG error;
    if (table && table->search_busy) return EACCDN;
    error=process_files_operation_begin(pd,operation);
    if (!error && table) {
        table->search_busy=TRUE;table->search_actor=execution_id();operation->search=TRUE;
    }
    return error;
}
LONG process_files_cache_begin(PD *pd,PROCESS_FILE_OPERATION *operation)
{
    PROCESS_FILES *table=kproc_files(pd);
    /* Trusted header helpers nest only inside their admitted search actor.
     * Reentrant GEMDOS search/Fsetdta calls still use search_begin and fail. */
    if (!table || !table->search_busy) return process_files_search_begin(pd,operation);
    if (table->search_actor!=execution_id()) return EACCDN;
    return process_files_operation_begin(pd,operation);
}
void process_files_operation_end(PROCESS_FILE_OPERATION *operation)
{
    PROCESS_FILES *table;
    KPROC *owner;
    if (!operation || !(table=operation->table)) return;
    owner=operation->owner;operation->table=NULL;operation->owner=NULL;
    if (!table->operations) panic("PROCESS operation underflow");
    if (operation->search) {
        if (!table->search_busy) panic("PROCESS search operation lost");
        operation->search=FALSE;table->search_busy=FALSE;table->search_actor=0;
    }
    table->operations--;
    kproc_release(owner);
}
void process_files_invalidate_epoch(PROCESS_FILES *table)
{
    /* Authoritative invalidation cannot wait for or respect a snapshot freeze. */
    if (!table || table->retiring) return;
    if (!next_id(&table->epoch)) { table->epoch=0;table->exhausted=TRUE; }
}

void process_files_search_invalidate(PROCESS_FILES *table,WORD drive)
{
    if (!table) return;
    if (table->preparation && table->preparation->search)
        kproc_search_snapshot_invalidate(table->preparation->search,drive);
    process_files_invalidate_epoch(table);
}

static LONG live_binding(const PROCESS_FILES *table, WORD descriptor,
                         const PROCESS_FILE_BINDING **out)
{
    const PROCESS_FILE_BINDING *binding;
    if (!table || !valid_descriptor(descriptor)) return EIHNDL;
    if (table->retiring) return EACCDN;
    binding=&table->binding[descriptor];
    if (binding->state==FILE_OPENING) return EACCDN;
    if (binding->state!=FILE_LIVE || !binding->generation) return EIHNDL;
    *out = binding;
    return E_OK;
}

LONG process_files_resolve(PD *pd,WORD descriptor,WORD *backend,UQUAD *incarnation)
{
    const PROCESS_FILE_BINDING *binding;
    WORD h;
    LONG error;

    if (!backend) return EIHNDL;
    error = live_binding(kproc_files(pd), descriptor, &binding);
    if (error) return error;
    h=binding->handle;
    if (h>=0 && (!valid_backend(h) || !binding->incarnation ||
        sft[h-NUMSTD].f_incarnation!=binding->incarnation || !fs_sft_accessible(h-NUMSTD))) return EIHNDL;
    *backend=h;if (incarnation) *incarnation=binding->incarnation;
    return E_OK;
}

#if CONF_WITH_FPOLL_TIME64
LONG process_files_poll_pin(const PROCESS_FILES *table, WORD descriptor,
                            FS_FILE_REFERENCE *out, WORD *bios, UQUAD *generation)
{
    const PROCESS_FILE_BINDING *binding;
    LONG error = live_binding(table, descriptor, &binding);
    WORD handle;

    if (error)
        return error;
    handle = binding->handle;
    if (IS_BIOS_HANDLE(handle)) {
        *bios = handle;
    } else {
        if (!valid_backend(handle))
            return EIHNDL;
        error = fs_reference_pin_poll(handle, binding->incarnation, out);
        if (error)
            return error;
        *bios = sft[handle - NUMSTD].f_bios;
    }
    *generation = binding->generation;
    return E_OK;
}
#endif

LONG process_files_open_begin(PD *pd,WORD backend)
{
    PROCESS_FILES *table=kproc_files(pd);
    PROCESS_FILE_BINDING *binding;
    WORD descriptor,i;
    UQUAD generation;
    LONG error;
    if (!table || !valid_backend(backend)) return EIHNDL;
    /* Prefer the legacy number for a fresh binding, but never overwrite a
     * different inherited binding merely because the backend slot is reused. */
    descriptor=backend;
    if (table->binding[descriptor].state!=FILE_EMPTY) {
        for (i=NUMSTD;i<NUMHANDLES;i++) if (table->binding[i].state==FILE_EMPTY) break;
        if (i==NUMHANDLES) return ENHNDL;
        descriptor=i;
    }
    error=mutate(table,&generation);if (error) return error;
    binding=&table->binding[descriptor];binding->generation=generation;
    binding->incarnation=sft[backend-NUMSTD].f_incarnation;
    binding->handle=backend;binding->state=FILE_OPENING;table->operations++;
    return descriptor;
}
LONG process_files_open_end(PD *pd,WORD descriptor,WORD backend)
{
    PROCESS_FILES *table=kproc_files(pd);
    PROCESS_FILE_BINDING *binding;
    if (!table || !valid_descriptor(descriptor)) return EACCDN;
    binding=&table->binding[descriptor];
    if (binding->state!=FILE_OPENING || binding->handle!=backend ||
        binding->incarnation!=sft[backend-NUMSTD].f_incarnation || table->retiring) return E_CHNG;
    if (!table->operations) panic("descriptor open owner lost");
    table->operations--;binding->state=FILE_LIVE;
    return E_OK;
}
LONG process_files_bind_bios(PD *pd,WORD descriptor,WORD bios)
{
    PROCESS_FILES *table=kproc_files(pd);
    UQUAD generation;
    LONG error;
    if (!table || !valid_descriptor(descriptor) || !IS_BIOS_HANDLE(bios)) return EIHNDL;
    if (table->binding[descriptor].state!=FILE_EMPTY && table->binding[descriptor].handle>=NUMSTD)
        return EACCDN; /* replacement must dispose its independently owned old binding */
    error=mutate(table,&generation);if (error) return error;
    table->binding[descriptor].handle=bios;table->binding[descriptor].incarnation=0;
    table->binding[descriptor].generation=generation;table->binding[descriptor].state=FILE_LIVE;
    mirror(table,descriptor,bios);return E_OK;
}
LONG process_files_duplicate(PD *source,WORD descriptor,PD *target,WORD destination)
{
    PROCESS_FILES *table=kproc_files(target);
    FS_FILE_REFERENCE reference=FS_FILE_REFERENCE_INIT;
    PROCESS_FILE_CLOSE close=PROCESS_FILE_CLOSE_INIT;
    FS_CLOSE_REPORT report;
    KPROC *owner=NULL,*target_owner=NULL;
    WORD backend,i;
    UQUAD incarnation,generation,empty;
    LONG error;
    if (!table || !source || source!=run || !kproc_file_references_allowed(source)) return EACCDN;
    if (destination<0) {
        for (i=NUMSTD;i<NUMHANDLES;i++) if (table->binding[i].state==FILE_EMPTY) break;
        if (i==NUMHANDLES) return ENHNDL;
        destination=i;
    }
    if (!valid_descriptor(destination)) return EIHNDL;
    if (process_files_managed(source)) {
        error=process_files_resolve(source,descriptor,&backend,&incarnation);
        if (error) return error;
    } else {
        if (!valid_descriptor(descriptor)) return EIHNDL;
        backend=descriptor<NUMSTD?PD_UFT(source)[descriptor]:descriptor;
        if (backend>=0 && (!valid_backend(backend) || !fs_sft_accessible(backend-NUMSTD))) return EIHNDL;
        incarnation=backend>=NUMSTD?sft[backend-NUMSTD].f_incarnation:0;
    }
    error=mutate(table,&generation);if (error) return error;
    if (process_files_managed(source)) {
        owner=kproc_retain(source);if (!owner) return EACCDN;
    }
    target_owner=kproc_retain_live(table->owner);if (!target_owner) { error=EACCDN;goto done; }
    if (backend>=NUMSTD) {
        error=owner?fs_reference_acquire(owner,descriptor,incarnation,&reference):
                    fs_reference_acquire_boot(source,descriptor,incarnation,&reference);
        if (error) goto done;
    }
    empty=table->binding[destination].generation;
    if (table->binding[destination].state!=FILE_EMPTY) {
        error=process_files_close_begin(target,destination,&close);
        if (error) goto done;
        empty=close.empty_generation;
        memset(&report,0,sizeof(report));report.disposition=FS_CLOSE_CONSUMED;
        if (close.reference.owned) fs_reference_close(&close.reference,TRUE,&report);
        process_files_close_end(&close,&report);
    }
    /* Backend callbacks may install a successor. The outer operation owns
     * only its captured empty generation, never that successor binding. */
    if (table->retiring || table->snapshot || table->binding[destination].generation!=empty ||
        (table->binding[destination].state!=FILE_EMPTY && table->binding[destination].handle>=NUMSTD) ||
        (reference.owned && !fs_reference_publishable(&reference))) { error=EACCDN;goto done; }
    table->binding[destination].generation=generation;
    table->binding[destination].incarnation=incarnation;
    table->binding[destination].handle=backend;table->binding[destination].state=FILE_LIVE;
    if (reference.owned) fs_reference_publish(&reference);
    mirror(table,destination,backend);error=destination;
done:
    if (reference.owned) {
        fs_reference_close(&reference,TRUE,&report);
        if (reference.owned) panic("descriptor duplicate rollback retained owner");
    }
    kproc_release(target_owner);kproc_release(owner);return error;
}
LONG process_files_force_bios(PD *pd,WORD descriptor,WORD bios)
{
    PROCESS_FILES *table=kproc_files(pd);
    PROCESS_FILE_CLOSE operation=PROCESS_FILE_CLOSE_INIT;
    FS_CLOSE_REPORT report;
    UQUAD generation,empty;
    KPROC *owner;
    LONG error;
    if (!table || !valid_descriptor(descriptor) || !IS_BIOS_HANDLE(bios)) return EIHNDL;
    error=mutate(table,&generation);if (error) return error;
    owner=kproc_retain_live(table->owner);if (!owner) return EACCDN;
    empty=table->binding[descriptor].generation;
    if (table->binding[descriptor].state!=FILE_EMPTY) {
        error=process_files_close_begin(pd,descriptor,&operation);if (error) goto done;
        empty=operation.empty_generation;
        memset(&report,0,sizeof(report));report.disposition=FS_CLOSE_CONSUMED;
        if (operation.reference.owned) fs_reference_close(&operation.reference,TRUE,&report);
        process_files_close_end(&operation,&report);
    }
    if (table->retiring || table->snapshot || table->binding[descriptor].generation!=empty ||
        (table->binding[descriptor].state!=FILE_EMPTY && table->binding[descriptor].handle>=NUMSTD)) { error=EACCDN;goto done; }
    table->binding[descriptor].generation=generation;table->binding[descriptor].incarnation=0;
    table->binding[descriptor].handle=bios;table->binding[descriptor].state=FILE_LIVE;
    mirror(table,descriptor,bios);error=E_OK;
done:
    kproc_release(owner);return error;
}
LONG process_files_close_begin(PD *pd,WORD descriptor,PROCESS_FILE_CLOSE *operation)
{
    PROCESS_FILES *table=kproc_files(pd);
    PROCESS_FILE_BINDING *binding;
    UQUAD generation;
    LONG error;
    if (!operation || operation->active || operation->reference.owned) return EINVAL;
    if (!valid_descriptor(descriptor)) return EIHNDL;
    if (!table) return EACCDN;
    binding=&table->binding[descriptor];
    if (binding->state==FILE_OPENING) return EACCDN;
    if (binding->state!=FILE_LIVE) return EIHNDL;
    error=mutate(table,&generation);if (error) return error;
    operation->owner=kproc_retain_identity(table->owner);if (!operation->owner) return EAGAIN;
    if (binding->handle>=0) {
        error=fs_reference_unpublish(binding->handle,binding->incarnation,&operation->reference);
        if (error) { kproc_release(operation->owner);operation->owner=NULL;return error; }
    }
    operation->table=table;operation->descriptor=descriptor;
    operation->empty_generation=generation;operation->active=TRUE;
    binding->state=FILE_EMPTY;binding->generation=generation;table->operations++;
    mirror(table,descriptor,descriptor<NUMSTD?get_default_handle(descriptor):0);
    return E_OK;
}
LONG process_files_close_end(PROCESS_FILE_CLOSE *operation,FS_CLOSE_REPORT *report)
{
    PROCESS_FILES *table;
    PROCESS_FILE_BINDING *binding;
    LONG error;
    if (!operation || !operation->active || !report) return EINVAL;
    table=operation->table;binding=&table->binding[operation->descriptor];
    if (operation->reference.owned) {
        if (!table->retiring && binding->state==FILE_EMPTY &&
            binding->generation==operation->empty_generation && !table->snapshot &&
            fs_reference_publishable(&operation->reference)) {
            binding->handle=operation->reference.handle;
            binding->incarnation=operation->reference.incarnation;
            binding->state=FILE_LIVE;
            fs_reference_publish(&operation->reference);
            mirror(table,operation->descriptor,binding->handle);
        } else {
            /* A callback installed a successor generation or source retired.
             * This old exact reference becomes existing residual service work;
             * never drop it or overwrite the successor binding. */
            error=fs_reference_dispose(&operation->reference,report);
            if (error) panic("descriptor retained close lost disposal owner");
        }
    } else if (!table->retiring && operation->descriptor<NUMSTD &&
               binding->state==FILE_EMPTY && binding->generation==operation->empty_generation) {
        binding->handle=get_default_handle(operation->descriptor);
        binding->incarnation=0;binding->state=FILE_LIVE;
        mirror(table,operation->descriptor,binding->handle);
    }
    if (!table->operations) panic("descriptor close operation underflow");
    table->operations--;operation->active=FALSE;
    kproc_release(operation->owner);operation->owner=NULL;operation->table=NULL;
    return E_OK;
}
LONG process_files_close(PD *pd,WORD descriptor,BOOL dispose,FS_CLOSE_REPORT *report)
{
    PROCESS_FILE_CLOSE operation=PROCESS_FILE_CLOSE_INIT;
    LONG error=process_files_close_begin(pd,descriptor,&operation);
    if (error) { memset(report,0,sizeof(*report));report->disposition=FS_CLOSE_NOT_OWNED;return report->error=error; }
    memset(report,0,sizeof(*report));report->disposition=FS_CLOSE_CONSUMED;
    error=operation.reference.owned?fs_reference_close(&operation.reference,dispose,report):E_OK;
    process_files_close_end(&operation,report);return error;
}
void process_files_invalidate(PROCESS_FILES *table,WORD backend,UQUAD incarnation)
{
    WORD i;
    UQUAD generation;
    BOOL changed=FALSE;
    if (!table || table->retiring) return;
    for (i=0;i<NUMHANDLES;i++) {
        PROCESS_FILE_BINDING *binding=&table->binding[i];
        if (binding->state==FILE_EMPTY || binding->handle!=backend || binding->incarnation!=incarnation) continue;
        if (binding->state==FILE_OPENING) {
            if (!table->operations) panic("invalidated descriptor open underflow");
            table->operations--;
        }
        if (!next_id(&generation)) { generation=0;table->exhausted=TRUE; }
        binding->generation=generation;binding->state=FILE_EMPTY;changed=TRUE;
        if (i<NUMSTD) {
            binding->handle=get_default_handle(i);binding->incarnation=0;
            binding->state=generation?FILE_LIVE:FILE_EMPTY;
            mirror(table,i,binding->handle);
        }
    }
    /* Media invalidation overrides snapshot admission and cannot be blocked
     * by exhausted mutation IDs. It invalidates the admitted epoch instead. */
    if (changed && !next_id(&table->epoch)) { table->epoch=0;table->exhausted=TRUE; }
}
void process_files_retire(PROCESS_FILES *table)
{
    WORD i;
    KPROC *owner;
    if (!table || table->retiring) return;
    owner=kproc_retain_identity(table->owner);
    if (!owner) panic("retiring descriptor identity saturation");
    table->retiring=TRUE;table->epoch=0;
    kproc_search_retire(table->owner);
    for (i=0;i<NUMHANDLES;i++) {
        PROCESS_FILE_BINDING *binding=&table->binding[i];
        FS_FILE_REFERENCE reference=FS_FILE_REFERENCE_INIT;
        FS_CLOSE_REPORT report;
        LONG error;
        if (binding->state==FILE_EMPTY) continue;
        if (binding->state==FILE_OPENING) {
            if (!table->operations) panic("retiring descriptor open underflow");
            table->operations--;
        }
        binding->state=FILE_EMPTY;binding->generation=0;
        if (binding->handle<NUMSTD) continue;
        error=fs_reference_unpublish(binding->handle,binding->incarnation,&reference);
        if (error) {
            if (error!=EIHNDL && error!=E_CHNG) panic("retiring descriptor reference lost");
            continue; /* only authoritative invalidation/reincarnation revokes */
        }
        fs_reference_close(&reference,TRUE,&report);
        kproc_note_file_owner_cleanup(table->owner,report.error,report.disposition==FS_CLOSE_TRANSFERRED);
        if (reference.owned) panic("retiring descriptor still owned");
    }
    table->standard=NULL;
    kproc_release(owner);
}

LONG kproc_files_snapshot_begin(KPROC *source,KPROC_FILE_SNAPSHOT **out)
{
    KPROC_FILE_SNAPSHOT *snapshot;
    PROCESS_FILES *table=kproc_ref_files(source);
    PD *pd=kproc_live_pd(source);
    UQUAD ticket;
    if (!out || *out || !source || !table || !pd || pd!=run || table->retiring ||
        table->snapshot || table->operations || table->exhausted || !kproc_file_references_allowed(pd)) return EACCDN;
    snapshot=kalloc(sizeof(*snapshot));if (!snapshot) return ENSMEM;
    memset(snapshot,0,sizeof(*snapshot));
    snapshot->source=kproc_retain_live(source);
    if (!snapshot->source || !next_id(&ticket)) {
        kproc_release(snapshot->source);kfree(snapshot);return EAGAIN;
    }
    snapshot->table=table;snapshot->number=table->number;snapshot->ticket=ticket;
    snapshot->epoch=table->epoch;snapshot->execution=execution_id();
    table->snapshot=ticket;table->preparation=snapshot;
    memcpy(snapshot->binding,table->binding,sizeof(snapshot->binding));
    *out=snapshot;return E_OK;
}
static BOOL snapshot_identity_live(const KPROC_FILE_SNAPSHOT *snapshot)
{
    PROCESS_FILES *table;
    if (!snapshot || snapshot->committed || snapshot->cancelled || !(table=snapshot->table) || !snapshot->source ||
        table!=kproc_ref_files(snapshot->source) || table->owner!=snapshot->source ||
        table->number!=snapshot->number || table->snapshot!=snapshot->ticket || table->preparation!=snapshot || table->retiring ||
        table->epoch!=snapshot->epoch || table->operations || table->exhausted ||
        !kproc_live_pd(snapshot->source)) return FALSE;
    if (snapshot->prepared) {
        WORD i;
        PROCESS_FILES *target=kproc_ref_files(snapshot->target);
        if (!target || target->retiring || target->snapshot || target->operations ||
            target->epoch!=snapshot->target_before || !kproc_live_pd(snapshot->target)) return FALSE;
        if (snapshot->search && !kproc_search_snapshot_valid(snapshot->search)) return FALSE;
        for (i=0;i<NUMHANDLES;i++)
            if (snapshot->references[i].owned && !fs_reference_publishable(&snapshot->references[i])) return FALSE;
    }
    return kproc_file_references_allowed(kproc_live_pd(snapshot->source));
}
BOOL kproc_files_snapshot_validate(const KPROC_FILE_SNAPSHOT *snapshot)
{
    return snapshot_identity_live(snapshot) && kproc_live_pd(snapshot->source)==run &&
        snapshot->execution==execution_id();
}
LONG kproc_files_snapshot_seal(KPROC_FILE_SNAPSHOT *snapshot)
{
    if (!kproc_files_snapshot_validate(snapshot) || !snapshot->prepared) return EACCDN;
    snapshot->sealed=TRUE;return E_OK;
}
static LONG snapshot_prepare_body(KPROC_FILE_SNAPSHOT *snapshot,KPROC *target,UWORD policy)
{
    PROCESS_FILES *table=kproc_ref_files(target);
    WORD i,limit=policy==PROCESS_FILES_STANDARD?NUMSTD:NUMHANDLES;
    LONG error;
    if (!kproc_files_snapshot_validate(snapshot) || snapshot->prepared || snapshot->target || !table || target==snapshot->source ||
        (policy!=PROCESS_FILES_STANDARD && policy!=PROCESS_FILES_ALL &&
         policy!=PROCESS_FILES_DESCRIPTORS) || table->retiring ||
        table->snapshot || table->operations || table->exhausted || !kproc_live_pd(target)) return EACCDN;
    for (i=0;i<NUMHANDLES;i++) if (table->binding[i].state!=FILE_EMPTY && table->binding[i].handle>=NUMSTD)
        return EACCDN;
    snapshot->target=kproc_retain_live(target);if (!snapshot->target) return EACCDN;
    snapshot->target_before=table->epoch;
    snapshot->policy=policy;
    if (!next_id(&snapshot->target_epoch)) return EAGAIN;
    for (i=0;i<limit;i++) {
        PROCESS_FILE_BINDING *binding=&snapshot->binding[i];
        if (binding->state==FILE_EMPTY) continue;
        if (binding->state!=FILE_LIVE) return EACCDN;
        if (!next_id(&binding->generation)) return EAGAIN;
        if (binding->handle>=NUMSTD) {
            error=fs_reference_acquire(snapshot->source,i,binding->incarnation,&snapshot->references[i]);
            if (error) return error;
        }
    }
    if (policy==PROCESS_FILES_ALL) {
        error=kproc_search_snapshot_prepare(snapshot->source,target,snapshot->ticket,&snapshot->search);
        if (error) return error;
    }
    snapshot->prepared=TRUE;
    return kproc_files_snapshot_validate(snapshot)?E_OK:EACCDN;
}
LONG kproc_files_snapshot_prepare(KPROC_FILE_SNAPSHOT *snapshot,KPROC *target,UWORD policy)
{
    LONG error;
    if (!snapshot || snapshot->preparing || snapshot->cancelled) return EACCDN;
    snapshot->preparing=TRUE;
    error=snapshot_prepare_body(snapshot,target,policy);
    snapshot->preparing=FALSE;
    if (snapshot->cancelled) {
        KPROC_FILE_SNAPSHOT *discard=snapshot;
        kproc_files_snapshot_end(&discard);return EACCDN;
    }
    return error;
}
void kproc_files_snapshot_commit(KPROC_FILE_SNAPSHOT *snapshot,KPROC *target)
{
    PROCESS_FILES *table=kproc_ref_files(target);
    WORD i,limit;
    /* Only the admitted creator may seal. Its existing scheduler root can
     * consume that token without impersonating the suspended execution. */
    if (!snapshot_identity_live(snapshot) || !snapshot->sealed || !snapshot->prepared || snapshot->target!=target ||
        !table || table->retiring || table->snapshot || table->operations) panic("descriptor snapshot commit changed");
    limit=snapshot->policy==PROCESS_FILES_STANDARD?NUMSTD:NUMHANDLES;
    for (i=0;i<NUMHANDLES;i++)
        if (table->binding[i].state!=FILE_EMPTY && table->binding[i].handle>=NUMSTD)
            panic("descriptor snapshot target acquired resources");
    for (i=0;i<limit;i++) if (snapshot->references[i].owned && !fs_reference_publishable(&snapshot->references[i]))
        panic("descriptor snapshot exact reference revoked before commit");
    for (i=0;i<limit;i++) {
        table->binding[i]=snapshot->binding[i];
        if (snapshot->references[i].owned) fs_reference_publish(&snapshot->references[i]);
        mirror(table,i,table->binding[i].state==FILE_EMPTY?0:table->binding[i].handle);
    }
    if (snapshot->search) kproc_search_snapshot_commit(snapshot->search);
    table->epoch=snapshot->target_epoch;snapshot->committed=TRUE;
    snapshot->table->preparation=NULL;
    snapshot->table->snapshot=0; /* publication ends mutation admission */
}
void kproc_files_snapshot_end(KPROC_FILE_SNAPSHOT **handle)
{
    KPROC_FILE_SNAPSHOT *snapshot;
    WORD i;
    if (!handle || !(snapshot=*handle)) return;
    *handle=NULL;
    if (snapshot->preparing) { snapshot->cancelled=TRUE;return; }
    /* This attempt can never publish again. Release admission before any
     * destructor/callback; exact staged references and retained identities
     * keep cleanup safe even if a callback starts a successor snapshot. */
    if (!snapshot->committed) {
        if (snapshot->table->snapshot==snapshot->ticket) snapshot->table->snapshot=0;
        else panic("descriptor snapshot admission replaced");
    }
    if (snapshot->table->preparation==snapshot) snapshot->table->preparation=NULL;
    kproc_search_snapshot_end(&snapshot->search);
    for (i=0;i<NUMHANDLES;i++) if (snapshot->references[i].owned) {
        FS_CLOSE_REPORT report;
        fs_reference_close(&snapshot->references[i],TRUE,&report);
        if (snapshot->references[i].owned) panic("descriptor snapshot rollback retained owner");
    }
    kproc_release(snapshot->target);kproc_release(snapshot->source);kfree(snapshot);
}
/* PROCESS_FILES_DESCRIPTORS for a Pexec child: like a FreeMiNT Pexec child
 * (and every TOS program, whose handles are global) it can use each of its
 * parent's open descriptors, under the same local numbers, with its own close
 * rights; it starts with its own DTA and no directory searches.
 * PROCESS_FILES_STANDARD for the clone factory's COPY_STANDARD_HANDLES. A
 * nonmanaged boot caller hands on its standard handles only. */
LONG process_files_inherit(PD *target,PD *source,UWORD policy)
{
    WORD i;
    KPROC_FILE_SNAPSHOT *snapshot=NULL;
    KPROC *parent,*child;
    LONG error;
    /* ALL also carries DTA/search state: fork's snapshot, not this path. */
    if (policy!=PROCESS_FILES_STANDARD && policy!=PROCESS_FILES_DESCRIPTORS) return EINVAL;
    parent=kproc_retain(source);child=kproc_retain(target);
    if (child && !process_files_managed(source)) {
        error=E_OK;
        for (i=0;i<NUMSTD;i++) {
            LONG copied=process_files_duplicate(source,i,target,i);
            if (copied<0) { error=copied;break; }
        }
        goto done;
    }
    if (!parent || !child) { error=EACCDN;goto done; }
    error=kproc_files_snapshot_begin(parent,&snapshot);
    if (!error) error=kproc_files_snapshot_prepare(snapshot,child,policy);
    if (!error) error=kproc_files_snapshot_seal(snapshot);
    if (!error) kproc_files_snapshot_commit(snapshot,child);
    kproc_files_snapshot_end(&snapshot);
done:
    kproc_release(parent);kproc_release(child);return error;
}
