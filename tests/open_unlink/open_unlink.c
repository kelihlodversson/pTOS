/* Native FAT unlink lifetime, independent opens and directory-slot reuse. */
#include "test.h"
#include <mint/osbind.h>
#include <string.h>

#define FIXTURE "C:\\UNLINK.TST"
void test_open_unlink(void);
#ifdef TEST_WITH_FAT_FILE_LIFETIMES
void test_open_unlink(void)
{
    long a = -1, b = -1, replacement = -1;
    long missing;
    char original[] = "original", fresh[] = "new", buffer[16];
    ptest_begin("open_unlink (#660)");
    a = Fcreate(FIXTURE, 0);
    if (a < 0) { ptest_fail("cannot create fixture"); return; }
    ptest_assert_msg(Fwrite(a, 8L, original) == 8, "write original");
    b = Fopen(FIXTURE, 0);
    ptest_assert_msg(b >= 0, "second open");
    if (b < 0) goto done;
    ptest_assert_msg(Fdelete(FIXTURE) == 0, "unlink an open file");
    missing = Fopen(FIXTURE, 0);
    ptest_assert_msg(missing == -33L, "deleted name no longer opens");
    if (missing >= 0) Fclose(missing);
    replacement = Fcreate(FIXTURE, 0);
    ptest_assert_msg(replacement >= 0, "recreate same pathname");
    if (replacement < 0) goto done;
    ptest_assert_msg(Fwrite(replacement, 3L, fresh) == 3, "write replacement");
    ptest_assert_msg(Dcreate("C:\\ULDIR") == 0, "mkdir while original backing is open");
    ptest_assert_msg(Frename(0, FIXTURE, "C:\\ULDIR\\MOVED.TST") == 0 &&
                     Fseek(0L, replacement, 2) == 3, "cross-directory rename retains open identity");
    ptest_assert_msg(Frename(0, "C:\\ULDIR\\MOVED.TST", FIXTURE) == 0,
                     "rename live file back into reused directory slot");
    Ddelete("C:\\ULDIR");
    missing = Fopen(FIXTURE, 0);
    ptest_assert_msg(missing >= 0 && Fseek(0L, missing, 2) == 3,
                     "mkdir preserves the open-file metadata chain");
    if (missing >= 0) Fclose(missing);
    ptest_assert_msg(Fseek(0L, b, 0) == 0 && Fread(b, 8L, buffer) == 8 &&
                     !memcmp(buffer, original, 8), "old handle retains old contents");
    ptest_assert_msg(Fseek(0L, a, 2) == 8 && Fwrite(a, 1L, fresh) == 1,
                     "unlinked file may still grow");
    ptest_assert_msg(Fclose(a) == 0, "close original writer"); a = -1;
    ptest_assert_msg(Fseek(0L, b, 2) == 9, "last reader retains shared metadata");
    ptest_assert_msg(Fclose(b) == 0, "close final original handle"); b = -1;
    ptest_assert_msg(Fclose(replacement) == 0, "close replacement"); replacement = -1;
    replacement = Fopen(FIXTURE, 0);
    ptest_assert_msg(replacement >= 0 && Fseek(0L, replacement, 2) == 3 &&
                     Fseek(0L, replacement, 0) == 0 && Fread(replacement, 3L, buffer) == 3 &&
                     !memcmp(buffer, fresh, 3), "old close does not overwrite reused directory slot");
done:
    if (a >= 0) Fclose(a);
    if (b >= 0) Fclose(b);
    if (replacement >= 0) Fclose(replacement);
    Fdelete(FIXTURE);
    ptest_pass();
}

#else
#include <mint/basepage.h>

#ifndef TEST_WITH_USER_ASPACE
static void inherited_child(void)
{
    char c = 0;
    long rc = Fseek(0L, 4, 0);
    if (rc == 0 && Fread(4, 1L, &c) == 1 && c == 'x')
        Pterm(77);
    Pterm(1);
}

static void staged_inheritance(void)
{
    long a, saved, raw;
    BASEPAGE *child;
    char c = 'x';

    a = Fcreate("C:\\INHERIT.TST", 0);
    if (a < 0) { ptest_fail("cannot create inheritance fixture"); return; }
    ptest_assert_msg(Fwrite(a, 1L, &c) == 1, "write inheritance fixture");
    saved = Fdup(4);
    if (saved < 0) { Fclose(a); ptest_fail("cannot save AUX"); return; }
    Fforce(4, a);
    raw = Pexec(5, 0L, "", 0L);
    ptest_assert_msg(raw > 0, "stage child inheriting AUX");
    if (raw > 0)
    {
        child = (BASEPAGE *)raw;
        ptest_assert_msg(Mshrink(child, 4096L) == 0, "shrink staged child");
        child->p_hitpa = (char *)child + 4096;
        child->p_tbase = (char *)inherited_child;
        ptest_assert_msg(Fdelete("C:\\INHERIT.TST") == -36L,
                         "staged child's reference prevents deletion");
        ptest_assert_msg(Fseek(0L, a, 0) == 0 && Fread(a, 1L, &c) == 1 && c == 'x',
                         "refused deletion preserves caller's handles and data");
        Fclose(4);
        Fclose(a);
        a = -1;
        ptest_assert_msg(Fdelete("C:\\INHERIT.TST") == -36L,
                         "child-only reference prevents deletion and slot reuse");
        ptest_assert_msg(Pexec(6, 0L, child, 0L) == 77,
                         "child still reads its inherited file and releases it on exit");
    }
    else
        Fclose(4);
    Fforce(4, saved);
    Fclose(saved);
    if (a >= 0) Fclose(a);
    ptest_assert_msg(Fdelete("C:\\INHERIT.TST") == 0,
                     "deletion succeeds after final inherited reference closes");
}

