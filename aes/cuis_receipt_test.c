/* Included by cuis_session.c only under the independent default-off test gate.
 * Fixed transport metadata, never a PROCESS status or observer registry. */
#define RECEIPT_SLOTS 16U
#define RECEIPT_TABLES 16U
typedef struct {
    UQUAD generation, key;
    ULONG destination, tag, state, bytes;
    BOOL committed;
    UBYTE packet[32];
} CUIS_RECEIPT_SLOT;
typedef struct cuis_receipt_table {
    CUIS_CLIENT *owner; /* registry lifetime only; detached at endpoint retirement */
    CUIS_RECEIPT_SLOT slots[RECEIPT_SLOTS];
} CUIS_RECEIPT_TABLE;
static CUIS_RECEIPT_TABLE receipt_tables[RECEIPT_TABLES];
static ULONG receipt_active;
static UQUAD receipt_pair(ULONG low,ULONG high) { return ((UQUAD)high<<32)|low; }
static void receipt_retire(CUIS_CLIENT *client)
{
    CUIS_RECEIPT_TABLE *table=client->receipt_table;
    ULONG i;
    if (!table) return;
    client->receipt_table=NULL;
    for (i=0;i<RECEIPT_SLOTS;i++)
        if (table->slots[i].state && table->slots[i].state!=CUIS_RECEIPT_RECLAIMED) receipt_active--;
    memset(table,0,sizeof(*table));
}
static CUIS_CLIENT *receipt_source(const CUIS_RECEIPT_TEST_RECORD *record)
{
    CUIS_CLIENT *source;
    if (record->operation || record->slot>=RECEIPT_SLOTS || record->source>0x7fffffffUL ||
        record->source_low!=record->source || record->source_high) return NULL;
    source=owned_client(record->source);
    return source && !source->retired ? source:NULL;
}
static CUIS_RECEIPT_SLOT *receipt_exact(CUIS_CLIENT *source,const CUIS_RECEIPT_TEST_RECORD *record)
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
static void receipt_record(CUIS_CLIENT *source,ULONG index,CUIS_RECEIPT_TEST_RECORD *out)
{
    CUIS_RECEIPT_SLOT *slot=&source->receipt_table->slots[index];
    memset(out,0,sizeof(*out));
    out->source=out->source_low=source->handle;
    out->destination=out->destination_low=slot->destination;
    out->slot=index; out->attempt_low=(ULONG)slot->generation; out->attempt_high=(ULONG)(slot->generation>>32);
    out->key_low=(ULONG)slot->key; out->key_high=(ULONG)(slot->key>>32);
    out->bytes=slot->bytes; out->state=slot->state; out->tag=slot->tag;
}
LONG cuis_receipt_test_launch(ULONG source_id,ULONG destination_id,ULONG index,
                             char *path,char *tail,char *environment,CUIS_RECEIPT_TEST_RECORD *out)
{
    KSCHED_CLEANUP_SCOPE cleanup={0};
    KPROC *owner=NULL;
    PROCESS_OBSERVER *observer=NULL;
    PROCESS_COMPLETION completion;
    CUIS_CLIENT *source=NULL,*destination=NULL;
    CUIS_RECEIPT_TABLE *table;
    CUIS_RECEIPT_SLOT *slot;
    UBYTE *storage=NULL;
    UQUAD flags,key;
    ULONG i;
    LONG right=0,result=EINVAL;
    BOOL installed=FALSE;
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
    cuis_input_unlock(flags);
    /* Reserve all fallible transport storage before exposing the entitlement. */
    storage=kalloc(CUIS_STREAM_BYTES*(1+sizeof(UWORD)));
    if (!storage) { result=ENSMEM; goto done; }
    flags=cuis_input_lock();
    if (source->retired || destination->retired || owned_client(source_id)!=source ||
        find_handle(destination_id)!=destination || ksched_cancellation_pending()) {
        result=EACCDN; cuis_input_unlock(flags); goto done;
    }
    table=source->receipt_table;
    if (!table) {
        for (i=0;i<RECEIPT_TABLES && receipt_tables[i].owner;i++);
        if (i==RECEIPT_TABLES) { result=EAGAIN; cuis_input_unlock(flags); goto done; }
        table=&receipt_tables[i]; table->owner=source; source->receipt_table=table;
    }
    slot=&table->slots[index];
    if ((slot->state && slot->state!=CUIS_RECEIPT_RECLAIMED) || receipt_active==RECEIPT_SLOTS*RECEIPT_TABLES) {
        result=EAGAIN; cuis_input_unlock(flags); goto done;
    }
    if (slot->generation==~0ULL) { result=ERANGE; cuis_input_unlock(flags); goto done; }
    for (i=0;i<RECEIPT_SLOTS;i++) if (table->slots[i].state &&
        table->slots[i].state!=CUIS_RECEIPT_RECLAIMED && table->slots[i].key==key) {
        result=EAGAIN; cuis_input_unlock(flags); goto done;
    }
    slot->generation++; slot->key=key; slot->destination=destination_id; slot->tag=(ULONG)right;
    slot->bytes=0; slot->committed=FALSE; slot->state=CUIS_RECEIPT_BOUND; receipt_active++;
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
    if (right) (void)proc_exec_handle_close(owner,(ULONG)right);
    if (observer) process_observer_release(&observer);
    if (destination) client_put(destination);
    if (source) client_put(source);
    if (owner) kproc_release(owner);
    ksched_cleanup_end(&cleanup);
    return result;
}
LONG cuis_receipt_test_query(CUIS_RECEIPT_TEST_RECORD *record)
{
    CUIS_CLIENT *source;
    CUIS_RECEIPT_TEST_RECORD snapshot;
    CUIS_RECEIPT_SLOT *slot;
    UBYTE packet[32];
    ULONG address,capacity;
    UQUAD flags=cuis_input_lock();
    source=receipt_source(record);
    if (!source || !source->receipt_table) { cuis_input_unlock(flags); return EACCDN; }
    slot=&source->receipt_table->slots[record->slot];
    if (!slot->state || (receipt_pair(record->attempt_low,record->attempt_high) && !receipt_exact(source,record))) {
        cuis_input_unlock(flags); return EIHNDL;
    }
    address=record->packet; capacity=record->bytes;
    receipt_record(source,record->slot,&snapshot); memcpy(packet,slot->packet,slot->bytes);
    cuis_input_unlock(flags);
    if (address && (capacity<snapshot.bytes || kproc_copy_to_user_checked(address,packet,snapshot.bytes))) return EIMBA;
    *record=snapshot; return E_OK;
}
LONG cuis_receipt_test_prepare(const CUIS_RECEIPT_TEST_RECORD *record)
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
LONG cuis_receipt_test_commit(const CUIS_RECEIPT_TEST_RECORD *record)
{
    KSCHED_CLEANUP_SCOPE cleanup={0};
    KPROC *owner=NULL;
    CUIS_CLIENT *source=NULL,*destination;
    CUIS_RECEIPT_SLOT *slot;
    PROCESS_OBSERVER *leases[RECEIPT_SLOTS]={0};
    ULONG tags[RECEIPT_SLOTS]={0},states[RECEIPT_SLOTS],i,at;
    UQUAD generations[RECEIPT_SLOTS],flags,order,candidate;
    UWORD *senders;
    PROCESS_COMPLETION completion;
    LONG result=EACCDN;
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
        if (result) goto done;
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
        at=(destination->stream_head+destination->stream_bytes)%CUIS_STREAM_BYTES;
        senders=(UWORD *)(destination->stream_data+CUIS_STREAM_BYTES);
        /* No allocation, usercopy, callback, scheduling or failure from the
         * first byte until provenance, byte count and receipt are committed. */
        for (i=0;i<slot->bytes;i++) {
            destination->stream_data[(at+i)%CUIS_STREAM_BYTES]=slot->packet[i];
            senders[(at+i)%CUIS_STREAM_BYTES]=source->aes_id;
        }
        destination->stream_bytes+=slot->bytes;
        slot->committed=TRUE; slot->state=CUIS_RECEIPT_COMMITTED;
        result=(LONG)slot->bytes;
    }
    cuis_input_unlock(flags);
 done:
    for (i=0;i<RECEIPT_SLOTS;i++) if (leases[i]) process_observer_release(&leases[i]);
    if (source) client_put(source);
    if (owner) kproc_release(owner);
    ksched_cleanup_end(&cleanup); return result;
}
LONG cuis_receipt_test_ack(const CUIS_RECEIPT_TEST_RECORD *record)
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
LONG cuis_receipt_test_reclaim(const CUIS_RECEIPT_TEST_RECORD *record)
{
    CUIS_RECEIPT_SLOT *slot;
    LONG result=EIHNDL;
    UQUAD flags=cuis_input_lock();
    slot=receipt_exact(receipt_source(record),record);
    if (slot) {
        if (slot->state==CUIS_RECEIPT_RECLAIMED) result=E_OK;
        else if (slot->state==CUIS_RECEIPT_COMMITTED) result=EACCDN;
        else { slot->state=CUIS_RECEIPT_RECLAIMED; receipt_active--; result=E_OK; }
    }
    cuis_input_unlock(flags); return result;
}
