/* Real heap/free + production CUIS: ASan can detect retirement UAFs. */
#define LSWRITE(h,n,b) cuis_stream_service(CUIS_STREAM_WRITE,h,0x10000,n,b,0)
#define LSREAD(h,n,b) cuis_stream_service(CUIS_STREAM_READ,h,0x20000,n,b,0x1000)
static LONG lifetime_source, lifetime_receiver, lifetime_replacement;
static CUIS_CLIENT *lifetime_stream_source, *lifetime_stream_receiver;
static int stream_action, stream_bad;
static ULONG stream_checkpoint_visits;
static void stream_retire_source(void)
{
    CUIS_STREAM_OPERATION *operation=lifetime_stream_source->stream_write ?
        lifetime_stream_source->stream_write : lifetime_stream_source->stream_read;
    if (!operation || lifetime_stream_source->references<2) stream_bad=1;
    if (cuis_service(CUIS_SESSION_END,lifetime_source)) stream_bad=1;
    if (!lifetime_stream_source->retired || operation->state!=STREAM_RETIRED ||
        lifetime_stream_source->references!=1) stream_bad=1;
    if (operation->packet && operation->packet->sender_pid!=1) stream_bad=1;
    lifetime_replacement=cuis_service(CUIS_SESSION_START,0);
    if (lifetime_replacement<=lifetime_source || *find_owner(run)==lifetime_stream_source ||
        cuis_service(CUIS_SESSION_END,lifetime_source)!=EACCDN) stream_bad=1;
}
static void stream_block_hook(void)
{
    PD *saved=run;
    CUIS_STREAM_OPERATION *operation=lifetime_stream_source->stream_write;
    if (stream_action==3) {
        operation=lifetime_stream_source->stream_read;
        if (!operation || operation->state!=STREAM_PENDING ||
            LSREAD(lifetime_source,1,0)!=EACCDN ||
            cuis_stream_service(CUIS_STREAM_PEEK,lifetime_source,0x20000,1,0,0)!=EACCDN ||
            cuis_stream_service(CUIS_STREAM_PENDING,lifetime_source,0,0,0,0)) stream_bad=1;
        if (LSWRITE(lifetime_source,16,0)!=16) stream_bad=1; /* Independent slot makes progress. */
        return;
    }
    if (!operation || operation->state!=STREAM_PENDING || !operation->packet ||
        lifetime_stream_source->references<2 || lifetime_stream_receiver->references<2) stream_bad=1;
    if (LSWRITE(lifetime_receiver,1,0)!=EACCDN ||
        lifetime_stream_source->stream_write!=operation) stream_bad=1;
    if (stream_action==1) { stream_retire_source(); return; }
    if (stream_action==2) {
        run=&second;
        if (cuis_service(CUIS_SESSION_END,lifetime_receiver)) stream_bad=1;
        if (!lifetime_stream_receiver->retired || lifetime_stream_receiver->references!=1 ||
            !lifetime_stream_receiver->stream_data) stream_bad=1;
        lifetime_replacement=cuis_service(CUIS_SESSION_START,0);
        if (lifetime_replacement<=lifetime_receiver) stream_bad=1;
        run=saved; return;
    }
    if (stream_action==4 && !cancel_pending) {
        cancel_pending=TRUE;
        input_operation_hook=stream_block_hook; /* Repeated request cannot free an active packet. */
        return;
    }
    run=&second;
    if (LSREAD(lifetime_receiver,16,0)!=16) stream_bad=1;
    run=saved;
    stream_input[0]=99; /* Writer must deliver its earlier immutable snapshot. */
}
static void stream_both_retire(void)
{
    CUIS_STREAM_OPERATION *write=lifetime_stream_source->stream_write;
    CUIS_STREAM_OPERATION *read=lifetime_stream_source->stream_read;
    if (!write || !read || lifetime_stream_source->references!=3 ||
        cuis_service(CUIS_SESSION_END,lifetime_source)) stream_bad=1;
    if (write->state!=STREAM_RETIRED || read->state!=STREAM_RETIRED ||
        lifetime_stream_source->references!=2 || write->packet->data[0]!=0x35) stream_bad=1;
    lifetime_replacement=cuis_service(CUIS_SESSION_START,0);
}
static void stream_both_block(void)
{
    input_operation_hook=stream_both_retire;
    if (LSREAD(lifetime_source,1,1)!=EACCDN || lifetime_stream_source->references!=1 ||
        lifetime_stream_source->stream_read || !lifetime_stream_source->stream_write) stream_bad=1;
}
static void stream_copy_hook(int phase)
{
    PD *saved=run;
    int expected=stream_action==5?0:stream_action==6?1:2;
    if (phase!=expected) { stream_operation_hook=stream_copy_hook; return; }
    if (stream_action==5 && phase==0) {
        /* No retained state exists across potentially non-returning preflight. */
        if (lifetime_stream_source->stream_write || lifetime_stream_source->stream_read ||
            lifetime_stream_source->references!=1) stream_bad=1;
        if (cuis_service(CUIS_SESSION_END,lifetime_source)) stream_bad=1;
        lifetime_replacement=cuis_service(CUIS_SESSION_START,0);
        return;
    }
    if (stream_action==6 && phase==1) {
        if (LSWRITE(lifetime_receiver,1,0)!=EACCDN ||
            lifetime_stream_source->stream_write->state!=STREAM_ADMITTED) stream_bad=1;
        stream_retire_source(); return;
    }
    if (stream_action==7 && phase==2) { stream_retire_source(); return; }
    if (stream_action==9 && phase==2) {
        LONG token=(LONG)cohort.token;
        run=&second;
        if (cuis_cohort_service(CUIS_COHORT_CANCEL,lifetime_receiver,token,0,0,0,0)) stream_bad=1;
        run=saved; return;
    }
    if (stream_action==8 && phase==2) {
        if (LSREAD(lifetime_source,1,0)!=EACCDN ||
            cuis_stream_service(CUIS_STREAM_PEEK,lifetime_source,0x20000,1,0,0)!=EACCDN) stream_bad=1;
        run=&second;
        if (LSWRITE(lifetime_source,8,0)!=8) stream_bad=1;
        run=saved;
    }
}
static int stream_lifetime_checks(void)
{
    ULONG before, sequence, i, cycle, head, count;
    int action, failure;
    LONG result;
    stream_heap_mode=TRUE;
    for (cycle=0;cycle<3;cycle++) {
        for (action=0;action<9;action++) {
            run=&first; lifetime_source=cuis_service(CUIS_SESSION_START,0);
            lifetime_stream_source=*find_owner(run);
            run=&second; lifetime_receiver=cuis_service(CUIS_SESSION_START,0);
            lifetime_stream_receiver=*find_owner(run); run=&first;
            CHECK(lifetime_source>=0 && lifetime_receiver>=0);
            memset(stream_input,0x35,sizeof(stream_input)); stream_action=action;
            if (!action) {
                for (failure=0;failure<3;failure++) {
                    before=allocations-frees; sequence=(ULONG)lifetime_stream_source->stream_sequence;
                    fail_at_allocation=allocations+(ULONG)failure;
                    CHECK(LSWRITE(lifetime_receiver,16,0)==ENSMEM);
                    fail_at_allocation=0;
                    CHECK(allocations-frees==before && !lifetime_stream_source->stream_write &&
                          !lifetime_stream_receiver->stream_data && lifetime_stream_source->references==1 &&
                          lifetime_stream_receiver->references==1);
                    CHECK(lifetime_stream_source->stream_sequence==sequence+(failure!=0));
                }
                lifetime_stream_source->stream_sequence=~0ULL;
                CHECK(LSWRITE(lifetime_receiver,16,0)==ERANGE && !lifetime_stream_source->stream_write);
                lifetime_stream_source->stream_sequence=0;
                lifetime_stream_receiver->references=0xffffffffUL;
                before=allocations-frees;
                CHECK(LSWRITE(lifetime_receiver,16,0)==EACCDN && allocations-frees==before &&
                      lifetime_stream_source->references==1 && !lifetime_stream_source->stream_sequence);
                lifetime_stream_receiver->references=1;
                lifetime_stream_source->references=0xffffffffUL;
                CHECK(LSWRITE(lifetime_receiver,16,0)==EACCDN && !lifetime_stream_source->stream_write &&
                      lifetime_stream_receiver->references==1);
                lifetime_stream_source->references=1;
                /* Retirement invalidates lookup immediately, even before the
                 * registry unlink at the end of session teardown. */
                lifetime_stream_source->retired=TRUE;
                CHECK(cuis_stream_service(CUIS_STREAM_PENDING,lifetime_source,0,0,0,0)==EACCDN &&
                      LSREAD(lifetime_source,0,0)==EACCDN && LSWRITE(lifetime_receiver,0,0)==EACCDN);
                lifetime_stream_source->retired=FALSE; lifetime_stream_receiver->retired=TRUE;
                CHECK(LSWRITE(lifetime_receiver,0,0)==EACCDN);
                lifetime_stream_receiver->retired=FALSE;
            }
            if (action<=2 || action==4) {
                CHECK(LSWRITE(lifetime_receiver,32767,0)==32767 &&
                      LSWRITE(lifetime_receiver,32767,0)==32767 && LSWRITE(lifetime_receiver,2,0)==2);
                input_operation_hook=stream_block_hook; stream_checkpoint_visits=cancellation_visits;
                result=LSWRITE(lifetime_receiver,16,1);
                CHECK(result==(action==1||action==2?EACCDN:16) && !input_operation_hook && !stream_bad && input_irqs);
                CHECK(cancellation_visits==stream_checkpoint_visits); /* No admitted stream cancellation. */
                cancel_pending=FALSE;
                if (action==1) {
                    CHECK((*find_owner(&first))->handle==(ULONG)lifetime_replacement &&
                          !(*find_owner(&first))->stream_write && lifetime_stream_receiver->stream_bytes==CUIS_STREAM_BYTES);
                } else if (action==2) {
                    CHECK((*find_owner(&second))->handle==(ULONG)lifetime_replacement &&
                          !(*find_owner(&second))->stream_bytes && !lifetime_stream_source->stream_write);
                } else {
                    ULONG tail=(lifetime_stream_receiver->stream_head+lifetime_stream_receiver->stream_bytes-16)%CUIS_STREAM_BYTES;
                    CHECK(lifetime_stream_receiver->stream_data[tail]==0x35 && stream_input[0]==99 &&
                          !lifetime_stream_source->stream_write);
                }
            } else if (action==3) {
                input_operation_hook=stream_block_hook;
                CHECK(LSREAD(lifetime_source,16,1)==16 && !input_operation_hook && !stream_bad &&
                      copied[0]==1 && stream_output[0]==0x35 && !lifetime_stream_source->stream_bytes &&
                      !lifetime_stream_source->stream_read && !lifetime_stream_source->stream_write);
            } else if (action==5 || action==6) {
                stream_operation_hook=stream_copy_hook;
                if (action==6) {
                    /* Let faulting preflight finish; inject into resident copy. */
                    CHECK(kproc_validate_user_range(0x10000,16));
                    stream_operation_hook=stream_copy_hook;
                }
                CHECK(LSWRITE(lifetime_receiver,16,0)==EACCDN && !stream_operation_hook && !stream_bad &&
                      (*find_owner(&first))->handle==(ULONG)lifetime_replacement &&
                      !lifetime_stream_receiver->stream_bytes);
            } else {
                run=&second; CHECK(LSWRITE(lifetime_source,16,0)==16); run=&first;
                stream_operation_hook=stream_copy_hook;
                CHECK(kproc_validate_user_write(0x20000,16));
                stream_operation_hook=stream_copy_hook;
                result=action==8?cuis_stream_service(CUIS_STREAM_PEEK,lifetime_source,0x20000,16,0,0):
                    LSREAD(lifetime_source,16,0);
                CHECK(result==(action==7?EACCDN:16) && !stream_operation_hook && !stream_bad);
                if (action==7) CHECK((*find_owner(&first))->handle==(ULONG)lifetime_replacement);
                else {
                    CHECK(lifetime_stream_source->stream_bytes==24 && lifetime_stream_source->stream_head==0 &&
                          stream_output[0]==0x35 && !lifetime_stream_source->stream_read);
                    CHECK(LSREAD(lifetime_source,8,0)==8 && copied[0]==2 &&
                          LSREAD(lifetime_source,16,0)==16 && !lifetime_stream_source->stream_bytes);
                }
            }
            cuis_process_exit(&first); cuis_process_exit(&second);
            CHECK(allocations==frees && !stream_heap_bad && !stream_operation_hook && input_irqs);
        }
        /* Wrapped copyout may modify user bytes before failure, never kernel
         * cursor/provenance. Repeated PEEK preserves bytes but publishes metadata. */
        run=&first; lifetime_source=cuis_service(CUIS_SESSION_START,0); lifetime_stream_source=*find_owner(run);
        CHECK(LSWRITE(lifetime_source,32767,0)==32767 && LSREAD(lifetime_source,32767,0)==32767 &&
              LSWRITE(lifetime_source,32763,0)==32763 && LSREAD(lifetime_source,32763,0)==32763);
        CHECK(lifetime_stream_source->stream_head==65530 && LSWRITE(lifetime_source,16,0)==16);
        head=lifetime_stream_source->stream_head; count=lifetime_stream_source->stream_bytes;
        lifetime_stream_source->cohort_read_token=77; lifetime_stream_source->cohort_read_bytes=19;
        fail_copy_address=0x20006; stream_output[0]=0;
        CHECK(LSREAD(lifetime_source,16,0)==EIMBA && stream_output[0]==0x35 &&
              lifetime_stream_source->stream_head==head && lifetime_stream_source->stream_bytes==count &&
              lifetime_stream_source->cohort_read_token==77 && lifetime_stream_source->cohort_read_bytes==19);
        fail_copy_address=0;
        for (i=0;i<2;i++) CHECK(cuis_stream_service(CUIS_STREAM_PEEK,lifetime_source,0x20000,16,0,0)==16 &&
                               lifetime_stream_source->stream_head==head && lifetime_stream_source->stream_bytes==count);
        CHECK(LSREAD(lifetime_source,16,0)==16 && copied[0]==1 && !lifetime_stream_source->stream_bytes);
        cuis_process_exit(&first);
        CHECK(allocations==frees && !stream_heap_bad);
    }
    /* Independent READ and WRITE slots may both block. Retiring their source
     * must leave each continuation as the sole cleanup owner of its own record. */
    run=&first; lifetime_source=cuis_service(CUIS_SESSION_START,0); lifetime_stream_source=*find_owner(run);
    run=&second; lifetime_receiver=cuis_service(CUIS_SESSION_START,0); run=&first;
    CHECK(LSWRITE(lifetime_receiver,32767,0)==32767 && LSWRITE(lifetime_receiver,32767,0)==32767 &&
          LSWRITE(lifetime_receiver,2,0)==2);
    input_operation_hook=stream_both_block;
    CHECK(LSWRITE(lifetime_receiver,16,1)==EACCDN && !input_operation_hook && !stream_bad &&
          (*find_owner(&first))->handle==(ULONG)lifetime_replacement &&
          !(*find_owner(&first))->stream_read && !(*find_owner(&first))->stream_write);
    cuis_process_exit(&first); cuis_process_exit(&second);
    CHECK(allocations==frees && !stream_heap_bad);
    /* Provenance is committed from current cohort state, including teardown
     * during a prepared copy. PEEK deliberately publishes metadata, not bytes. */
    run=&first; lifetime_source=cuis_service(CUIS_SESSION_START,0); lifetime_stream_source=*find_owner(run);
    run=&second; lifetime_receiver=cuis_service(CUIS_SESSION_START,0);
    result=cuis_cohort_service(CUIS_COHORT_BEGIN,lifetime_receiver,0,0,0,0,0);
    CHECK(result>0);
    CHECK(!cuis_cohort_service(CUIS_COHORT_STAGE,lifetime_receiver,result,lifetime_source,0x10000,16,0) &&
          !cuis_cohort_service(CUIS_COHORT_COMMIT,lifetime_receiver,result,0x10000,16,0,0));
    run=&first;
    CHECK(!cuis_cohort_service(CUIS_COHORT_PUBLISH,lifetime_source,result,0,0,0,0));
    CHECK(cuis_stream_service(CUIS_STREAM_PEEK,lifetime_source,0x20000,4,0,0)==4 &&
          lifetime_stream_source->cohort_read_token==(ULONG)result && lifetime_stream_source->cohort_read_bytes==4);
    fail_copy_address=0x1000;
    CHECK(LSREAD(lifetime_source,8,0)==EIMBA && lifetime_stream_source->cohort_unread==16 &&
          lifetime_stream_source->stream_bytes==16 && lifetime_stream_source->cohort_read_bytes==4);
    fail_copy_address=0;
    CHECK(LSREAD(lifetime_source,4,0)==4 && lifetime_stream_source->cohort_read_offset==0 &&
          lifetime_stream_source->cohort_unread==12);
    CHECK(cuis_stream_service(CUIS_STREAM_PEEK,lifetime_source,0x20000,4,0,0)==4 &&
          lifetime_stream_source->cohort_read_offset==4 && lifetime_stream_source->cohort_unread==12);
    stream_action=9; stream_operation_hook=stream_copy_hook;
    CHECK(LSREAD(lifetime_source,4,0)==4 && !stream_bad && !stream_operation_hook &&
          !cohort.owner && !lifetime_stream_source->cohort_read_token && lifetime_stream_source->stream_bytes==8);
    CHECK(LSREAD(lifetime_source,8,0)==8 && copied[0]==2 && !lifetime_stream_source->stream_bytes);
    cuis_process_exit(&first); cuis_process_exit(&second);
    CHECK(allocations==frees && !stream_heap_bad);
    for (i=0;i<stream_heap_count;i++) CHECK(!stream_heap[i].live);
    stream_heap_mode=FALSE; allocations=frees=0; next_handle=next_window_handle=0;
    __builtin_printf("CUIS retained stream operations: real free/retirement, slots, packet snapshots, copyout rollback and wrap: PASS (host)\n");
    return 0;
}
#undef LSREAD
#undef LSWRITE
