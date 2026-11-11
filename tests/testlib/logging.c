/* Host filesystem/process model; actual harness and logging code are linked. */
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "test.h"
#include <mint/basepage.h>
#include <mint/osbind.h>

static BASEPAGE base;
BASEPAGE *_base = &base;
static const char *program;
int ptest_harness_main(void);

long Fcreate(const char *name, int attributes)
{
    (void)attributes;
    return open(name, O_WRONLY | O_CREAT | O_TRUNC, 0600);
}
long Fopen(const char *name, int mode) { (void)mode; return open(name, O_RDWR); }
long Fseek(long offset, short fd, int mode) { return lseek(fd, offset, mode); }
long Fwrite(short fd, long count, const void *data) { return write(fd, data, count); }
long Fclose(short fd) { return close(fd); }
void Cconws(const char *text) { (void)text; }
void Cconout(int ch) { (void)ch; }
void Pterm(int status) { (void)status; } /* main also returns its status. */

void pclone_run_unmanaged_tests(void)
{
    /* This fixture exercises log ownership, not native clone execution. */
    abort(); /* No unmanaged worker is launched by this model. */
}
void pclone_run_staged_tests(void) { abort(); }

void pclone_run_managed_tests(void)
{
    ptest_begin("managed worker");
    ptest_assert_msg(!getenv("INJECT_FAILURE"), "worker failure diagnostic");
    ptest_pass();
}
void ptest_run_tests(void)
{
    pid_t child;
    int status;
    ptest_begin("outer before");
    ptest_assert_msg(!getenv("INJECT_FAILURE"), "outer failure diagnostic");
    ptest_pass();
    conws("parent prefix before launch\r\n");
    ptest_log_flush();
    child = fork();
    assert(child >= 0);
    if (!child) {
        execl(program, program, "--clone", (char *)NULL);
        _exit(127);
    }
    assert(waitpid(child, &status, 0) == child);
    ptest_begin("outer after");
    ptest_assert_msg(WIFEXITED(status) && WEXITSTATUS(status) == 0,
                     "worker failure propagated to parent");
    ptest_pass();
}
int main(int argc, char **argv)
{
    program = argv[0];
    if (argc == 2 && !strcmp(argv[1], "--clone")) {
        base.p_cmdlin[0] = 7;
        memcpy(base.p_cmdlin + 1, "--clone", 7);
    }
    return ptest_harness_main();
}
