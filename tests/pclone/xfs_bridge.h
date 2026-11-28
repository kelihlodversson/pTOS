#if defined(TEST_WITH_FPOLL_TIME64)
#include "../xfs/event.h"

static const uint32_t event_producer_value = UINT32_C(0x369cd58a);
static volatile short xfs_rebind_control;

static int xfs_wait_for_block(short control)
{
    unsigned long start = poll_ticks();

    /* ARM resets the samples. One is from the zero-time check. The
     * second comes from the blocking wait with the old file pinned. */
    do {
        if (Fcntl(control, 0, XFS_TEST_SAMPLE_COUNT) >= 2)
            return 1;
        poll64(NULL, 0, 5);
    } while (poll_ticks() - start < 100);
    return 0;
}

static void xfs_event_producer(void *argument)
{
    long control = Fopen(XFS_TEST_CONTROL_PATH, 0);
    long file = Fopen(XFS_TEST_EVENT_PATH, 1);

    (void)argument;
    if (control < 0 || file < 0 || !xfs_wait_for_block((short)control))
        Pterm(80);
    if (Fwrite((short)file, sizeof(event_producer_value), &event_producer_value) !=
        sizeof(event_producer_value) || Fclose((short)file) || Fclose((short)control))
        Pterm(81);
}

static void xfs_event_rebind(void *argument)
{
    long replacement;

    (void)argument;
    if (!xfs_wait_for_block(xfs_rebind_control) || Fclose(4))
        Pterm(82);
    replacement = Fopen("R:\\FIXTURE\\NULL.TXT", 2);
    if (replacement < 0 || Fforce(4, (short)replacement) || Fclose((short)replacement))
        Pterm(83);
}

