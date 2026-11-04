/* The host idle mock delivers an IRQ at the check/sleep boundary. */
#define I(fn, h, arg) cuis_input_service(fn, h, arg)
#define FOCUS(fn, h) cuis_window_service(fn, h, ~0ULL, ~0ULL, ~0ULL, ~0ULL)
static int input_checks(void)
{
    LONG a, b;
    ULONG i;
    run = &first;
    CHECK(!cuis_keyboard_event(123));
    CHECK(I(CUIS_KEY_READ, 0, 0) == EACCDN);
    CHECK(I(99, 0, 0) == EINVFN);
    CHECK(cuis_service(CUIS_SESSION_START, 0) >= 0);
    a = cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 10, 10, 0);
    CHECK(a >= 0 && FOCUS(CUIS_WINDOW_SHOW, a) == E_OK);
    CHECK(I(CUIS_KEY_WAIT, a, 0) == EACCDN);
    CHECK(FOCUS(CUIS_FOCUS_REQUEST, a) == E_OK);
    CHECK(I(CUIS_KEY_READ, a, ~0ULL) == 0); /* Empty read ignores output. */
    CHECK(I(CUIS_KEY_WAIT, a, 0) == 0 && !input_sleeps);
    CHECK(I(CUIS_KEY_WAIT, a, -2LL) == EINVAL);
    CHECK(I(CUIS_KEY_WAIT, a, 0xffffffffULL) == EINVAL);
    CHECK(I(CUIS_KEY_WAIT, 0x100000000ULL | (ULONG)a, 0) == EINVAL);
    CHECK(cuis_keyboard_event(0));
    CHECK(I(CUIS_KEY_WAIT, a, 0) == 1);
    CHECK(I(CUIS_KEY_READ, a, 0x1000) == 1 && copied[0] == 0);
    input_irqs = FALSE;
    CHECK(cuis_keyboard_event(0xffffffffUL) && !input_irqs);
    input_irqs = TRUE;
    CHECK(I(CUIS_KEY_READ, a, 0x1000) == 1 && (ULONG)copied[0] == 0xffffffffUL);
    for (i = 0; i < CUIS_KEY_CAPACITY + 3; i++) CHECK(cuis_keyboard_event(i + 1));
    CHECK(I(CUIS_KEY_READ, a, 0) == EIMBA);
    CHECK(I(CUIS_KEY_READ, a, 0x100001000ULL) == EIMBA);
    fail_copy = TRUE;
    CHECK(I(CUIS_KEY_READ, a, 0x1000) == EIMBA);
    fail_copy = FALSE;
    for (i = 0; i < CUIS_KEY_CAPACITY; i++)
        CHECK(I(CUIS_KEY_READ, a, 0x1000) == 1 && (ULONG)copied[0] == i + 1);
    CHECK(I(CUIS_KEY_READ, a, 0x1000) == 0);
    CHECK(cuis_keyboard_event(42));
    input_drop_on_validate = TRUE;
    CHECK(I(CUIS_KEY_READ, a, 0x1000) == EACCDN);
    CHECK(FOCUS(CUIS_FOCUS_REQUEST, a) == E_OK);
    CHECK(I(CUIS_KEY_READ, a, 0x1000) == 0);
    input_sleeps = 0;
    CHECK(I(CUIS_KEY_WAIT, a, 6) == 0 && input_sleeps == 3);
    input_ticks = 0xfffffffeUL; input_sleeps = 0;
    CHECK(I(CUIS_KEY_WAIT, a, 10) == 0 && input_ticks == 1);
    input_sleeps = 0; input_inject_after = 1;
    CHECK(I(CUIS_KEY_WAIT, a, 100) == 1 && input_sleeps == 1);
    CHECK(I(CUIS_KEY_READ, a, 0x1000) == 1 && (ULONG)copied[0] == 0xff123456UL);
    input_sleeps = 0; input_inject_after = 3;
    CHECK(I(CUIS_KEY_WAIT, a, ~0ULL) == 1 && input_sleeps == 3);
    CHECK(I(CUIS_KEY_READ, a, 0x1000) == 1);
    input_inject_after = 0;
    CHECK(FOCUS(CUIS_UPDATE_BEGIN, a) == E_OK);
    CHECK(I(CUIS_KEY_WAIT, a, 0) == 0);
    CHECK(I(CUIS_KEY_WAIT, a, 10) == EACCDN);
    CHECK(cuis_keyboard_event(77));
    CHECK(I(CUIS_KEY_WAIT, a, 10) == 1); /* Pending input does not block. */
    CHECK(I(CUIS_KEY_READ, a, 0x1000) == 1 && copied[0] == 77);
    CHECK(FOCUS(CUIS_UPDATE_END, a) == E_OK);
    input_drop_focus = TRUE;
    CHECK(I(CUIS_KEY_WAIT, a, 10) == EACCDN);
    CHECK(!cuis_keyboard_event(88));
    CHECK(FOCUS(CUIS_FOCUS_REQUEST, a) == E_OK);
    CHECK(cuis_keyboard_event(123));
    run = &second;
    CHECK(cuis_service(CUIS_SESSION_START, 0) >= 0);
    CHECK(I(CUIS_KEY_READ, a, 0x1000) == EACCDN);
    CHECK(I(CUIS_KEY_WAIT, a, 0) == EACCDN);
    b = cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 10, 10, 0);
    CHECK(b > a && FOCUS(CUIS_WINDOW_SHOW, b) == E_OK);
    CHECK(FOCUS(CUIS_FOCUS_REQUEST, b) == E_OK);
    CHECK(I(CUIS_KEY_READ, b, 0x1000) == 0);
    run = &first;
    CHECK(cuis_keyboard_event(456)); /* Route by focus, not interrupted process. */
    CHECK(I(CUIS_KEY_READ, a, 0x1000) == EACCDN);
    run = &second;
    CHECK(I(CUIS_KEY_READ, b, 0x1000) == 1 && copied[0] == 456);
    CHECK(cuis_keyboard_event(654));
    cuis_process_exit(&second);
    run = &first;
    CHECK(I(CUIS_KEY_READ, a, 0x1000) == 0); /* Focus loss flushed old input. */
    CHECK(cuis_keyboard_event(789));
    CHECK(FOCUS(CUIS_FOCUS_REQUEST, a) == E_OK); /* Repeated focus keeps input. */
    CHECK(I(CUIS_KEY_READ, a, 0x1000) == 1 && copied[0] == 789);
    cuis_process_exit(&first);
    CHECK(!cuis_keyboard_event(123) && !cuis_focus_active());
    CHECK(input_irqs && !input_bad_idle && allocations == frees);
    input_irqs = FALSE;
    CHECK(!cuis_keyboard_event(1) && !input_irqs);
    input_irqs = TRUE;
    return 0;
}
#undef I
#undef FOCUS
