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