static void test_xfs_ioctl(void)
{
    PTOS_POLLFD fd = {0, PTOS_POLLIN, 0};
    uint32_t value = XFS_TEST_INPUT;
    int32_t pid = 0, tid = 0, status = -1;
    unsigned long start;
    long stack, control, closes, result;
    int failures = ptest_failures();

    ptest_begin("XFS Fcntl ioctl and retained poll ownership");
    fd.fd = Fopen(XFS_TEST_EVENT_PATH, 0);
    control = Fopen(XFS_TEST_CONTROL_PATH, 0);
    stack = Malloc(RUNNABLE_STACK_SIZE);
    ptest_assert_msg(fd.fd >= 0 && control >= 0 && stack > 0,
                     "open the device, its observer and a worker stack");
    if (fd.fd < 0 || control < 0 || stack <= 0)
        goto done;

    ptest_assert_msg(Fcntl(32767, &value, XFS_TEST_ARM) == -37L &&
                     Fcntl((short)fd.fd, 0, XFS_TEST_ARM) == -40L &&
                     Fcntl((short)fd.fd, 1, XFS_TEST_ARM) == -40L &&
                     Fcntl((short)fd.fd, 1, 0x7aff) == -32L &&
                     Fcntl((short)fd.fd, 1, F_GETFL) == 0 &&
                     Fcntl((short)fd.fd, XFS_TEST_INPUT, XFS_TEST_SCALAR) == 0,
                     "invalid inputs, unknown commands, scalar and F_GETFL");
#ifdef TEST_WITH_MMAP
    {
        long page = clone_test_sys6(GEMDOS_MMAP, 0, 4096, PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        ptest_assert_msg(page > 0, "map a readable ioctl argument");
        if (page > 0) {
            *(uint32_t *)page = XFS_TEST_INPUT;
            ptest_assert_msg(clone_test_sys6(GEMDOS_MPROT, page, 4096, PROT_READ, 0, 0, 0) == 0 &&
                             Fcntl((short)fd.fd, page, XFS_TEST_ARM) == -40L &&
                             *(const uint32_t *)page == XFS_TEST_INPUT,
                             "reject a read-only output before arming the device");
            ptest_assert_msg(clone_test_sys6(GEMDOS_MUNMAP, page, 4096, 0, 0, 0, 0) == 0,
                             "release the rejected output mapping");
        }
    }
#endif
    closes = Fcntl((short)control, 1, XFS_TEST_CLOSE_COUNT);
    ptest_assert_msg(closes >= 0 && Fcntl((short)fd.fd, &value, XFS_TEST_ARM) == 0 &&
                     value == XFS_TEST_REPLY && poll64(&fd, 1, 0) == 0 && !fd.revents,
                     "the device sees the input, returns its reply and is initially unready");
    result = launch_entry(xfs_event_producer, CLONE_VM | CLONE_COPY_CWD,
                          (void *)stack, &pid, NULL);
    ptest_assert_msg(result > 0 && pid > 0, "launch a producer with an independent descriptor table");
    if (result > 0 && pid > 0) {
        start = poll_ticks();
        result = poll64(&fd, 1, 2000);
        ptest_assert_msg(result == 1 && fd.revents == PTOS_POLLIN && poll_ticks() - start < 200,
                         "device publication wakes the blocked poll before its deadline");
        ptest_assert_msg(Pwaitpid(pid, 0, 0) == ((long)pid << 16), "the producer released its own files");
        ptest_assert_msg(poll64(&fd, 1, 0) == 1 && fd.revents == PTOS_POLLIN &&
                         Fread((short)fd.fd, sizeof(value), &value) == sizeof(value) &&
                         value == (event_producer_value ^ XFS_TEST_INPUT) &&
                         poll64(&fd, 1, 0) == 0 && !fd.revents,
                         "readiness is persistent until Fread consumes the exact payload");
    }
    ptest_assert_msg(Fclose((short)fd.fd) == 0 &&
                     Fcntl((short)control, 0, XFS_TEST_CLOSE_COUNT) == closes + 1,
                     "close the first device description once");

    fd.fd = Fopen(XFS_TEST_EVENT_PATH, 0);
    value = XFS_TEST_INPUT;
    ptest_assert_msg(fd.fd >= 0, "open a fresh description for the descriptor reuse run");
    if (fd.fd >= 0) {
        result = Fforce(4, (short)fd.fd);
        ptest_assert_msg(result == 0, "bind the fresh description to a known descriptor");
        if (result)
            goto done;
        ptest_assert_msg(Fclose((short)fd.fd) == 0, "leave only the binding the sibling will replace");
        fd.fd = 4;
        ptest_assert_msg(Fcntl(4, &value, XFS_TEST_ARM) == 0 && poll64(&fd, 1, 0) == 0 &&
                         !fd.revents, "arm the fresh description and sample it unready");
        xfs_rebind_control = (short)control;
        result = launch_entry(xfs_event_rebind, CLONE_VM | CLONE_SAME_PROCESS,
                              (void *)stack, NULL, &tid);
        ptest_assert_msg(result > 0, "start the sibling that replaces the polled descriptor");
        if (result > 0) {
            ptest_assert_msg(poll64(&fd, 1, 2000) == 1 && fd.revents == PTOS_POLLNVAL,
                             "the retained device reports POLLNVAL after its descriptor changes");
            ptest_assert_msg(join_tid(tid, &status) == 0 && !status, "the sibling replaced the binding");
            ptest_assert_msg(poll64(&fd, 1, 0) == 1 && fd.revents == PTOS_POLLIN && Fwrite(4, 1, "x") == 1,
                             "a new poll and write use the replacement description");
            ptest_assert_msg(Fcntl((short)control, 0, XFS_TEST_CLOSE_COUNT) == closes + 2,
                             "the old retained FILEPTR closes exactly once");
        }
    }
done:
    if (fd.fd >= 0)
        Fclose((short)fd.fd);
    if (control >= 0)
        Fclose((short)control);
    if (stack > 0)
        ptest_assert_msg(Mfree((void *)stack) == 0, "release the device probe stack");
    ptest_pass();
    if (ptest_failures() == failures)
        conws("native XFS Fcntl and pollable device ownership: PASS\r\n");
}
#endif

static void xfs_check_open_mode(const char *path)
{
    static const short unsupported[] = { 0x10, 0x100 }; /* deny read/write, nonblocking */
    static const char fat_path[] = "C:\\XFSFLAG.TMP";
    const short deny_none = 0x42; /* read/write, deny none */
    long fd;
    unsigned i;

    for (i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); i++) {
        fd = Fopen(path, unsupported[i]);
        ptest_assert_msg(fd == -32L, "XFS rejects unsupported Fopen flags at the GEMDOS trap");
        if (fd >= 0)
            Fclose((short)fd);
    }
    fd = Fopen(path, deny_none);
    ptest_assert_msg(fd >= 0, "XFS accepts explicit deny-none at the GEMDOS trap");
    if (fd >= 0) {
#ifdef TEST_WITH_FPOLL_TIME64
        ptest_assert_msg(Fcntl((short)fd, 0, XFS_TEST_OPEN_FLAGS) == deny_none &&
                         Fcntl((short)fd, 0, F_GETFL) == 2,
                         "deny-none reaches FILEPTR.flags while F_GETFL reports access bits");
#endif
        ptest_assert_msg(Fclose((short)fd) == 0, "close the deny-none description");
    }

    fd = Fcreate(fat_path, 0);
    ptest_assert_msg(fd >= 0, "create a FAT file for the legacy masking check");
    if (fd >= 0) {
        ptest_assert_msg(Fclose((short)fd) == 0, "close the initial FAT description");
        fd = Fopen(fat_path, unsupported[0] | unsupported[1]);
        ptest_assert_msg(fd >= 0, "FAT still ignores the legacy high mode bits");
        if (fd >= 0)
            ptest_assert_msg(Fclose((short)fd) == 0, "close the masked FAT description");
        ptest_assert_msg(Fdelete(fat_path) == 0, "remove the FAT fixture");
    }
}

