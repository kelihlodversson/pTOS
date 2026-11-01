/* Shared opaque context snapshots use the production scalar CUIS service. */
#define CTX(h,s,a,p,n,o) cuis_context(h,s,a,p,n,o)
static LONG context_a, context_b;
static BOOL context_peer_copy, context_read_peer, context_exit_copy;
static UBYTE context_peer_value = 99;
static void context_copy_hook(void)
{
    if (context_exit_copy) { context_exit_copy = FALSE; cuis_process_exit(run); return; }
    if (!context_peer_copy) return;
    context_peer_copy = FALSE;
    /* Model a reentrant handoff while a user copy runs with IRQs enabled. */
    cuis_display_lock(context_a,0,0);
    run = &second;
    cuis_display_lock(context_b,1,0);
    stream_input[4096] = context_peer_value;
    cuis_context(context_b,0,CUIS_CONTEXT_WRITE,0x11000,1,2);
    cuis_display_lock(context_b,0,0);
    run = &first;
    cuis_display_lock(context_a,1,0);
}
static void context_read_hook(void)
{
    if (!context_read_peer) return;
    context_read_peer = FALSE; context_peer_copy = TRUE; context_peer_value = 77;
    context_copy_hook();
}
static int context_checks(void)
{
    ULONG i, before;
    UQUAD generation;
    run = &first;
    CHECK(CTX(0,0,CUIS_CONTEXT_SIZE,0,0,0) == EACCDN);
    context_a = cuis_service(CUIS_SESSION_START,0);
    run = &second; context_b = cuis_service(CUIS_SESSION_START,0);
    run = &first;
    CHECK(context_a >= 0 && context_b >= 0);
    CHECK(CTX(context_b,0,CUIS_CONTEXT_SIZE,0,0,0) == EACCDN);
    CHECK(CTX(1ULL<<32,0,CUIS_CONTEXT_SIZE,0,0,0) == EINVAL);
    CHECK(CTX(context_a,CUIS_CONTEXT_SLOTS,CUIS_CONTEXT_SIZE,0,0,0) == EINVAL);
    CHECK(CTX(context_a,0,3,0,0,0) == EINVAL);
    CHECK(CTX(context_a,0,CUIS_CONTEXT_SIZE,1,0,0) == EINVAL);
    CHECK(CTX(context_a,0,CUIS_CONTEXT_SIZE,0,1,0) == EINVAL);
    CHECK(CTX(context_a,0,CUIS_CONTEXT_SIZE,0,0,1) == EINVAL);
    CHECK(CTX(context_a,0,CUIS_CONTEXT_WRITE,0x10000,32768,0) == EINVAL);
    CHECK(CTX(context_a,0,CUIS_CONTEXT_READ,0x20000,1,32767) == EINVAL);
    CHECK(CTX(context_a,0,CUIS_CONTEXT_WRITE,0,0,~0ULL) == EINVAL);
    CHECK(CTX(context_a,0,CUIS_CONTEXT_SIZE,0,0,0) == 0);
    CHECK(CTX(context_a,0,CUIS_CONTEXT_READ,0x20000,16,0) == EACCDN);
    CHECK(cuis_display_lock(context_a,1,0) == E_OK);
    CHECK(CTX(context_a,0,CUIS_CONTEXT_WRITE,0,0,0) == E_OK);
    CHECK(CTX(context_a,0,CUIS_CONTEXT_READ,0,0,0) == E_OK);
    CHECK(CTX(context_a,0,CUIS_CONTEXT_READ,0x20000,16,0) == E_OK);
    for (i = 0; i < 16; i++) CHECK(!stream_output[i]);
    for (i = 0; i < sizeof(stream_input); i++) stream_input[i] = i+1;
    CHECK(CTX(context_a,0,CUIS_CONTEXT_WRITE,0x10000,16,0) == E_OK);
    CHECK(CTX(context_a,0,CUIS_CONTEXT_SIZE,0,0,0) == 16);
    CHECK(CTX(context_a,0,CUIS_CONTEXT_WRITE,0x10000,4,4) == E_OK);
    CHECK(CTX(context_a,0,CUIS_CONTEXT_READ,0x20000,20,0) == E_OK);
    for (i = 0; i < 20; i++) CHECK(stream_output[i] == (i < 4 ? i+1 : i < 8 ? i-3 : i < 16 ? i+1 : 0));
    CHECK(CTX(context_a,0,CUIS_CONTEXT_READ,0x20000,16,32751) == E_OK);
    for (i = 0; i < 16; i++) CHECK(!stream_output[i]);
    generation = ui_contexts[0].generation;
    before = allocations-frees;
    fail_allocation = TRUE;
    CHECK(CTX(context_a,0,CUIS_CONTEXT_WRITE,0x10000,32,0) == ENSMEM);
    CHECK(CTX(context_a,0,CUIS_CONTEXT_READ,0x20000,32,0) == ENSMEM);
    fail_allocation = FALSE;
    fail_copy = TRUE;
    CHECK(CTX(context_a,0,CUIS_CONTEXT_WRITE,0x10000,32,0) == EIMBA);
    CHECK(CTX(context_a,0,CUIS_CONTEXT_READ,0x20000,32,0) == EIMBA);
    fail_copy = FALSE;
    CHECK(CTX(context_a,0,CUIS_CONTEXT_WRITE,0x30000000,32,0) == EIMBA);
    CHECK(CTX(context_a,0,CUIS_CONTEXT_READ,0x30000000,32,0) == EIMBA);
    CHECK(generation == ui_contexts[0].generation && allocations-frees == before && ui_contexts[0].bytes == 16);
    context_peer_copy = TRUE;
    CHECK(CTX(context_a,0,CUIS_CONTEXT_WRITE,0x10000,4,0) == EAGAIN);
    CHECK(!context_peer_copy && display_owner == *find_owner(&first));
    CHECK(CTX(context_a,0,CUIS_CONTEXT_READ,0x20000,4,0) == E_OK);
    CHECK(stream_output[0] == 1 && stream_output[1] == 2 && stream_output[2] == 99 && stream_output[3] == 4);
    context_read_peer = TRUE;
    CHECK(CTX(context_a,0,CUIS_CONTEXT_READ,0x20000,4,0) == E_OK && stream_output[2] == 99);
    CHECK(CTX(context_a,0,CUIS_CONTEXT_READ,0x20000,4,0) == E_OK && stream_output[2] == 77);
    generation = ui_contexts[0].generation; context_exit_copy = TRUE;
    CHECK(CTX(context_a,0,CUIS_CONTEXT_WRITE,0x10000,32,0) == EACCDN);
    CHECK(!context_exit_copy && !*find_owner(&first) && !display_owner && ui_contexts[0].generation == generation);
    run = &second;
    CHECK(CTX(context_b,0,CUIS_CONTEXT_SIZE,0,0,0) == 16);
    CHECK(cuis_display_lock(context_b,1,0) == E_OK);
    CHECK(CTX(context_b,0,CUIS_CONTEXT_READ,0x20000,4,0) == E_OK && stream_output[2] == 77);
    CHECK(CTX(context_b,1,CUIS_CONTEXT_WRITE,0x10000,32767,0) == E_OK);
    CHECK(CTX(context_b,1,CUIS_CONTEXT_SIZE,0,0,0) == 32767);
    CHECK(CTX(context_b,1,CUIS_CONTEXT_READ,0x20000,32767,0) == E_OK);
    CHECK(!memcmp(stream_input,stream_output,32767));
    CHECK(cuis_display_lock(context_b,0,0) == E_OK);
    cuis_process_exit(&second);
    /* Persistent values deliberately outlive clients; reclaim fixture state. */
    for (i = 0; i < CUIS_CONTEXT_SLOTS; i++) {
        if (ui_contexts[i].data) kfree(ui_contexts[i].data);
        memset(ui_contexts+i,0,sizeof(ui_contexts[i]));
    }
    CHECK(allocations == frees && !display_owner && !cuis_session_count());
    return 0;
}
#undef CTX
