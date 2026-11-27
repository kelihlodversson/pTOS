/* Exercise the production traps from a scheduler-owned test PROCESS. */
static long poll64(PTOS_POLLFD *fds, uint32_t count, int64_t timeout)
{
    uint64_t bits = (uint64_t)timeout;

    return trap_1_wllll(GEMDOS_FPOLL_TIME64, (long)(uintptr_t)fds, count,
                        (long)(uint32_t)(bits >> 32), (long)(uint32_t)bits);
}

static unsigned long poll_ticks(void)
{
    return (unsigned long)Ssystem(S_GETLVAL, 0x4baL, 0L);
}

static volatile struct {
    long replacement, result;
    int ran;
} poll_peer;

static void poll_rebind(void *argument)
{
    (void)argument;
    if (poll64(NULL, 0, 40) != 0)
        return;
    poll_peer.result = Fclose(4);
    if (!poll_peer.result)
        poll_peer.result = Fforce(4, (short)poll_peer.replacement);
    poll_peer.ran = 1;
}

static volatile struct {
    void *stack;
    long wait_result, group_result;
    int waiting, returned;
} poll_cancel;

static void poll_cancel_waiter(void *argument)
{
    PTOS_POLLFD fd = {0};

    (void)argument;
    fd.fd = Fcreate("FPOLLCAN.TMP", 0);
    if (fd.fd < 0)
        return;
    poll_cancel.waiting = 1;
    poll_cancel.wait_result = poll64(&fd, 1, -1);
    poll_cancel.returned = 1;
    Fclose((short)fd.fd);
}

static void poll_cancel_group(void *argument)
{
    int32_t tid = 0;

    (void)argument;
    if (launch_entry(poll_cancel_waiter, CLONE_VM | CLONE_SAME_PROCESS,
                     poll_cancel.stack, NULL, &tid) <= 0)
        Pterm(90);
    poll_cancel.group_result = poll64(NULL, 0, 50);
    if (poll_cancel.group_result || !poll_cancel.waiting || poll_cancel.returned)
        Pterm(91);
    poll_cancel.group_result = term_group(73);
}

