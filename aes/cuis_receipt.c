/* Endpoint receipts own publication progress and the generic PROCESS right.
 * Completion and terminal order remain in the PROCESS observer. */
#define RECEIPT_SLOTS CUIS_RECEIPT_SLOTS
typedef struct {
    UQUAD generation, key;
    ULONG destination, tag, state, bytes;
    BOOL committed;
    UBYTE packet[32];
} CUIS_RECEIPT_SLOT;
typedef struct cuis_receipt_table {
    CUIS_RECEIPT_SLOT slots[RECEIPT_SLOTS];
} CUIS_RECEIPT_TABLE;
#if CONF_WITH_CUIS_RECEIPT_TEST
static ULONG receipt_active;
#endif
static UQUAD receipt_pair(ULONG low,ULONG high) { return ((UQUAD)high<<32)|low; }
static void receipt_release_table(CUIS_RECEIPT_TABLE *table, KPROC *owner)
{
    KSCHED_CLEANUP_SCOPE cleanup={0};
    ULONG i;
    if (!table) return;
    ksched_cleanup_begin(&cleanup);
    for (i=0;i<RECEIPT_SLOTS;i++) {
        CUIS_RECEIPT_SLOT *slot=&table->slots[i];
        if (!slot->state || slot->state==CUIS_RECEIPT_RECLAIMED) continue;
#if CONF_WITH_CUIS_RECEIPT_TEST
        receipt_active--;
#endif
        if (owner && kproc_live_pd(owner)==run)
            (void)proc_exec_handle_close(owner,slot->tag);
    }
    kfree(table);
    ksched_cleanup_end(&cleanup);
}
static ULONG receipt_free_slot(const CUIS_RECEIPT_TABLE *table)
{
    ULONG i;
    if (!table) return 0;
    for (i=0;i<RECEIPT_SLOTS;i++) {
        const CUIS_RECEIPT_SLOT *slot=&table->slots[i];
        if (!slot->state || (slot->state==CUIS_RECEIPT_RECLAIMED && slot->generation!=~0ULL)) break;
    }
    return i;
}
static LONG receipt_bind_locked(CUIS_CLIENT *source, ULONG destination, ULONG index,
                                ULONG right, UQUAD key, CUIS_RECEIPT_TABLE **storage)
{
    CUIS_RECEIPT_TABLE *table=source->receipt_table;
    CUIS_RECEIPT_SLOT *slot;
    ULONG i;
    if (!table) {
        if (!*storage) return ENSMEM;
        table=*storage;
    }
    if (index==RECEIPT_SLOTS) {
        index=receipt_free_slot(table);
        if (index==RECEIPT_SLOTS) return EAGAIN;
    }
    slot=&table->slots[index];
    if (slot->state && slot->state!=CUIS_RECEIPT_RECLAIMED) return EAGAIN;
    if (slot->generation==~0ULL || source->receipt_epoch==~0ULL) return ERANGE;
    for (i=0;i<RECEIPT_SLOTS;i++) {
        CUIS_RECEIPT_SLOT *entry=&table->slots[i];
        if (entry->state && entry->state!=CUIS_RECEIPT_RECLAIMED && entry->key==key) return EAGAIN;
    }
    if (!source->receipt_table) {
        source->receipt_table=table;
        *storage=NULL;
    }
    slot->generation++;
    slot->key=key;
    slot->destination=destination;
    slot->tag=right;
    slot->bytes=0;
    slot->committed=FALSE;
    slot->state=CUIS_RECEIPT_BOUND;
    source->receipt_epoch++;
#if CONF_WITH_CUIS_RECEIPT_TEST
    receipt_active++;
#endif
    return (LONG)index;
}
static ULONG receipt_wait_handles(const CUIS_CLIENT *source, ULONG *handles)
{
    ULONG count=0, i;
    if (!source->receipt_table) return 0;
    for (i=0;i<RECEIPT_SLOTS;i++) {
        const CUIS_RECEIPT_SLOT *slot=&source->receipt_table->slots[i];
        if (slot->state==CUIS_RECEIPT_BOUND || slot->state==CUIS_RECEIPT_PREPARED)
            handles[count++]=slot->tag;
    }
    return count;
}
static BOOL receipt_pending_cleanup(const CUIS_CLIENT *source)
{
    ULONG i;
    if (!source->receipt_table) return FALSE;
    for (i=0;i<RECEIPT_SLOTS;i++) {
        ULONG state=source->receipt_table->slots[i].state;
        if (state==CUIS_RECEIPT_COMMITTED || state==CUIS_RECEIPT_ACKNOWLEDGED) return TRUE;
    }
    return FALSE;
}
static CUIS_CLIENT *receipt_source(const CUIS_RECEIPT_RECORD *record)
{
    CUIS_CLIENT *source;
    if (record->operation || record->slot>=RECEIPT_SLOTS || record->source>0x7fffffffUL ||
        record->source_low!=record->source || record->source_high) return NULL;
    source=owned_client(record->source);
    return source && !source->retired ? source:NULL;
}
static CUIS_RECEIPT_SLOT *receipt_exact(CUIS_CLIENT *source,const CUIS_RECEIPT_RECORD *record)
{
    CUIS_RECEIPT_SLOT *slot;
    if (!source || !source->receipt_table) return NULL;
    slot=&source->receipt_table->slots[record->slot];
    if (!slot->state || !slot->generation || slot->generation!=receipt_pair(record->attempt_low,record->attempt_high) ||
        slot->key!=receipt_pair(record->key_low,record->key_high) || slot->tag!=record->tag ||
        slot->destination!=record->destination || record->destination_low!=slot->destination ||
        record->destination_high) return NULL;
    return slot;
}
static void receipt_record(CUIS_CLIENT *source,ULONG index,CUIS_RECEIPT_RECORD *out)
{
    CUIS_RECEIPT_SLOT *slot=&source->receipt_table->slots[index];
    memset(out,0,sizeof(*out));
    out->source=out->source_low=source->handle;
    out->destination=out->destination_low=slot->destination;
    out->slot=index; out->attempt_low=(ULONG)slot->generation; out->attempt_high=(ULONG)(slot->generation>>32);
    out->key_low=(ULONG)slot->key; out->key_high=(ULONG)(slot->key>>32);
    out->bytes=slot->bytes; out->state=slot->state; out->tag=slot->tag;
    if (slot->state==CUIS_RECEIPT_RECLAIMED && slot->committed) out->state|=CUIS_RECEIPT_COMMITTED_OUTCOME;
}
#if CONF_WITH_CUIS_RECEIPT_TEST
LONG cuis_receipt_test_launch(ULONG source_id,ULONG destination_id,ULONG index,
                             char *path,char *tail,char *environment,CUIS_RECEIPT_RECORD *out)
{
    KSCHED_CLEANUP_SCOPE cleanup={0};
    KPROC *owner=NULL;
    PROCESS_OBSERVER *observer=NULL;
    PROCESS_COMPLETION completion;
    CUIS_CLIENT *source=NULL,*destination=NULL;
    CUIS_RECEIPT_TABLE *table_storage=NULL;
    CUIS_RECEIPT_SLOT *slot;
    UBYTE *storage=NULL;
    UQUAD flags,key;
    LONG right=0,result=EINVAL;
    BOOL installed=FALSE,need_storage=FALSE,need_table;
    if (!out || index>=RECEIPT_SLOTS) return EINVAL;
    ksched_cleanup_begin(&cleanup);
    owner=kproc_retain(run);
    if (!owner) { result=EACCDN; goto done; }
    right=proc_exec_handle_prepare(owner,path,tail,environment,NULL,PROCESS_EXEC_ASYNC);
    if (right<0) { result=right; right=0; goto done; }
    result=proc_exec_retain_observer(owner,right,TRUE,&observer);
    if (result) goto done;
    if (!process_observer_snapshot(observer,&completion) || completion.state!=PROCESS_UNSTARTED) {
        result=EACCDN; goto done;
    }
    key=receipt_pair(completion.number_low,completion.number_high);
    flags=cuis_input_lock();
    source=owned_client(source_id); destination=find_handle(destination_id);
    if (!source || !destination || source->retired || destination->retired || source->process!=owner) {
        source=destination=NULL; result=EACCDN; cuis_input_unlock(flags); goto done;
    }
    if (!client_get(source)) { source=destination=NULL; result=EAGAIN; cuis_input_unlock(flags); goto done; }
    if (!client_get(destination)) { destination=NULL; result=EAGAIN; cuis_input_unlock(flags); goto done; }
    need_storage=destination->stream_data==NULL;
    need_table=source->receipt_table==NULL;
    cuis_input_unlock(flags);
    /* Existing rings remain owned by the retained destination. Only prepare
     * missing storage; memory pressure must not reject a reusable live ring. */
    if (need_storage) {
        storage=kalloc(CUIS_STREAM_BYTES*(1+sizeof(UWORD)));
        if (!storage) { result=ENSMEM; goto done; }
    }
    if (need_table) {
        table_storage=kalloc(sizeof(*table_storage));
        if (!table_storage) { result=ENSMEM; goto done; }
        memset(table_storage,0,sizeof(*table_storage));
    }
    flags=cuis_input_lock();
    if (source->retired || destination->retired || owned_client(source_id)!=source ||
        find_handle(destination_id)!=destination || ksched_cancellation_pending()) {
        result=EACCDN; cuis_input_unlock(flags); goto done;
    }
    if (!destination->stream_data && !storage) {
        result=EAGAIN; cuis_input_unlock(flags); goto done;
    }
    result=receipt_bind_locked(source,destination_id,index,(ULONG)right,key,&table_storage);
    if (result<0) { cuis_input_unlock(flags); goto done; }
    slot=&source->receipt_table->slots[index];
    /* This helper creates ASYNC preparations itself. Their canonical COMMIT
     * path has no allocation/callback/yield or predecessor teardown. Keeping
     * the existing IRQ exclusion through that path protects the entire BIND
     * interval: no sibling QUERY/CLOSE/END or callback can interleave. Deferred
     * preparations must never be routed through this bounded prototype helper. */
    if (!destination->stream_data) { destination->stream_data=storage; storage=NULL; installed=TRUE; }
    result=proc_exec_handle_commit(owner,(ULONG)right);
    if (result<0) {
        slot->state=CUIS_RECEIPT_RECLAIMED; receipt_active--;
        if (installed) { storage=destination->stream_data; destination->stream_data=NULL; }
    } else {
        receipt_record(source,index,out);
        right=0; /* right now belongs to the recoverable per-client adapter tag */
    }
    cuis_input_unlock(flags);
 done:
    if (storage) kfree(storage);
    if (table_storage) kfree(table_storage);
    if (right) (void)proc_exec_handle_close(owner,(ULONG)right);
    if (observer) process_observer_release(&observer);
    if (destination) client_put(destination);
    if (source) client_put(source);
    if (owner) kproc_release(owner);
    ksched_cleanup_end(&cleanup);
    return result;
}
#endif
static LONG receipt_query(CUIS_RECEIPT_RECORD *record)
{
    CUIS_CLIENT *source;
    CUIS_RECEIPT_RECORD snapshot;
    CUIS_RECEIPT_SLOT *slot=NULL;
    UBYTE packet[32];
    ULONG address,capacity;
    UQUAD flags,generation;
    if (!record || record->operation || record->slot>=RECEIPT_SLOTS ||
        record->destination || record->destination_low || record->destination_high ||
        record->key_low || record->key_high || record->tag || record->state ||
        record->bytes>sizeof(packet) || (!record->packet != !record->bytes)) return EINVAL;
    address=record->packet;capacity=record->bytes;
    if (capacity>0x100000000ULL-address) return EIMBA;
    flags=cuis_input_lock();source=receipt_source(record);
    if (!source) { cuis_input_unlock(flags); return EACCDN; }
    if (source->receipt_table) slot=&source->receipt_table->slots[record->slot];
    generation=receipt_pair(record->attempt_low,record->attempt_high);
    if (generation && (!slot || !slot->state || slot->generation!=generation)) {
        cuis_input_unlock(flags);return EIHNDL;
    }
    if (slot && slot->state) {
        receipt_record(source,record->slot,&snapshot);memcpy(packet,slot->packet,slot->bytes);
    } else {
        memset(&snapshot,0,sizeof(snapshot));
        snapshot.source=snapshot.source_low=source->handle;snapshot.slot=record->slot;
    }
    snapshot.packet=address;
    cuis_input_unlock(flags);
    if (address && capacity<snapshot.bytes) return ERANGE;
    if (address && snapshot.bytes && kproc_copy_to_user_checked(address,packet,snapshot.bytes)) return EIMBA;
    *record=snapshot;return E_OK;
}
static LONG receipt_prepare(const CUIS_RECEIPT_RECORD *record)
{
    KSCHED_CLEANUP_SCOPE cleanup={0};
    CUIS_CLIENT *source;
    CUIS_RECEIPT_SLOT *slot;
    UBYTE packet[32];
    UQUAD flags;
    LONG result;
    if (!record->bytes || record->bytes>sizeof(packet) || !record->packet ||
        record->bytes>0x100000000ULL-record->packet) return EINVAL;
    ksched_cleanup_begin(&cleanup);
    result=kproc_copy_from_user_checked(packet,record->packet,record->bytes);
    if (!result) {
        flags=cuis_input_lock(); source=receipt_source(record); slot=receipt_exact(source,record);
        if (!slot || slot->state==CUIS_RECEIPT_RECLAIMED) result=EIHNDL;
        else if (slot->state!=CUIS_RECEIPT_BOUND) {
            result=slot->bytes==record->bytes && !memcmp(slot->packet,packet,record->bytes) ? E_OK:EACCDN;
        } else { memcpy(slot->packet,packet,record->bytes); slot->bytes=record->bytes; slot->state=CUIS_RECEIPT_PREPARED; }
        cuis_input_unlock(flags);
    }
    ksched_cleanup_end(&cleanup); return result;
}
static LONG receipt_commit(const CUIS_RECEIPT_RECORD *record)
{
    KSCHED_CLEANUP_SCOPE cleanup={0};
    KPROC *owner=NULL;
    CUIS_CLIENT *source=NULL,*destination;
    CUIS_RECEIPT_SLOT *slot;
    PROCESS_OBSERVER *leases[RECEIPT_SLOTS]={0};
    ULONG tags[RECEIPT_SLOTS]={0},states[RECEIPT_SLOTS],i;
    UQUAD generations[RECEIPT_SLOTS],flags,order,candidate;
    PROCESS_COMPLETION completion;
    LONG result=EACCDN;
    BOOL observing=FALSE;
    ksched_cleanup_begin(&cleanup);
    owner=kproc_retain(run);
    if (!owner) goto done;
    flags=cuis_input_lock(); source=receipt_source(record); slot=receipt_exact(source,record);
    if (!source || source->process!=owner || !slot || slot->state==CUIS_RECEIPT_RECLAIMED) {
        source=NULL; result=EIHNDL; cuis_input_unlock(flags); goto done;
    }
    /* A receipt is the authority even if its observer has been closed or the
     * destination has already drained/retired. No second append is possible. */
    if (slot->committed) { result=(LONG)slot->bytes; source=NULL; cuis_input_unlock(flags); goto done; }
    if (slot->state!=CUIS_RECEIPT_PREPARED) { source=NULL; result=EAGAIN; cuis_input_unlock(flags); goto done; }
    if (!client_get(source)) { source=NULL; result=EAGAIN; cuis_input_unlock(flags); goto done; }
    if (source->receipt_observations==0xffffffffUL) { result=EAGAIN; cuis_input_unlock(flags); goto done; }
    source->receipt_observations++; observing=TRUE;
    for (i=0;i<RECEIPT_SLOTS;i++) {
        CUIS_RECEIPT_SLOT *entry=&source->receipt_table->slots[i];
        states[i]=entry->state; generations[i]=entry->generation;
        if (entry->state==CUIS_RECEIPT_BOUND || entry->state==CUIS_RECEIPT_PREPARED) tags[i]=entry->tag;
    }
    cuis_input_unlock(flags);
    /* Bounded operation-local canonical leases. Never retain copied status or
     * an observer in a transport slot. Imports/releases stay outside exclusion. */
    for (i=0;i<RECEIPT_SLOTS;i++) if (tags[i]) {
        result=proc_exec_retain_observer(owner,tags[i],FALSE,&leases[i]);
        if (result && result!=EIHNDL) goto done;
    }
    flags=cuis_input_lock(); slot=receipt_exact(source,record);
    if (source->retired || owned_client(record->source)!=source || !slot) result=EACCDN;
    else {
        result=E_OK;
        for (i=0;i<RECEIPT_SLOTS;i++) {
            CUIS_RECEIPT_SLOT *entry=&source->receipt_table->slots[i];
            if (states[i]!=entry->state || generations[i]!=entry->generation || (tags[i] && tags[i]!=entry->tag)) {
                result=EAGAIN; break;
            }
        }
    }
    if (!result && ksched_cancellation_pending()) result=EACCDN;
    if (!result && (!leases[record->slot] || !process_observer_ready(leases[record->slot]) ||
        !process_observer_snapshot(leases[record->slot],&completion) || completion.state!=PROCESS_COMPLETED)) result=EAGAIN;
    candidate=result ? 0:receipt_pair(completion.order_low,completion.order_high);
    if (!result && !candidate) result=EAGAIN;
    /* The terminal ordinal is read from KPROC at the publication boundary.
     * An earlier ready BOUND/PREPARED attempt blocks later publishers even if
     * its publishing THREAD stalled or died. Any sibling can recover that
     * endpoint-owned entitlement. No userspace mutex or copied status FIFO. */
    if (!result) for (i=0;i<RECEIPT_SLOTS;i++) if (leases[i] && process_observer_ready(leases[i])) {
        if (!process_observer_snapshot(leases[i],&completion)) { result=EAGAIN; break; }
        order=receipt_pair(completion.order_low,completion.order_high);
        if (completion.state==PROCESS_COMPLETED && order && order<candidate) { result=EAGAIN; break; }
    }
    destination=!result ? find_handle(slot->destination):NULL;
    if (!result && (!destination || destination->retired || !destination->stream_data)) result=EACCDN;
    if (!result && CUIS_STREAM_BYTES-destination->stream_bytes<slot->bytes) result=EAGAIN;
    if (!result) {
        /* The same leaf publishes bytes, origin and durable outcome. */
        stream_append(destination,slot->packet,slot->bytes,source->aes_id,destination==source);
        slot->committed=TRUE; slot->state=CUIS_RECEIPT_COMMITTED;
        result=(LONG)slot->bytes;
    }
    cuis_input_unlock(flags);
 done:
    for (i=0;i<RECEIPT_SLOTS;i++) if (leases[i]) process_observer_release(&leases[i]);
    if (observing) receipt_observation_end(source);
    if (source) client_put(source);
    if (owner) kproc_release(owner);
    ksched_cleanup_end(&cleanup); return result;
}
static LONG receipt_ack(const CUIS_RECEIPT_RECORD *record)
{
    CUIS_RECEIPT_SLOT *slot;
    LONG result=EIHNDL;
    UQUAD flags=cuis_input_lock();
    slot=receipt_exact(receipt_source(record),record);
    if (slot && slot->committed && slot->state!=CUIS_RECEIPT_RECLAIMED) {
        slot->state=CUIS_RECEIPT_ACKNOWLEDGED; result=E_OK;
    }
    cuis_input_unlock(flags); return result;
}
static LONG receipt_reclaim(const CUIS_RECEIPT_RECORD *record)
{
    KSCHED_CLEANUP_SCOPE cleanup={0};
    KPROC *owner=NULL;
    PROCESS_OBSERVER *probe=NULL;
    CUIS_CLIENT *source=NULL;
    CUIS_RECEIPT_SLOT *slot;
    ULONG tag;
    UQUAD flags;
    LONG result=EIHNDL;
    ksched_cleanup_begin(&cleanup);
    owner=kproc_retain(run);
    if (!owner) { result=EACCDN; goto done; }
    flags=cuis_input_lock();
    source=receipt_source(record); slot=receipt_exact(source,record);
    if (!slot || source->process!=owner) { source=NULL; cuis_input_unlock(flags); goto done; }
    if (slot->state==CUIS_RECEIPT_RECLAIMED) {
        source=NULL; result=E_OK; cuis_input_unlock(flags); goto done;
    }
    if (slot->state==CUIS_RECEIPT_COMMITTED) {
        source=NULL; result=EACCDN; cuis_input_unlock(flags); goto done;
    }
    if (!client_get(source)) { source=NULL; result=EAGAIN; cuis_input_unlock(flags); goto done; }
    tag=slot->tag;
    cuis_input_unlock(flags);
    /* BIND creates only committed ASYNC rights. Their scalar never recycles;
     * EIHNDL proves CLOSE consumed it, including an error-returning CLOSE.
     * Import/release outside GUI exclusion; generic CLOSE has no GUI callback. */
    result=proc_exec_retain_observer(owner,tag,FALSE,&probe);
    if (!result) { result=EACCDN; goto done; }
    if (result!=EIHNDL) goto done;
    flags=cuis_input_lock(); slot=receipt_exact(source,record);
    if (source->retired || owned_client(record->source)!=source || !slot) result=EACCDN;
    else if (slot->state==CUIS_RECEIPT_RECLAIMED) result=E_OK;
    else if (slot->state==CUIS_RECEIPT_COMMITTED) result=EACCDN;
    else if (source->receipt_observations) result=EAGAIN;
    else {
        slot->state=CUIS_RECEIPT_RECLAIMED;
#if CONF_WITH_CUIS_RECEIPT_TEST
        receipt_active--;
#endif
        result=E_OK;
    }
    cuis_input_unlock(flags);
 done:
    if (probe) process_observer_release(&probe);
    if (source) client_put(source);
    if (owner) kproc_release(owner);
    ksched_cleanup_end(&cleanup);
    return result;
}

