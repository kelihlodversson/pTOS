/*
 * testlib.c - regression test library implementation
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>

#ifdef TEST_LOG_FILE
/*
 * The console is the only way a program has to say anything, and under an
 * emulator with no way to read it (the Amiga's, where the desktop shows what
 * a program prints on a screen that nobody reads) the results are written to
 * a file on the disk too, which the host reads from the image
 * (tools/run-amiberry-tests.py).  The text is kept and written when a test
 * ends and at the summary, so what has run is on the disk if a later test
 * hangs: Fclose() is what puts a file's data on the disk.
 */
#define LOG_NAME        "RUNTESTS.LOG"
#define LOG_SIZE        4096

static char logbuf[LOG_SIZE];
static int logn;
static int log_started;

static void log_flush(void)
{
    long fd;

    if (!logn)
        return;
    if (!log_started) {
        fd = Fcreate(LOG_NAME, 0);
        log_started = 1;
    } else {
        fd = Fopen(LOG_NAME, 2);
        if (fd >= 0)
            Fseek(0L, (short)fd, 2);
    }
    if (fd >= 0) {
        Fwrite((short)fd, (long)logn, logbuf);
        Fclose((short)fd);
    }
    logn = 0;
}

static void log_char(int ch)
{
    if (logn >= LOG_SIZE)
        log_flush();
    logbuf[logn++] = (char)ch;
}
#else
#define log_flush()
#define log_char(ch)
#endif

void ptest_log_flush(void)
{
    log_flush();
}

/* Output a NUL-terminated string to the console */
void conws(const char *s)
{
#ifdef TEST_LOG_FILE
    const char *p;

    for (p = s; *p; p++)
        log_char(*p);
#endif
    Cconws(s);
}

/* Output a single character to the console */
static void conout(int ch)
{
    log_char(ch);
    Cconout(ch);
}

/* Print a decimal number */
static void print_num(long n)
{
    char buf[12];
    char *p = buf + sizeof(buf) - 1;
    unsigned long un;
    *p = '\0';
    if (n < 0) {
        conout('-');
        /* -n overflows signed long when n is LONG_MIN; negating in the
         * corresponding unsigned type is well defined for every value. */
        un = 0UL - (unsigned long)n;
    } else {
        un = (unsigned long)n;
    }
    if (un == 0) {
        conout('0');
        return;
    }
    while (un > 0) {
        *--p = '0' + (un % 10);
        un /= 10;
    }
    conws(p);
}

static int total_tests;
static int failed_tests;
static int current_failed;
static const char *current_fail_msg;

void ptest_begin(const char *name)
{
    current_failed = 0;
    current_fail_msg = 0;
    conws("  ");
    conws(name);
    conws("... ");
}

void ptest_assert(int condition)
{
    if (!condition) {
        current_failed = 1;
    }
}

void ptest_assert_msg(int condition, const char *msg)
{
    if (!condition) {
        current_failed = 1;
        /* Keep the first message: a plain ptest_assert() failing first
         * must not suppress a later ptest_assert_msg()'s message. */
        if (!current_fail_msg)
            current_fail_msg = msg;
    }
}

/*
 * PASS/FAIL is printed here, completing the "  <name>... " line from
 * ptest_begin() with a single result word, so any failure detail
 * (deferred from ptest_assert_msg()/ptest_fail()) can only follow on
 * its own indented line afterward instead of splitting that line apart.
 */
void ptest_pass(void)
{
    total_tests++;
    if (!current_failed) {
        conws("PASS\r\n");
    } else {
        failed_tests++;
        conws("FAIL\r\n");
        if (current_fail_msg) {
            conws("    FAIL: ");
            conws(current_fail_msg);
            conws("\r\n");
        }
    }
    log_flush();
}

void ptest_fail(const char *msg)
{
    current_failed = 1;
    current_fail_msg = msg;
    ptest_pass();
}

int ptest_failures(void)
{
    return failed_tests;
}

int ptest_summary(void)
{
    conws("\r\n--- Summary: ");
    print_num(total_tests - failed_tests);
    conws(" passed, ");
    print_num(failed_tests);
    conws(" failed, ");
    print_num(total_tests);
    conws(" total ---\r\n");
    log_flush();
    return failed_tests;
}
