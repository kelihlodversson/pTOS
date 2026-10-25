/* Window-independent timers ignore queued events and preserve IRQ state. */
static int delay_checks(void)
{
    LONG first_session, other_session, window;
    ULONG before;
    run = &first;
    first_session = cuis_service(CUIS_SESSION_START, 0);
    CHECK(first_session >= 0);
    CHECK(cuis_delay_service(1ULL << 32, 0) == EINVAL);
    CHECK(cuis_delay_service(first_session, 1ULL << 32) == EINVAL);
    CHECK(cuis_delay_service(first_session, ~0ULL) == EINVAL);
    run = &second;
    other_session = cuis_service(CUIS_SESSION_START, 0);
    CHECK(other_session >= 0 && cuis_delay_service(first_session, 1) == EACCDN);
    CHECK(cuis_message_service(CUIS_MESSAGE_SEND, first_session, 11, 22, 33, 44, 0) == E_OK);
    run = &first;
    before = delay_yields;
    CHECK(cuis_delay_service(first_session, 0) == E_OK && delay_yields == before + 1);
    input_ticks = 0xfffffffeUL; input_sleeps = 0;
    CHECK(cuis_delay_service(first_session, 6) == E_OK && input_sleeps == 3 && input_irqs);
    CHECK((*find_owner(run))->message_count == 1); /* A ready message cannot shorten the timer. */
    input_irqs = FALSE; input_sleeps = 0;
    CHECK(cuis_delay_service(first_session, 1) == E_OK && input_sleeps == 2 && !input_irqs);
    input_irqs = TRUE;
    delay_tick_step = 858993460UL; input_sleeps = 0;
    CHECK(cuis_delay_service(first_session, 0xffffffffULL) == E_OK && input_sleeps == 1);
    delay_tick_step = 1;
    window = cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 10, 10, 0);
    CHECK(window >= 0 && cuis_window_service(CUIS_WINDOW_SHOW, window, 0, 0, 0, 0) == E_OK);
    CHECK(cuis_window_service(CUIS_UPDATE_BEGIN, window, 0, 0, 0, 0) == E_OK);
    CHECK(cuis_delay_service(first_session, 1) == EACCDN);
    CHECK(cuis_delay_service(first_session, 0) == E_OK);
    cuis_process_exit(&first); cuis_process_exit(&second);
    CHECK(cuis_delay_service(first_session, 0) == EACCDN);
    CHECK(allocations == frees && !input_bad_idle);
    return 0;
}
