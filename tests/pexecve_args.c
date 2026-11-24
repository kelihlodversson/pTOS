/* Hosted exercise of the production capture with checked ILP32 user memory. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "gemerror.h"
#include "kheap.h"
#include "kproc.h"
#include "launch_args.h"
#include "string.h"

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "pexecve_args:%d: %s\n", \
                                        __LINE__, #expr); abort(); } } while (0)
#define USER_BASE 0x80010000ULL
#define LAST_SLOT 0xfffffffcULL
#define PATH_ADDR (USER_BASE + 0x100)
#define ARGV_ADDR (USER_BASE + 0x200)
#define ENVP_ADDR (USER_BASE + 0x2000)
#define STRING_ADDR (USER_BASE + 0x4000)

static char memory[128 * 1024];
static ULONG last_slot;
static unsigned reads, live, allocations;
static BOOL fail_allocation;
static UQUAD fault;
static LONG fault_code;

void *kalloc(ULONG bytes)
{
    void *ptr;

    allocations++;
    if (fail_allocation)
        return NULL;
    ptr = calloc(1, bytes);
    CHECK(ptr != NULL);
    live++;
    return ptr;
}

void kfree(void *ptr)
{
    if (ptr) {
        CHECK(live == 1);
        live--;
        free(ptr);
    }
}

LONG kproc_copy_from_user_checked(void *out, VADDR address, ULONG bytes)
{
    reads++;
    CHECK(address <= 0xffffffffULL && bytes <= 0x100000000ULL - address);
    if (fault && address <= fault && bytes > fault - address)
        return fault_code;
    if (address >= LAST_SLOT) {
        memcpy(out, (const char *)&last_slot + (address - LAST_SLOT), bytes);
        return E_OK;
    }
    CHECK(address >= USER_BASE);
    if (address - USER_BASE >= sizeof(memory) ||
        bytes > sizeof(memory) - (address - USER_BASE))
        return EIMBA;
    memcpy(out, memory + (address - USER_BASE), bytes);
    return E_OK;
}

static char *user(UQUAD address)
{
    CHECK(address >= USER_BASE && address - USER_BASE < sizeof(memory));
    return memory + (address - USER_BASE);
}

static void put_slot(UQUAD vector, ULONG index, UQUAD address)
{
    ULONG pointer = (ULONG)address;

    CHECK(address == pointer);
    memcpy(user(vector + sizeof(pointer) * index), &pointer, sizeof(pointer));
}

static void fixture(void)
{
    memset(memory, 0, sizeof(memory));
    memcpy(user(PATH_ADDR), "C:\\BOOT.TOS", sizeof("C:\\BOOT.TOS"));
    fault = 0;
    fault_code = EIMBA;
    reads = 0;
    allocations = 0;
}

static void check_error(LONG expected, WORD mode, UQUAD path, UQUAD argv, UQUAD envp)
{
    PEXECVE_ARGS *args = NULL;

    CHECK(pexecve_args_capture(mode, path, argv, envp, &args) == expected);
    CHECK(!args && !live);
}

static void test_snapshot(void)
{
    PEXECVE_ARGS *args = NULL;
    char *name = user(STRING_ADDR);
    char *env = user(STRING_ADDR + 0x40);

    fixture();
    memcpy(name, "custom name", sizeof("custom name"));
    memcpy(env, "KEY=two words", sizeof("KEY=two words"));
    put_slot(ARGV_ADDR, 0, STRING_ADDR);
    put_slot(ARGV_ADDR, 1, STRING_ADDR + 0xfff);
    put_slot(ENVP_ADDR, 0, STRING_ADDR + 0x40);
    fault = STRING_ADDR + 0x1000;
    CHECK(pexecve_args_capture(0, PATH_ADDR, ARGV_ADDR, ENVP_ADDR, &args) == E_OK);
    CHECK(live == 1 && args->argc == 2 && args->envc == 1);
    CHECK(pexecve_args_capture(0, PATH_ADDR, ARGV_ADDR, ENVP_ADDR, &args) == EINVAL);
    memset(memory, 0xab, sizeof(memory));
    CHECK(!strcmp(args->path, "C:\\BOOT.TOS"));
    CHECK(!strcmp(args->argv[0], "custom name") && !args->argv[1][0] && !args->argv[2]);
    CHECK(!strcmp(args->envp[0], "KEY=two words") && !args->envp[1]);
    pexecve_args_release(&args);
    CHECK(!args && !live);

    fixture();
    CHECK(pexecve_args_capture(100, PATH_ADDR, ARGV_ADDR, ENVP_ADDR, &args) == E_OK);
    CHECK(!args->argc && !args->envc && !args->argv[0] && !args->envp[0]);
    pexecve_args_release(&args);
    CHECK(!args && !live);
}

static void test_rejection(void)
{
    fixture();
    check_error(EINVFN, 3, 0, 0, 0);
    check_error(EINVFN, -1, PATH_ADDR, ARGV_ADDR, ENVP_ADDR);
    check_error(EIMBA, 0, PATH_ADDR + 0x100000000ULL, ARGV_ADDR, ENVP_ADDR);
    check_error(EIMBA, 0, PATH_ADDR, ARGV_ADDR + 0x100000000ULL, ENVP_ADDR);
    check_error(EIMBA, 0, PATH_ADDR, ARGV_ADDR, 0);
    check_error(EIMBA, 0, PATH_ADDR, LAST_SLOT + 1, ENVP_ADDR);
    CHECK(!reads && !allocations);

    fail_allocation = TRUE;
    check_error(ENSMEM, 200, PATH_ADDR, ARGV_ADDR, ENVP_ADDR);
    CHECK(!reads);
    fail_allocation = FALSE;
    last_slot = (ULONG)STRING_ADDR;
    check_error(EIMBA, 200, PATH_ADDR, LAST_SLOT, ENVP_ADDR);
    last_slot = 0xffffffffUL;
    put_slot(ARGV_ADDR, 0, LAST_SLOT + 3);
    check_error(EIMBA, 200, PATH_ADDR, ARGV_ADDR, ENVP_ADDR);

    fixture();
    put_slot(ENVP_ADDR, 0, STRING_ADDR);
    memcpy(user(STRING_ADDR), "A=B", sizeof("A=B"));
    fault = STRING_ADDR + 2;
    fault_code = EREADF;
    check_error(EREADF, 200, PATH_ADDR, ARGV_ADDR, ENVP_ADDR);
    fault = ENVP_ADDR + sizeof(ULONG);
    fault_code = EIMBA;
    check_error(EIMBA, 100, PATH_ADDR, ARGV_ADDR, ENVP_ADDR);

    fixture();
    memset(user(PATH_ADDR), 'x', PEXECVE_PATH_MAX);
    check_error(ERANGE, 0, PATH_ADDR, ARGV_ADDR, ENVP_ADDR);
    *user(PATH_ADDR) = 0;
    check_error(EFILNF, 0, PATH_ADDR, ARGV_ADDR, ENVP_ADDR);
}

static void test_limits(void)
{
    const ULONG string_limit = PEXECVE_ARG_MAX - 3 * sizeof(ULONG);
    PEXECVE_ARGS *args = NULL;
    ULONG i;

    fixture();
    put_slot(ARGV_ADDR, 0, STRING_ADDR);
    memset(user(STRING_ADDR), 'x', string_limit - 1);
    CHECK(pexecve_args_capture(200, PATH_ADDR, ARGV_ADDR, ENVP_ADDR, &args) == E_OK);
    CHECK(args->argc == 1 && strlen(args->argv[0]) == string_limit - 1);
    pexecve_args_release(&args);
    user(STRING_ADDR)[string_limit - 1] = 'x';
    check_error(ERANGE, 200, PATH_ADDR, ARGV_ADDR, ENVP_ADDR);

    fixture();
    for (i = 0; i < PEXECVE_MAX_ENTRIES - 1; i++)
        put_slot(ARGV_ADDR, i, STRING_ADDR);
    put_slot(ENVP_ADDR, 0, STRING_ADDR);
    CHECK(pexecve_args_capture(0, PATH_ADDR, ARGV_ADDR, ENVP_ADDR, &args) == E_OK);
    CHECK(args->argc == PEXECVE_MAX_ENTRIES - 1 && args->envc == 1);
    CHECK(!args->argv[args->argc] && !args->envp[args->envc]);
    pexecve_args_release(&args);
    put_slot(ENVP_ADDR, 1, STRING_ADDR);
    check_error(ERANGE, 0, PATH_ADDR, ARGV_ADDR, ENVP_ADDR);
}

int main(void)
{
    test_snapshot();
    test_rejection();
    test_limits();
    puts("Pexecve capture: ILP32 slots, immutable snapshot, limits and rollback: PASS");
    return 0;
}
