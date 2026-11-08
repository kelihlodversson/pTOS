/* Distinct client, endpoint and PROCESS identities through production CUIS.
 * The host owner model permits several caller contexts in one PROCESS. */
static int client_identity_checks(void)
{
    LONG a, b, c, d, replacement;
    CUIS_PEER_IDENTITY_DATA first_identity, second_identity, third_identity;
    ULONG references = process_references;
    next_aes_id = 32767;
    run = &first; a = cuis_service(CUIS_SESSION_START,0);
    run = &second; b = cuis_service(CUIS_SESSION_START,0);
    run = &third; c = cuis_service(CUIS_SESSION_START,0);
    CHECK(a >= 0 && b >= 0 && c >= 0 && process_references == references + 3);
    CHECK(cuis_peer_service(CUIS_PEER_QUERY,c,a,CUIS_PEER_IDENTITY,0x20000,16) == 16);
    memcpy(&first_identity,stream_output,16);
    CHECK(first_identity.client_id == 32767 && first_identity.process_high == 1 &&
          first_identity.process_low == 1 && !first_identity.reserved);
    CHECK(cuis_peer_service(CUIS_PEER_QUERY,c,b,CUIS_PEER_IDENTITY,0x20000,16) == 16);
    memcpy(&second_identity,stream_output,16);
    CHECK(cuis_peer_service(CUIS_PEER_QUERY,c,c,CUIS_PEER_IDENTITY,0x20000,16) == 16);
    memcpy(&third_identity,stream_output,16);
    CHECK(second_identity.client_id == 1 && third_identity.client_id == 2 &&
          second_identity.process_low == third_identity.process_low &&
          second_identity.process_high == third_identity.process_high &&
          second_identity.process_high == 2);
    CHECK(cuis_peer_service(CUIS_PEER_QUERY,c,b,CUIS_PEER_IDENTITY,0x20000,15) == EINVAL);
    CHECK(cuis_peer_service(CUIS_PEER_QUERY,c,b,CUIS_PEER_IDENTITY,0,16) == EIMBA);
    fail_copy = TRUE;
    CHECK(cuis_peer_service(CUIS_PEER_QUERY,c,b,CUIS_PEER_IDENTITY,0x20000,16) == EIMBA);
    fail_copy = FALSE;
    CHECK(process_references == references + 3);
    run = &fourth; fail_allocation = TRUE;
    CHECK(cuis_service(CUIS_SESSION_START,0) == ENSMEM && process_references == references + 3);
    fail_allocation = FALSE; next_aes_id = 32767;
    d = cuis_service(CUIS_SESSION_START,0);
    CHECK(d >= 0 && find_handle(d)->aes_id == 3); /* Skip all three live IDs at wrap. */
    run = &second; CHECK(cuis_service(CUIS_SESSION_END,b) == E_OK);
    next_aes_id = 1;
    run = &fifth; replacement = cuis_service(CUIS_SESSION_START,0);
    CHECK(replacement > b && find_handle(replacement)->aes_id == second_identity.client_id);
    CHECK(cuis_peer_service(CUIS_PEER_QUERY,replacement,b,CUIS_PEER_IDENTITY,0x20000,16) == EFILNF);
    CHECK(cuis_peer_service(CUIS_PEER_QUERY,replacement,replacement,CUIS_PEER_IDENTITY,0x20000,16) == 16);
    cuis_process_exit(&first); cuis_process_exit(&third);
    cuis_process_exit(&fourth); cuis_process_exit(&fifth);
    CHECK(!cuis_session_count() && process_references == references && allocations == frees && input_irqs);
    return 0;
}
