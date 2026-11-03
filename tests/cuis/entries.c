/* Native opaque directory entries outlive unrelated clients, never owners. */
static int entry_checks(void)
{
    LONG owner, peer, replacement;
    ULONG i;
    run = &first; owner = cuis_service(CUIS_SESSION_START,0);
    run = &second; peer = cuis_service(CUIS_SESSION_START,0);
    memcpy(stream_input,"Accessory",10);
    CHECK(cuis_entry_service(CUIS_ENTRY_PUBLISH,owner,0,0,0x10000,10) == EACCDN);
    CHECK(cuis_entry_service(CUIS_ENTRY_OWNER,peer,CUIS_ENTRY_LIMIT,0,0,0) == EINVAL);
    CHECK(cuis_entry_service(CUIS_ENTRY_PUBLISH,peer,0,0,0x10000,0) == EINVAL);
    CHECK(cuis_entry_service(CUIS_ENTRY_PUBLISH,peer,0,0,0x10000,CUIS_ENTRY_BYTES+1) == EINVAL);
    fail_copy = TRUE;
    CHECK(cuis_entry_service(CUIS_ENTRY_PUBLISH,peer,0,0,0x10000,10) == EIMBA);
    fail_copy = FALSE;
    CHECK(cuis_entry_service(CUIS_ENTRY_OWNER,peer,0,0,0,0) == EFILNF);
    run = &first;
    for (i = 0; i < CUIS_ENTRY_LIMIT; i++)
        CHECK(cuis_entry_service(CUIS_ENTRY_PUBLISH,owner,i,0,0x10000,10) == (LONG)i);
    run = &second;
    CHECK(cuis_entry_service(CUIS_ENTRY_PUBLISH,peer,0,0,0x10000,10) == EAGAIN);
    CHECK(cuis_entry_service(CUIS_ENTRY_OWNER,peer,0,0,0,0) == owner);
    CHECK(cuis_entry_service(CUIS_ENTRY_READ,peer,0,owner,0x20000,9) == ERANGE);
    CHECK(cuis_entry_service(CUIS_ENTRY_READ,peer,0,peer,0x20000,10) == EAGAIN);
    CHECK(cuis_entry_service(CUIS_ENTRY_READ,peer,0,owner,0x20000,10) == 10);
    CHECK(!memcmp(stream_output,"Accessory",10));
    /* Teardown of the querying application must not remove another owner. */
    cuis_process_exit(&second);
    peer = cuis_service(CUIS_SESSION_START,0);
    CHECK(cuis_entry_service(CUIS_ENTRY_OWNER,peer,0,0,0,0) == owner);
    cuis_process_exit(&first);
    for (i = 0; i < CUIS_ENTRY_LIMIT; i++)
        CHECK(cuis_entry_service(CUIS_ENTRY_OWNER,peer,i,0,0,0) == EFILNF);
    run = &first; replacement = cuis_service(CUIS_SESSION_START,0);
    CHECK(replacement != owner);
    CHECK(cuis_entry_service(CUIS_ENTRY_PUBLISH,replacement,0,0,0x10000,10) == 0);
    run = &second;
    CHECK(cuis_entry_service(CUIS_ENTRY_READ,peer,0,owner,0x20000,10) == EAGAIN);
    CHECK(cuis_entry_service(CUIS_ENTRY_READ,peer,0,replacement,0x20000,10) == 10);
    /* Explicit appl_exit removes registrations as well as process exit. */
    run = &first; CHECK(cuis_service(CUIS_SESSION_END,replacement) == E_OK);
    run = &second;
    CHECK(cuis_entry_service(CUIS_ENTRY_OWNER,peer,0,0,0,0) == EFILNF);
    cuis_process_exit(&second);
    CHECK(!cuis_session_count() && allocations == frees && input_irqs);
    return 0;
}
