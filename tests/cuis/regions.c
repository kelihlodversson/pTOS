/* Rectangle subtraction checked against an independent pixel-grid oracle. */
static int subtraction_checks(void)
{
    CUIS_RECT buffers[2][CUIS_CLIP_LIMIT], covers[8], *input, *output, *swap;
    CUIS_RECT center = { -2, -2, 4, 4 };
    ULONG seed = 37, trial, i, j, count;
    LONG result, x, y;
    int visible, matches;

    buffers[0][0].x = buffers[0][0].y = -8;
    buffers[0][0].width = buffers[0][0].height = 16;
    CHECK(cuis_region_subtract(buffers[0], 1, &center, buffers[1], 3) == ENSMEM);
    CHECK(cuis_region_subtract(buffers[0], 1, &center, buffers[1], 4) == 4);
    for (trial = 0; trial < 64; trial++) {
        input = buffers[0]; output = buffers[1]; count = 1;
        input[0].x = input[0].y = -8;
        input[0].width = input[0].height = 16;
        for (i = 0; i < 8; i++) {
            seed = seed * 1664525UL + 1013904223UL;
            covers[i].x = (LONG)(seed % 24) - 12;
            seed = seed * 1664525UL + 1013904223UL;
            covers[i].y = (LONG)(seed % 24) - 12;
            seed = seed * 1664525UL + 1013904223UL;
            covers[i].width = 1 + seed % 12;
            seed = seed * 1664525UL + 1013904223UL;
            covers[i].height = 1 + seed % 12;
            result = cuis_region_subtract(input, count, &covers[i], output, CUIS_CLIP_LIMIT);
            CHECK(result >= 0);
            count = (ULONG)result; swap = input; input = output; output = swap;
            for (y = -8; y < 8; y++) for (x = -8; x < 8; x++) {
                visible = 1; matches = 0;
                for (j = 0; j <= i; j++)
                    if (x >= covers[j].x && x < covers[j].x + covers[j].width &&
                        y >= covers[j].y && y < covers[j].y + covers[j].height) visible = 0;
                for (j = 0; j < count; j++) {
                    CHECK(input[j].width > 0 && input[j].height > 0);
                    CHECK(input[j].x >= -8 && input[j].y >= -8 &&
                          input[j].x + input[j].width <= 8 && input[j].y + input[j].height <= 8);
                    if (x >= input[j].x && x < input[j].x + input[j].width &&
                        y >= input[j].y && y < input[j].y + input[j].height) matches++;
                }
                CHECK(matches == visible);
            }
        }
    }
    buffers[0][0].x = (LONG)0x80000000UL; buffers[0][0].y = -1;
    buffers[0][0].width = 0x7fffffffL; buffers[0][0].height = 0x7fffffffL;
    center.x = -1; center.y = -1; center.width = 1; center.height = 1;
    CHECK(cuis_region_subtract(buffers[0], 1, &center, buffers[1], 1) == 1);
    CHECK(buffers[1][0].x == (LONG)0x80000000UL && buffers[1][0].width == 0x7fffffffL);
    return 0;
}
#define W(fn, h) cuis_window_service(fn, h, ~0ULL, ~0ULL, ~0ULL, ~0ULL)
#define CLIP(h, i) cuis_clip_service(h, i, 0x1000, 0x1004, 0x1008, 0x100c)
static int region_checks(void)
{
    LONG a, b, session, result;
    ULONG before, i;
    int rc = subtraction_checks();
    if (rc) return rc;
    next_handle = next_window_handle = 0;
    run = &first;
    session = cuis_service(CUIS_SESSION_START, 0);
    a = cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 10, 10, 0);
    CHECK(session >= 0 && a >= 0);
    CHECK(CLIP(a, 0) == EACCDN);
    CHECK(cuis_vdi_policy() == EACCDN);
    CHECK(W(CUIS_UPDATE_BEGIN, a) == EINVAL);
    CHECK(W(CUIS_WINDOW_SHOW, a) == E_OK);
    fail_allocation = TRUE;
    CHECK(W(CUIS_UPDATE_BEGIN, a) == ENSMEM && !cuis_update_active());
    fail_allocation = FALSE;
    before = allocations - frees;
    fail_at_allocation = allocations + 1;
    CHECK(W(CUIS_UPDATE_BEGIN, a) == ENSMEM && !cuis_update_active());
    fail_at_allocation = 0;
    CHECK(allocations - frees == before);
    run = &second;
    CHECK(cuis_service(CUIS_SESSION_START, 0) >= 0);
    b = cuis_window_service(CUIS_WINDOW_CREATE, 2, 2, 4, 4, 0);
    CHECK(b >= 0 && W(CUIS_WINDOW_SHOW, b) == E_OK);
    run = &first;
    CHECK(W(CUIS_UPDATE_BEGIN, a) == E_OK && cuis_update_active());
    CHECK(update_count == 4 && cuis_vdi_policy() == 1);
    CHECK(CLIP(a, 0) == 1 && copied[0] == 0 && copied[1] == 0 && copied[2] == 10 && copied[3] == 2);
    CHECK(cuis_clip_service(a, 0, 0x1000, 0x1004, 0x1008, 0) == EIMBA);
    copied[0] = 999;
    CHECK(cuis_clip_service(a, 0, 0x1000, 0x1004, 0x1008, 0x10000100cULL) == EIMBA);
    CHECK(copied[0] == 999);
    fail_copy = TRUE;
    CHECK(CLIP(a, 0) == EIMBA);
    fail_copy = FALSE;
    CHECK(CLIP(a, 0) == 1 && copied[0] == 0);
    CHECK(cuis_clip_service(a, 0x100000000ULL, 0, 0, 0, 0) == EINVAL);
    CHECK(CLIP(0x100000000ULL, 0) == EINVAL);
    CHECK(CLIP(a, 4) == 0);
    CHECK(cuis_clip_service(a, 4, 0, 0, 0, 0) == 0);
    CHECK(W(CUIS_UPDATE_BEGIN, a) == EAGAIN);
    CHECK(W(CUIS_WINDOW_HIDE, a) == EAGAIN);
    CHECK(cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 1, 1, 0) == EAGAIN);
    run = &second;
    CHECK(CLIP(a, 0) == EACCDN);
    CHECK(cuis_vdi_policy() == EAGAIN);
    CHECK(W(CUIS_UPDATE_END, a) == EACCDN);
    CHECK(W(CUIS_UPDATE_BEGIN, b) == EAGAIN);
    CHECK(W(CUIS_WINDOW_DESTROY, b) == EAGAIN);
    CHECK(cuis_window_service(CUIS_WINDOW_CONFIGURE, b, 0, 0, 1, 1) == EAGAIN);
    /* Exiting an occluder may expose more, but the held snapshot stays safe. */
    cuis_process_exit(&second);
    CHECK(cuis_vdi_policy() == EAGAIN);
    run = &first;
    CHECK(update_count == 4 && CLIP(a, 0) == 1 && copied[3] == 2);
    CHECK(update_window->redraw);
    CHECK(W(CUIS_UPDATE_END, a) == E_OK && !cuis_update_active());
    CHECK(W(CUIS_WINDOW_REDRAW, a) == 1);
    CHECK(W(CUIS_UPDATE_END, a) == EACCDN);
    CHECK(W(CUIS_UPDATE_BEGIN, a) == E_OK && update_count == 1);
    CHECK(CLIP(a, 0) == 1 && copied[2] == 10 && copied[3] == 10);
    CHECK(cuis_service(CUIS_SESSION_END, session) == E_OK && !cuis_update_active());
    CHECK(!window_front && allocations == frees);

    CHECK(cuis_service(CUIS_SESSION_START, 0) >= 0);
    a = cuis_window_service(CUIS_WINDOW_CREATE, -5LL, -5LL, 10, 10, 0);
    CHECK(a >= 0 && W(CUIS_WINDOW_SHOW, a) == E_OK);
    b = cuis_window_service(CUIS_WINDOW_CREATE, -5LL, -5LL, 10, 10, 0);
    CHECK(b >= 0 && W(CUIS_WINDOW_SHOW, b) == E_OK);
    CHECK(W(CUIS_UPDATE_BEGIN, a) == E_OK && update_count == 0);
    CHECK(cuis_clip_service(a, 0, 0, 0, 0, 0) == 0);
    CHECK(W(CUIS_UPDATE_END, a) == E_OK);
    CHECK(W(CUIS_WINDOW_HIDE, b) == E_OK);
    CHECK(W(CUIS_UPDATE_BEGIN, a) == E_OK && update_count == 1);
    CHECK(cuis_clip_service(a, 0, 0x1000, 0x1000, 0x1000, 0x1000) == 1);
    CHECK(copied[0] == 5);
    cuis_process_exit(&first);
    CHECK(!cuis_update_active() && allocations == frees);

    /* A grid of thin occluders fragments the target beyond the hard bound. */
    CHECK(cuis_service(CUIS_SESSION_START, 0) >= 0);
    a = cuis_window_service(CUIS_WINDOW_CREATE, 0, 0, 33, 33, 0);
    CHECK(a >= 0 && W(CUIS_WINDOW_SHOW, a) == E_OK);
    for (i = 0; i < 16; i++) {
        b = cuis_window_service(CUIS_WINDOW_CREATE, 1 + 2 * i, 0, 1, 33, 0);
        CHECK(b >= 0 && W(CUIS_WINDOW_SHOW, b) == E_OK);
        b = cuis_window_service(CUIS_WINDOW_CREATE, 0, 1 + 2 * i, 33, 1, 0);
        CHECK(b >= 0 && W(CUIS_WINDOW_SHOW, b) == E_OK);
    }
    before = allocations - frees;
    result = W(CUIS_UPDATE_BEGIN, a);
    CHECK(result == ENSMEM && !cuis_update_active());
    CHECK(allocations - frees == before);
    CHECK(W(CUIS_UPDATE_BEGIN, b) == E_OK);
    cuis_process_exit(&first);
    CHECK(!cuis_update_active() && !window_front && allocations == frees);
    CHECK(cuis_vdi_policy() == 0);
    CHECK(cuis_service(CUIS_SESSION_START, 0) >= 0);
    a = cuis_window_service(CUIS_WINDOW_CREATE, -10LL, -20LL, 30, 40, 0);
    CHECK(a >= 0 && W(CUIS_WINDOW_SHOW, a) == E_OK);
    CHECK(W(CUIS_UPDATE_BEGIN, a) == E_OK);
    CHECK(CLIP(a, 0) == 1 && copied[0] == 0 && copied[1] == 0 && copied[2] == 20 && copied[3] == 20);
    CHECK(W(CUIS_UPDATE_END, a) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_CONFIGURE, a, 1020, 760, 30, 40) == E_OK);
    CHECK(W(CUIS_UPDATE_BEGIN, a) == E_OK);
    CHECK(CLIP(a, 0) == 1 && copied[0] == 1020 && copied[1] == 760 && copied[2] == 4 && copied[3] == 8);
    CHECK(W(CUIS_UPDATE_END, a) == E_OK);
    CHECK(cuis_window_service(CUIS_WINDOW_CONFIGURE, a, 1024, 0, 1, 1) == E_OK);
    CHECK(W(CUIS_UPDATE_BEGIN, a) == E_OK);
    CHECK(CLIP(a, 0) == 0);
    CHECK(W(CUIS_UPDATE_END, a) == E_OK);
    display_width = 0;
    CHECK(W(CUIS_UPDATE_BEGIN, a) == EINVFN && !cuis_update_active());
    display_width = 1024;
    cuis_process_exit(&first);
    CHECK(allocations == frees);
    return 0;
}
#undef CLIP
#undef W
