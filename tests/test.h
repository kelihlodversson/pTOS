/*
 * test.h - regression test library API
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef TEST_H
#define TEST_H

/*
 * Minimal regression test library for pTOS.
 *
 * Usage from a test suite:
 *
 *   void test_foo(void)
 *   {
 *       ptest_begin("foo");
 *       ptest_assert(some_condition);
 *       ptest_assert_msg(another_condition, "expected 42");
 *       ptest_pass();
 *   }
 *
 * Or on failure:
 *
 *       ptest_fail("unexpected value");
 */

/*
 * Begin a named test.  Must be called before any assertions.
 */
void ptest_begin(const char *name);

/*
 * Assert a condition.  If false, the current test fails.
 */
void ptest_assert(int condition);

/*
 * Assert a condition with a message.  If false, the current test fails
 * and the message is printed.
 */
void ptest_assert_msg(int condition, const char *msg);

/*
 * Mark the current test as passed.  Must be called after ptest_begin()
 * if all assertions passed.
 */
void ptest_pass(void);

/*
 * Mark the current test as failed with a message.  Can be called at
 * any point after ptest_begin() to immediately fail the test.
 */
void ptest_fail(const char *msg);

/*
 * Run all enabled test suites.  Implemented in the generated run_tests.c.
 */
void ptest_run_tests(void);

/*
 * Output a NUL-terminated string to the console via GEMDOS Cconws.
 */
void conws(const char *s);

/*
 * Return the number of failed tests so far.
 */
int ptest_failures(void);

/*
 * Print a summary of all tests run.  Returns the number of failures.
 */
int ptest_summary(void);

#endif /* TEST_H */