static void test_fpoll_time64(void)
{
    static const char file[] = "FPOLL.TMP";
    static const char replacement[] = "FPOLL2.TMP";
    PTOS_POLLFD fds[4] = {{0}};
    unsigned long before;
    long first, second, stack, worker_stack, result, completion;
    int32_t tid = 0, status = -1, pid = 0;
    int failures = ptest_failures();
#if defined(TEST_WITH_PRIVATE_CLONE_TEST) && defined(TEST_WITH_MMAP)
    MAPPING_LIFETIME_SNAPSHOT files_before, files_after;
    int baseline;
#endif

    ptest_begin("Fpoll_time64 native ABI and descriptor wait");
    ptest_assert_msg(poll64(fds, 1, -2) == -25L, "reject negative timeout other than -1");
    ptest_assert_msg(poll64(fds, PTOS_POLL_MAX + 1, 0) == -25L, "enforce array limit");
    ptest_assert_msg(poll64((PTOS_POLLFD *)0x80001000UL, 1, 0) == -40L, "validate the entire user output");
    ptest_assert_msg(poll64((PTOS_POLLFD *)0x80001000UL, 0, 0) == 0, "zero descriptors use no pointer");
    before = poll_ticks();
    ptest_assert_msg(poll64(NULL, 0, 17) == 0 && poll_ticks() - before >= 4,
                     "timeout-only wait blocks for its original sub-tick deadline");

    first = Fcreate(file, 0);
    second = Fcreate(replacement, 0);
    ptest_assert_msg(first >= 0 && second >= 0, "create two regular descriptions");
    if (first < 0 || second < 0)
        goto done;
    fds[0] = (PTOS_POLLFD){first, PTOS_POLLIN | PTOS_POLLOUT | PTOS_POLLPRI, 0xffff};
    fds[1] = (PTOS_POLLFD){first, PTOS_POLLRDNORM | PTOS_POLLWRNORM, 0xffff};
    fds[2] = (PTOS_POLLFD){-1, PTOS_POLLIN, 0xffff};
    fds[3] = (PTOS_POLLFD){INT32_MAX, 0, 0xffff};
#if defined(TEST_WITH_PRIVATE_CLONE_TEST) && defined(TEST_WITH_MMAP)
    ptest_assert_msg(pclone_trap(fds, MLT_FAIL_COPY) == 0, "inject output mapping lease failure");
    ptest_assert_msg(poll64(fds, 4, 0) == -39L && fds[0].revents == 0xffff &&
                     fds[1].revents == 0xffff && fds[2].revents == 0xffff &&
                     fds[3].revents == 0xffff,
                     "failed copy preparation leaves all output unchanged");
#endif
    ptest_assert_msg(poll64(fds, 4, INT64_MAX) == 3, "wide deadline and per-entry readiness count");
    ptest_assert_msg(fds[0].revents == (PTOS_POLLIN | PTOS_POLLOUT) &&
                     fds[1].revents == (PTOS_POLLRDNORM | PTOS_POLLWRNORM) &&
                     fds[2].revents == 0 && fds[3].revents == PTOS_POLLNVAL,
                     "regular EOF, duplicate, negative and invalid descriptors");

#if defined(__mc68000__)
    if (first < 32) {
        uint32_t read_mask = 1UL << first, write_mask = read_mask, except_mask = read_mask;

        ptest_assert_msg(trap_1_wlll(GEMDOS_FPOLL, (long)fds, 4, 0) == 3,
                         "raw legacy Fpoll stack ABI");
        ptest_assert_msg(trap_1_wwlll(GEMDOS_FSELECT, 0, (long)&read_mask,
                                     (long)&write_mask, (long)&except_mask) == 2 &&
                         read_mask == (1UL << first) && write_mask == read_mask && !except_mask,
                         "raw legacy Fselect WORD/LONG ABI, zero timeout and bit counts");
    }
#else
    ptest_assert_msg(trap_1_wlll(GEMDOS_FPOLL, 0, 0, 0) == -32L &&
                     trap_1_wwlll(GEMDOS_FSELECT, 1, 0, 0, 0) == -32L,
                     "native kernel exposes only the canonical trap");
#endif

    stack = Malloc(RUNNABLE_STACK_SIZE);
    ptest_assert_msg(stack > 0, "allocate a sibling stack");
    if (stack <= 0)
        goto done;
    if (Fforce(4, (short)first) == 0) {
        poll_peer.replacement = second;
        poll_peer.ran = 0;
        result = launch_entry(poll_rebind, CLONE_VM | CLONE_SAME_PROCESS,
                              (void *)stack, NULL, &tid);
        ptest_assert_msg(result > 0, "start a sibling THREAD while the description is still bound");
        if (result > 0) {
            fds[0] = (PTOS_POLLFD){4, 0, 0xffff};
            result = poll64(fds, 1, INT64_C(0x100000000));
            ptest_assert_msg(result == 1 && poll_peer.ran && !poll_peer.result &&
                             fds[0].revents == PTOS_POLLNVAL,
                             "wide finite wait progresses peers and rejects the reused binding");
            ptest_assert_msg(join_tid(tid, &status) == 0 && !status && join_tid(tid, 0) == -20L,
                             "reap the sibling exactly once");
            fds[0].events = PTOS_POLLIN;
            ptest_assert_msg(poll64(fds, 1, 0) == 1 && fds[0].revents == PTOS_POLLIN,
                             "a fresh poll sees the replacement");
        }
        ptest_assert_msg(Fclose(4) == 0, "release the standard alias");
    } else {
        ptest_fail("bind the initial description to a known numeric descriptor");
    }

    worker_stack = Malloc(RUNNABLE_STACK_SIZE);
    ptest_assert_msg(worker_stack > 0, "allocate the cancellation waiter stack");
    if (worker_stack > 0) {
        poll_cancel.stack = (void *)worker_stack;
        poll_cancel.waiting = poll_cancel.returned = 0;
#if defined(TEST_WITH_PRIVATE_CLONE_TEST) && defined(TEST_WITH_MMAP)
        baseline = mlt_snapshot(&files_before) == 0;
        ptest_assert_msg(baseline, "capture exact SFT and OFD counts before cancellation");
#endif
        result = launch_entry(poll_cancel_group, CLONE_VM | CLONE_COPY_CWD, (void *)stack, &pid, NULL);
        ptest_assert_msg(result > 0 && pid > 0, "launch a separate PROCESS with a blocked sibling");
        if (result > 0 && pid > 0) {
            completion = Pwaitpid(pid, 0, 0);
            if ((completion >> 16) != pid || (completion & 0xffff) != 73 ||
                !poll_cancel.waiting || poll_cancel.returned)
                printf("poll cancel: pid=%ld status=%lx waiting=%d returned=%d poll=%ld group=%ld\r\n",
                       (long)pid, completion, poll_cancel.waiting, poll_cancel.returned,
                       poll_cancel.wait_result, poll_cancel.group_result);
            ptest_assert_msg((completion >> 16) == pid && (completion & 0xffff) == 73 &&
                             poll_cancel.waiting && !poll_cancel.returned,
                             "group cancellation detaches an infinite poll before USER return");
#if defined(TEST_WITH_PRIVATE_CLONE_TEST) && defined(TEST_WITH_MMAP)
            ptest_assert_msg(baseline && mlt_snapshot(&files_after) == 0 &&
                             files_after.value[MLT_SFT_SLOTS] == files_before.value[MLT_SFT_SLOTS] &&
                             files_after.value[MLT_SFT_REFS] == files_before.value[MLT_SFT_REFS] &&
                             files_after.value[MLT_OFD_REFS] == files_before.value[MLT_OFD_REFS],
                             "group exit restores exact SFT and OFD counts");
#endif
            ptest_assert_msg(Fdelete("FPOLLCAN.TMP") == 0, "remove the reaped group's fixture");
        }
        ptest_assert_msg(Mfree((void *)worker_stack) == 0, "cancelled poll releases its worker stack");
    }
    ptest_assert_msg(Mfree((void *)stack) == 0, "no live sibling stack hold");

done:
    if (first >= 0)
        ptest_assert_msg(Fclose((short)first) == 0 && Fdelete(file) == 0, "release the original file");
    if (second >= 0)
        ptest_assert_msg(Fclose((short)second) == 0 && Fdelete(replacement) == 0, "release the replacement file");
    ptest_pass();
    if (ptest_failures() == failures)
        conws("native Fpoll_time64 descriptor generations and scheduler deadlines: PASS\r\n");
}