/* Exercise the mounted source driver through GEMDOS's real SFT bindings. */
static void xfs_inherited_reader(void *argument)
{
    char byte = 0;

    (void)argument;
    if (Fread(4, 1, &byte) != 1 || byte != 'c' || Fclose(4))
        Pterm(75);
}

static void test_xfs_bridge(void)
{
    static const char path[] = "R:\\FIXTURE\\DATA.TXT";
    _DTA directory, *saved = (_DTA *)Fgetdta();
    char byte = 0;
    long fd, alias, stack;
    int32_t pid = 0;
    int failures = ptest_failures();

    ptest_begin("FreeMiNT XFS shared open-file description");
    fd = Fcreate(path, 0);
    ptest_assert_msg(fd >= 0, "open the XFS FILEPTR");
    if (fd < 0)
        goto done;

    ptest_assert_msg(Fwrite((short)fd, 6, "abcdef") == 6 &&
                     Fseek(0, (short)fd, 0) == 0 && Fforce(4, (short)fd) == 0,
                     "DEVDRV write, seek and shared standard binding");
    alias = Fdup(4);
    ptest_assert_msg(alias >= 0, "duplicate the FILEPTR binding");
    if (alias >= 0) {
        ptest_assert_msg(Fread((short)fd, 1, &byte) == 1 && byte == 'a' &&
                         Fread((short)alias, 1, &byte) == 1 && byte == 'b' &&
                         Fclose((short)alias) == 0,
                         "shared cursor and independent close right");
    }
    stack = Malloc(RUNNABLE_STACK_SIZE);
    ptest_assert_msg(stack > 0, "allocate XFS inheritance probe stack");
    if (stack > 0) {
        ptest_assert_msg(launch_entry(xfs_inherited_reader, CLONE_VM | CLONE_COPY_ALL_HANDLES,
                                       (void *)stack, &pid, NULL) > 0 && pid > 0,
                         "inherit the same FILEPTR in a second PROCESS");
        if (pid > 0)
            ptest_assert_msg(Pwaitpid(pid, 0, 0) == ((long)pid << 16),
                             "child advances the cursor and releases its bindings");
        ptest_assert_msg(Mfree((void *)stack) == 0, "release XFS inheritance probe stack");
    }
    ptest_assert_msg(Fclose((short)fd) == 0 && Fread(4, 1, &byte) == 1 &&
                     byte == 'd' && Fclose(4) == 0,
                     "final close belongs to the last binding");
    xfs_check_open_mode(path);

    fd = Fcreate(path, 2); /* FA_HIDDEN */
    ptest_assert_msg(fd >= 0, "replace an XFS file with the hidden attribute");
    if (fd >= 0) {
        ptest_assert_msg(Fattrib(path, 0, 0) == 2 && Fseek(0, (short)fd, 2) == 0,
                         "replacement changes the attribute and truncates the file");
        ptest_assert_msg(Fclose((short)fd) == 0, "close the hidden XFS description");
    }
    fd = Fcreate(path, 0);
    ptest_assert_msg(fd >= 0, "replace the hidden XFS file with normal attributes");
    if (fd >= 0)
        ptest_assert_msg(Fattrib(path, 0, 0) == 0 && Fclose((short)fd) == 0,
                         "replacement clears the hidden attribute");

    fd = Fopen("R:\\FIXTURE\\NULL.TXT", 2);
    ptest_assert_msg(fd >= 0, "open the unchanged FreeMiNT DEVDRV callback subset");
    if (fd >= 0)
        ptest_assert_msg(Fread((short)fd, 1, &byte) == 0 &&
                         Fwrite((short)fd, 1, &byte) == 1 &&
                         Fseek(0, (short)fd, 0) == 0 && Fclose((short)fd) == 0,
                         "FreeMiNT callbacks run through the native bridge");

    (void)Fsetdta(&directory);
    ptest_assert_msg(Fsfirst("R:\\FIXTURE\\*.TXT", 0) == 0 &&
                     !strcmp(directory.dta_name, "DATA.TXT") &&
                     Fsnext() == 0 && !strcmp(directory.dta_name, "NULL.TXT") &&
                     Fsnext() == -49L, "XFS DIR and cookie lifetime through Fsfirst/Fsnext");
    (void)Fsetdta(saved);
done:
    ptest_pass();
    if (ptest_failures() == failures)
        conws("native FreeMiNT XFS over shared PFS descriptions: PASS\r\n");
}
