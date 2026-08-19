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

/* Output a NUL-terminated string to the console */
void conws(const char *s)
{
    Cconws(s);
}

/* Output a single character to the console */
static void conout(int ch)
{
    Cconout(ch);
}

/* Print a decimal number */
static void print_num(long n)
{
    char buf[12];
    char *p = buf + sizeof(buf) - 1;
    *p = '\0';
    if (n < 0) {
        conout('-');
        n = -n;
    }
    if (n == 0) {
        conout('0');
        return;
    }
    while (n > 0) {
        *--p = '0' + (n % 10);
        n /= 10;
    }
    conws(p);
}

static int total_tests;
static int failed_tests;
static int current_failed;
static const char *current_name;

void ptest_begin(const char *name)
{
    current_name = name;
    current_failed = 0;
    conws("  ");
    conws(name);
    conws("... ");
}

void ptest_assert(int condition)
{
    if (!condition && !current_failed) {
        current_failed = 1;
    }
}

void ptest_assert_msg(int condition, const char *msg)
{
    if (!condition && !current_failed) {
        current_failed = 1;
        conws("\n    FAIL: ");
        conws(msg);
    }
}

void ptest_pass(void)
{
    total_tests++;
    if (!current_failed) {
        conws("PASS\n");
    } else {
        failed_tests++;
        conws("FAIL\n");
    }
}

void ptest_fail(const char *msg)
{
    current_failed = 1;
    conws("\n    FAIL: ");
    conws(msg);
    ptest_pass();
}

int ptest_failures(void)
{
    return failed_tests;
}

int ptest_summary(void)
{
    conws("\n--- Summary: ");
    print_num(total_tests - failed_tests);
    conws(" passed, ");
    print_num(failed_tests);
    conws(" failed, ");
    print_num(total_tests);
    conws(" total ---\n");
    return failed_tests;
}