static void staged_foreign_handle(void)
{
    long a, saved, raw;
    BASEPAGE *child;
    char c = 'x';

    if (!(Drvmap() & (1L << 16))) return; /* virtio-9p Q: */
    a = Fcreate("Q:\\INHERIT.TST", 0);
    if (a < 0) { ptest_fail("cannot create foreign inheritance fixture"); return; }
    ptest_assert_msg(Fwrite(a, 1L, &c) == 1, "write foreign fixture");
    saved = Fdup(4);
    if (saved < 0) { Fclose(a); ptest_fail("cannot save AUX"); return; }
    ptest_assert_msg(Fforce(4, a) == 0, "redirect AUX to a foreign handle");
    raw = Pexec(5, 0L, "", 0L);
    ptest_assert_msg(raw > 0, "stage child inheriting a foreign handle without an OFD");
    if (raw > 0)
    {
        child = (BASEPAGE *)raw;
        ptest_assert_msg(Mshrink(child, 4096L) == 0, "shrink foreign-handle child");
        child->p_hitpa = (char *)child + 4096;
        child->p_tbase = (char *)inherited_child;
        ptest_assert_msg(Pexec(6, 0L, child, 0L) == 77,
                         "child reads its inherited foreign handle");
    }
    Fclose(4);
    Fforce(4, saved);
    Fclose(saved);
    Fclose(a);
    ptest_assert_msg(Fdelete("Q:\\INHERIT.TST") == 0, "remove foreign fixture");
}

#endif

/* Small-ROM deletion closes this process's handles before freeing backing. */
void test_open_unlink(void)
{
    long a, b, alias, saved, replacement;
    char original[1024], buffer[1024];

    ptest_begin("open_unlink (traditional GEMDOS)");
#ifndef TEST_WITH_USER_ASPACE
    staged_inheritance();
    staged_foreign_handle();
#endif
    memset(original, 0x5a, sizeof(original));
    a = Fcreate(FIXTURE, 0);
    if (a < 0) { ptest_fail("cannot create fixture"); return; }
    ptest_assert_msg(Fwrite(a, sizeof(original), original) == sizeof(original),
                     "write new backing before its metadata is flushed");
    b = Fopen(FIXTURE, 0);
    /* GEMDOS Fdup accepts only standard handles. Temporarily redirect
     * AUX and leave that mapping live to test deletion closes it too. */
    saved = Fdup(4);
    alias = -1;
    if (saved >= 0 && Fforce(4, a) == 0)
    {
        alias = Fdup(4);
    }
    ptest_assert_msg(b >= 0 && alias >= 0, "independent open and duplicate");
    ptest_assert_msg(Fdelete(FIXTURE) == 0, "delete caller's open file");
    ptest_assert_msg(Fclose(4) == 0, "standard mapping reset by deletion");
    if (saved >= 0)
    {
        Fforce(4, saved);
        Fclose(saved);
    }
    ptest_assert_msg(Fopen(FIXTURE, 0) == -33L, "deleted name no longer opens");
    ptest_assert_msg(Fclose(a) == -37L, "writer closed by deletion");
    if (b >= 0)
        ptest_assert_msg(Fclose(b) == -37L, "independent reader closed by deletion");
    if (alias >= 0)
        ptest_assert_msg(Fclose(alias) == -37L, "duplicate closed by deletion");
    replacement = Fcreate(FIXTURE, 0);
    ptest_assert_msg(replacement >= 0, "recreate deleted name");
    if (replacement >= 0)
    {
        ptest_assert_msg(Fwrite(replacement, sizeof(original), original) == sizeof(original),
                         "write replacement backing");
        Fclose(replacement);
        replacement = Fopen(FIXTURE, 0);
        ptest_assert_msg(replacement >= 0, "reopen replacement");
        if (replacement >= 0)
        {
            ptest_assert_msg(Fread(replacement, sizeof(buffer), buffer) == sizeof(buffer) &&
                             !memcmp(buffer, original, sizeof(buffer)),
                             "replacement contents survive closing");
            Fclose(replacement);
        }
    }
    ptest_assert_msg(Fdelete(FIXTURE) == 0, "delete closed file");
    ptest_pass();
}
#endif
