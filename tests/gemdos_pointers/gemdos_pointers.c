/*
 * gemdos_pointers.c - regression test for the validation of GEMDOS pointers
 * (CONF_WITH_USER_COPY, #437, #439)
 *
 * A call with a null pointer, or a negative length, is refused before it
 * runs (EIMBA, ERANGE).  The calls are chosen so that every shape of the
 * argument frame is met: a pointer after a word (Fread, Frename), a pointer
 * first (Dgetpath, Fdatime, Fopen) and the four-argument Pexec().  On m68k
 * the frame is packed in words, and a wrong width would read the pointer or
 * the length from the wrong place and let the call through (or refuse a good
 * one).  A build without the check passes without testing anything.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>

#define EIMBA   (-40L)
#define ERANGE  (-64L)

void test_gemdos_pointers(void);

void test_gemdos_pointers(void)
{
#ifdef TEST_WITH_USER_COPY
    char path[128];
    char buf[16];
#endif

    ptest_begin("gemdos_pointers");

#ifdef TEST_WITH_USER_COPY
    /* the pointer is the first argument */
    ptest_assert_msg(Fopen((const char *)0, 0) == EIMBA, "Fopen(NULL) was not refused");
    ptest_assert_msg(Dgetpath((char *)0, 0) == EIMBA, "Dgetpath(NULL) was not refused");
    ptest_assert_msg(Fdatime((_DOSTIME *)0, 0, 0) == EIMBA, "Fdatime(NULL) was not refused");

    /* ... after a word and a long: Fread(handle, count, buf), Fwrite() */
    ptest_assert_msg(Fread(0, 4L, (void *)0) == EIMBA, "Fread(buf NULL) was not refused");
    ptest_assert_msg(Fwrite(1, 4L, (void *)0) == EIMBA, "Fwrite(buf NULL) was not refused");
    ptest_assert_msg(Fread(0, -1L, buf) == ERANGE, "Fread(count -1) was not refused");
    ptest_assert_msg(Fwrite(1, -2L, buf) == ERANGE, "Fwrite(count -2) was not refused");

    /* two names, after a word: either one can be the null */
    ptest_assert_msg(Frename(0, (const char *)0, "X") == EIMBA, "Frename(old NULL) was not refused");
    ptest_assert_msg(Frename(0, "X", (const char *)0) == EIMBA, "Frename(new NULL) was not refused");

    /* Pexec(mode, path, tail, env): the path of mode 0, the tail of mode 5 */
    ptest_assert_msg(Pexec(0, (void *)0, "", (void *)0) == EIMBA, "Pexec(path NULL) was not refused");
    ptest_assert_msg(Pexec(5, (void *)0, (void *)0, (void *)0) == EIMBA, "Pexec(tail NULL) was not refused");

    /* and a good pointer still works */
    ptest_assert_msg(Dgetpath(path, 0) == 0, "Dgetpath() with a buffer failed");
    ptest_assert_msg(Fread(0, 0L, buf) == 0, "Fread() of no bytes failed");
#endif

    ptest_pass();
}
