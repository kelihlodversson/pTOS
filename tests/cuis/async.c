/* Exercise standard MiNT process calls; no custom scheduling syscall. */
#include "config.h"
#include "libaes.h"
#include "gemerror.h"
#include "sysconf.h"
#include "bdosdefs.h"
#include "ptos.h"
#define CHECK(c) do { if (!(c)) return 0x6000 | __LINE__; } while (0)
static LONG call(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    register UQUAD ax __asm__("rax") = (1ULL << 32) | fn;
    register UQUAD di __asm__("rdi") = a, si __asm__("rsi") = b;
    register UQUAD dx __asm__("rdx") = c, r10 __asm__("r10") = d;
    __asm__ volatile ("syscall" : "+a"(ax) : "r"(di), "r"(si), "r"(dx), "r"(r10)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
static const char environment[] = { 0, 0 };
static const char digits[] = "0123456789abcdef";
static void hex(char *out, ULONG value)
{
    UWORD i;
    for (i = 0; i < 8; i++) out[i] = digits[(value >> ((7-i)*4)) & 15];
}
static ULONG unhex(const char *in)
{
    ULONG value = 0;
    UWORD i;
    for (i = 0; i < 8; i++) value = (value << 4) | (in[i] <= '9' ? in[i]-'0' : in[i]-'a'+10);
    return value;
}
static void tail(char *buffer, char mode, LONG session, LONG pid)
{
    buffer[0] = 18; buffer[1] = 'c'; buffer[2] = mode;
    hex(buffer+3, (ULONG)session); hex(buffer+11, (ULONG)pid); buffer[19] = 0;
}
static LONG spawn(const char *path, char *command)
{
    return call(0x4b, 100, (uintptr_t)path, (uintptr_t)command, (uintptr_t)environment);
}
static LONG waitpid(LONG pid, LONG flags, UQUAD usage)
{
    return call(0x13a, (QUAD)pid, flags, usage, ~0ULL);
}
int main(int argc, char **argv, char **envp)
{
    LONG session, pid, me, parent, status, sender, base;
    LONG children[7];
    ULONG a, b, c, d, usage[2], i, j, child_env;
    char command[20];
    (void)envp;
    CHECK(argc == 2);
    session = aes_session_open(); me = call(0x10b, 0, 0, 0, 0);
    CHECK(session >= 0 && me > 0);
    if (argv[1][0] == 'c') {
        char mode = argv[1][1];
        parent = (LONG)unhex(argv[1]+10);
        if (mode == 'd') {
            CHECK(call(0x10c, 0, 0, 0, 0) != parent);
            CHECK(aes_message_send((LONG)unhex(argv[1]+2), 1, 2, 3, 4) == EACCDN);
            CHECK(aes_message_wait(session, 5) == 0);
            {
                static const char marker[] = "x32 async orphan: PASS\r\n";
                ptos_write(1, marker, sizeof(marker)-1);
            }
            return 0;
        }
        CHECK(call(0x10c, 0, 0, 0, 0) == parent);
        CHECK(aes_message_send((LONG)unhex(argv[1]+2), me, parent, 3, 4) == E_OK);
        if (mode == '1') _Exit(77);
        if (mode == '2') __asm__ volatile ("ud2");
        CHECK(aes_message_wait(session, 5) == 0);
        return 0x1234;
    }
    if (argv[1][0] == 'o' || argv[1][0] == 'z') {
        tail(command, argv[1][0] == 'o' ? 'd' : '1', session, me);
        CHECK(spawn(argv[0], command) > 0);
        if (argv[1][0] == 'z') CHECK(call(0xff, 0, 0, 0, 0) == 0);
        return 0; /* Orphan a new child or an unreaped zombie. */
    }
    CHECK(waitpid(-1, 1, ~0ULL) == EFILNF);
    CHECK(waitpid(-1, 4, 0) == EINVAL);
    CHECK(waitpid(0, 0, 0) == EINVFN); /* process groups are not implemented */
    CHECK(call(0x4b, 104, 0, 0, 0) == EINVFN);
    CHECK(call(0x4b, 106, 0, 0, 0) == EIMBA);
    for (i = 0; i < 5; i++) {
        tail(command, i == 1 ? '1' : i == 2 ? '2' : '0', session, me);
        if (i == 4) {
            base = call(0x4b, 3, (uintptr_t)argv[0], (uintptr_t)command, (uintptr_t)environment);
            CHECK(base > 0);
            child_env = (ULONG)(uintptr_t)USERPTR_TO_PTR(((PD *)(uintptr_t)(ULONG)base)->p_env);
            CHECK(call(0x17d, (ULONG)base, sizeof(PD), 0, 0) == E_OK);
            pid = call(0x4b, 106, 0, (ULONG)base, 0);
            CHECK(pid > 0);
            CHECK(call(0x17d, (ULONG)base, sizeof(PD), 1, 0) == EINVAL);
            CHECK(call(0x17d, child_env, 2, 1, 0) == EINVAL);
            CHECK(call(0x49, (ULONG)base, 0, 0, 0) == EACCDN);
            CHECK(call(0x4b, 106, 0, (ULONG)base, 0) == EIMBA);
        } else pid = spawn(argv[0], command);
        CHECK(pid > 0 && pid != me);
        CHECK(waitpid(pid, 1, ~0ULL) == 0); /* Child has not run yet. */
        CHECK(waitpid(me, 1, 0) == EFILNF);
        CHECK(call(0xff, 0, 0, 0, 0) == 0);
        CHECK(aes_message_wait(session, -1) == 1);
        CHECK(aes_message_read(session, &sender, &a, &b, &c, &d) == 1 && a == (ULONG)pid && b == (ULONG)me);
        CHECK(waitpid(pid, 0, (1ULL << 32) | (uintptr_t)usage) == EIMBA);
        CHECK(waitpid(pid, 0, (uintptr_t)main) == EIMBA);
        if (i == 3) status = call(0x11c, 0, (uintptr_t)usage, ~0ULL, ~0ULL);
        else status = waitpid(pid, 0, (uintptr_t)usage);
        CHECK(((ULONG)status >> 16) == (ULONG)pid);
        CHECK((status & 0xffff) == (i == 1 ? 77 : i == 2 ? 0x0400 : 0x34));
        CHECK(usage[1] == 0);
        CHECK(waitpid(pid, 1, 0) == EFILNF);
    }
    tail(command, '0', session, me);
    for (i = 0; i < 7; i++) { children[i] = spawn(argv[0], command); CHECK(children[i] > 0); }
    CHECK(spawn(argv[0], command) == EAGAIN); /* Zombies also occupy a bounded slot. */
    base = call(0x4b, 3, (uintptr_t)argv[0], (uintptr_t)command, (uintptr_t)environment);
    CHECK(base > 0);
    child_env = (ULONG)(uintptr_t)USERPTR_TO_PTR(((PD *)(uintptr_t)(ULONG)base)->p_env);
    CHECK(call(0x4b, 106, 0, (ULONG)base, 0) == EAGAIN);
    CHECK(call(0x17d, (ULONG)base, sizeof(PD), 0, 0) == E_OK);
    CHECK(call(0x49, (ULONG)base, 0, 0, 0) == E_OK);
    CHECK(call(0x49, child_env, 0, 0, 0) == E_OK);
    for (i = 0; i < 7; i++) {
        status = call(0x109, ~0ULL, ~0ULL, ~0ULL, ~0ULL);
        CHECK(status > 0 && (status & 0xffff) == 0x34);
        for (j = 0; j < 7 && children[j] != (LONG)((ULONG)status >> 16); j++) ;
        CHECK(j < 7); children[j] = 0;
    }
    for (i = 0; i < 7; i++) CHECK(aes_message_read(session, &sender, &a, &b, &c, &d) == 1);
    for (i = 0; i < 16; i++) {
        pid = spawn(argv[0], command); CHECK(pid > 0);
        CHECK(((ULONG)waitpid(pid, 0, 0) >> 16) == (ULONG)pid);
        CHECK(aes_message_read(session, &sender, &a, &b, &c, &d) == 1);
    }
    CHECK(call(0x109, 0, 0, 0, 0) == EFILNF);
    {
        static const char marker[] = "x32 async launch/wait: PASS\r\n";
        ptos_write(1, marker, sizeof(marker)-1);
    }
    return 0;
}
