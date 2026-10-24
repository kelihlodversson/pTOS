/* Native FAT unlink lifetime, independent opens and directory-slot reuse. */
#include "test.h"
#include <mint/osbind.h>
#include <string.h>

#define FIXTURE "C:\\UNLINK.TST"
void test_open_unlink(void);
void test_open_unlink(void)
{
    long a = -1, b = -1, replacement = -1;
    long missing;
    char original[] = "original", fresh[] = "new", buffer[16];
    ptest_begin("open_unlink (#660)");
    a = Fcreate(FIXTURE, 0);
    if (a < 0) { ptest_fail("cannot create fixture"); ptest_pass(); return; }
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
