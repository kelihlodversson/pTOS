/* Actual same-PD endpoint admission through production CUIS (host CPU model). */
static int client_identity_checks(void)
{
    LONG a,b,replacement,sender,old_sender;
    CUIS_PEER_IDENTITY_DATA one,two,next;
    ULONG references=process_references;
    UWORD saved=last_aes_id;
    run=&first;
    a=cuis_service(CUIS_SESSION_START,0);
    b=cuis_service(CUIS_SESSION_START,CUIS_SESSION_NEW);
    CHECK(a>=0 && b>a && *find_owner(run)==find_handle(a));
    CHECK(cuis_service(CUIS_SESSION_START,0)==a && process_references==references+2);
    CHECK(cuis_peer_service(CUIS_PEER_QUERY,a,a,CUIS_PEER_IDENTITY,0x20000,16)==16);
    memcpy(&one,stream_output,16);
    CHECK(cuis_peer_service(CUIS_PEER_QUERY,b,b,CUIS_PEER_IDENTITY,0x20000,16)==16);
    memcpy(&two,stream_output,16);
    CHECK(one.client_id!=two.client_id && one.process_low==two.process_low &&
          one.process_high==two.process_high && !two.reserved);
    CHECK(cuis_peer_service(CUIS_PEER_NEXT,a,(UQUAD)-1,0,0,0)==a &&
          cuis_peer_service(CUIS_PEER_NEXT,b,a,0,0,0)==b);
    CHECK(cuis_stream_service(CUIS_STREAM_WRITE,a,0x10000,16,1,(ULONG)b+1)==EINVFN);
    CHECK(cuis_stream_service(CUIS_STREAM_WRITE,a,0x10000,16,0,(ULONG)b+1)==16);
    CHECK(cuis_stream_service(CUIS_STREAM_READ,a,0x20000,16,0,0x1000)==16 && copied[0]==(LONG)two.client_id);
    CHECK(cuis_stream_service(CUIS_STREAM_WRITE,b,0x10000,16,0,(ULONG)a+1)==16);
    CHECK(cuis_stream_service(CUIS_STREAM_READ,b,0x20000,16,0,0x1000)==16 && copied[0]==(LONG)one.client_id);
    /* Sender retirement cannot lend its historical identity to a replacement. */
    CHECK(cuis_stream_service(CUIS_STREAM_WRITE,a,0x10000,16,0,(ULONG)b+1)==16);
    old_sender=two.client_id;
    CHECK(cuis_service(CUIS_SESSION_END,b)==0 && cuis_service(CUIS_SESSION_QUERY,a)==0);
    CHECK(cuis_peer_service(CUIS_PEER_QUERY,a,b,CUIS_PEER_IDENTITY,0x20000,16)==EFILNF);
    replacement=cuis_service(CUIS_SESSION_START,CUIS_SESSION_NEW);
    CHECK(replacement>b && cuis_peer_service(CUIS_PEER_QUERY,a,replacement,CUIS_PEER_IDENTITY,0x20000,16)==16);
    memcpy(&next,stream_output,16); CHECK((LONG)next.client_id!=old_sender);
    CHECK(cuis_stream_service(CUIS_STREAM_READ,a,0x20000,16,0,0x1000)==16 && copied[0]==old_sender);
    run=&second;
    CHECK(cuis_stream_service(CUIS_STREAM_WRITE,a,0x10000,16,0,(ULONG)replacement+1)==EACCDN);
    CHECK(cuis_service(CUIS_SESSION_END,replacement)==EACCDN);
    run=&first; fail_allocation=TRUE;
    CHECK(cuis_service(CUIS_SESSION_START,CUIS_SESSION_NEW)==ENSMEM && process_references==references+2);
    fail_allocation=FALSE;
    CHECK(cuis_service(CUIS_SESSION_END,a)==0 && cuis_service(CUIS_SESSION_QUERY,replacement)==0);
    CHECK(cuis_stream_service(CUIS_STREAM_WRITE,replacement,0x10000,16,0,(ULONG)replacement+1)==16 &&
          cuis_stream_service(CUIS_STREAM_READ,replacement,0x20000,16,0,0x1000)==16 && copied[0]==(LONG)next.client_id);
    /* A surviving explicit endpoint does not become the implicit default. */
    CHECK(!*find_owner(run) && *find_any_owner(run)==find_handle(replacement));
    a=cuis_service(CUIS_SESSION_START,0);
    CHECK(a>replacement && !find_handle(a)->explicit_client &&
          *find_owner(run)==find_handle(a) && cuis_service(CUIS_SESSION_START,0)==a);
    CHECK(cuis_peer_service(CUIS_PEER_QUERY,a,next.client_id,CUIS_PEER_HANDLE,0,0)==replacement);
    CHECK(cuis_peer_service(CUIS_PEER_QUERY,a,old_sender,CUIS_PEER_HANDLE,0,0)==EFILNF);
    CHECK(cuis_peer_service(CUIS_PEER_QUERY,a,0,CUIS_PEER_HANDLE,0,0)==EINVAL);
    CHECK(cuis_peer_service(CUIS_PEER_QUERY,a,next.client_id,CUIS_PEER_HANDLE,1,0)==EINVAL);
    cuis_process_exit(&first); cuis_process_exit(&first);
    CHECK(!cuis_session_count() && process_references==references && allocations==frees);
    /* Exhaustion is fail-closed, with no handle, reference or allocation change. */
    last_aes_id=32766;
    a=cuis_service(CUIS_SESSION_START,0); CHECK(a>=0 && find_handle(a)->aes_id==32767);
    sender=next_handle;
    CHECK(cuis_service(CUIS_SESSION_START,CUIS_SESSION_NEW)==ENSMEM && (LONG)next_handle==sender &&
          process_references==references+1 && cuis_service(CUIS_SESSION_START,0)==a);
    cuis_process_exit(&first); CHECK(!cuis_session_count() && process_references==references);
    last_aes_id=saved; /* Host-only isolated boundary injection; no live clients. */
    return 0;
}