#if CONF_WITH_CUIS_RECEIPT_TEST
LONG cuis_receipt_test_query(CUIS_RECEIPT_RECORD *record) { return receipt_query(record); }
LONG cuis_receipt_test_prepare(const CUIS_RECEIPT_RECORD *record) { return receipt_prepare(record); }
LONG cuis_receipt_test_commit(const CUIS_RECEIPT_RECORD *record) { return receipt_commit(record); }
LONG cuis_receipt_test_ack(const CUIS_RECEIPT_RECORD *record) { return receipt_ack(record); }
LONG cuis_receipt_test_reclaim(const CUIS_RECEIPT_RECORD *record) { return receipt_reclaim(record); }
#endif

LONG cuis_stream_receipt(UQUAD session, UQUAD action, UQUAD address, UQUAD size)
{
    KSCHED_CLEANUP_SCOPE cleanup={0};
    CUIS_RECEIPT_RECORD record;
    LONG result;
    if (session>0x7fffffffULL || action>CUIS_RECEIPT_RECLAIM || size!=sizeof(record)) return EINVAL;
    if (!address || address>0x100000000ULL-sizeof(record)) return EIMBA;
    if (address & 3) return EINVAL;
    ksched_cleanup_begin(&cleanup);
    result=kproc_copy_from_user_checked(&record,address,sizeof(record));
    if (!result && (record.source!=session || record.operation)) result=EINVAL;
    if (!result) switch (action) {
    case CUIS_RECEIPT_QUERY:
        result=receipt_query(&record);
        if (!result) result=kproc_copy_to_user_checked(address,&record,sizeof(record));
        break;
    case CUIS_RECEIPT_PREPARE: result=receipt_prepare(&record); break;
    case CUIS_RECEIPT_COMMIT: result=receipt_commit(&record); break;
    case CUIS_RECEIPT_ACK: result=receipt_ack(&record); break;
    case CUIS_RECEIPT_RECLAIM: result=receipt_reclaim(&record); break;
    }
    ksched_cleanup_end(&cleanup);
    return result;
}
