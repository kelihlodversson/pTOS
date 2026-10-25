/* Included by the host service harness: inspect metadata without exporting it. */
static int window_checks(void)
{
    CUIS_CLIENT *client;
    LONG first_window, second_window, child_window;
    ULONG before, i;

    next_handle = 0;
    CHECK(cuis_window_service(99, 0, 0, 0, 0, 0) == EINVFN);
    CHECK(cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 1, 1, 0) == EACCDN);
    CHECK(cuis_service(CUIS_SESSION_START, 0) >= 0);
    client = *find_owner(run);
    before = allocations;
    CHECK(cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 0, 1, 0) == EINVAL);
    CHECK(cuis_window_service(CUIS_WINDOW_CREATE, 0x100000000ULL, 0, 1, 1, 0) == EINVAL);
    CHECK(cuis_window_service(CUIS_WINDOW_CREATE, 0xffffffffULL, 0, 1, 1, 0) == EINVAL);
    CHECK(cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, ~0ULL, 1, 0) == EINVAL);
    CHECK(cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 1, 0x100000001ULL, 0) == EINVAL);
    CHECK(cuis_window_service(CUIS_WINDOW_CREATE, 0x7fffffff, 0, 1, 1, 0) == EINVAL);
    CHECK(allocations == before);
    fail_allocation = TRUE;
    CHECK(cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 1, 1, 0) == ENSMEM);
    fail_allocation = FALSE;
    first_window = cuis_window_service(CUIS_WINDOW_CREATE, ~0ULL, -2LL, 30, 40, ~0ULL);
    CHECK(first_window == 0 && cuis_window_count() == 1);
    CHECK(client->windows->x == -1 && client->windows->y == -2 &&
          client->windows->width == 30 && client->windows->height == 40);
    CHECK(cuis_window_service(CUIS_WINDOW_CONFIGURE, first_window, -5LL, 6, 70, 80) == E_OK);
    CHECK(client->windows->x == -5 && client->windows->y == 6 &&
          client->windows->width == 70 && client->windows->height == 80);
    CHECK(cuis_window_service(CUIS_WINDOW_CONFIGURE, first_window, 1, 2, 3, 0) == EINVAL);
    CHECK(client->windows->x == -5 && client->windows->height == 80);
    CHECK(cuis_window_service(CUIS_WINDOW_DESTROY, 0x100000000ULL, 0, 0, 0, 0) == EINVAL);
    CHECK(cuis_window_service(CUIS_WINDOW_DESTROY, ~0ULL, 0, 0, 0, 0) == EINVAL);
    run = &second;
    CHECK(cuis_service(CUIS_SESSION_START, 0) >= 0);
    CHECK(cuis_window_service(CUIS_WINDOW_DESTROY, first_window, 0, 0, 0, 0) == EACCDN);
    CHECK(cuis_window_service(CUIS_WINDOW_CONFIGURE, first_window, 0, 0, 1, 1) == EACCDN);
    child_window = cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 1, 1, 0);
    CHECK(child_window > first_window && cuis_window_count() == 2);
    run = &first;
    second_window = cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 1, 1, 0);
    CHECK(second_window > child_window);
    CHECK(cuis_window_service(CUIS_WINDOW_DESTROY, first_window, ~0ULL, ~0ULL, ~0ULL, ~0ULL) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_DESTROY, first_window, 0, 0, 0, 0) == EACCDN);
    for (i = 1; i < CUIS_WINDOWS_PER_SESSION; i++)
        CHECK(cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 1, 1, 0) >= 0);
    CHECK(cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 1, 1, 0) == ENSMEM);
    CHECK(client->window_count == CUIS_WINDOWS_PER_SESSION);
    CHECK(cuis_service(CUIS_SESSION_END, client->handle) == E_OK);
    CHECK(cuis_window_count() == 1);
    CHECK(cuis_service(CUIS_SESSION_START, 0) >= 0);
    CHECK(cuis_window_service(CUIS_WINDOW_DESTROY, second_window, 0, 0, 0, 0) == EACCDN);
    next_window_handle = 0x7fffffffUL;
    CHECK(cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 1, 1, 0) == 0x7fffffffL);
    CHECK(cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 1, 1, 0) == ENSMEM);
    cuis_process_exit(&first);
    cuis_process_exit(&first);
    run = &second;
    CHECK(cuis_window_service(CUIS_WINDOW_DESTROY, child_window, 0, 0, 0, 0) == E_OK);
    cuis_process_exit(&second);
    CHECK(!cuis_window_count() && !cuis_session_count() && allocations == frees);
    return 0;
}
