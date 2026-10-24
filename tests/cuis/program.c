/* Real standard-ELF clients, with overlapping lifetimes in distinct spaces. */
#include "libaes.h"
#include "gemerror.h"
#include "ptos.h"
extern int64_t client_pexec(const char *path, const char *tail, const char *env);

static LONG raw(UQUAD function, UQUAD address, UQUAD size,
                    UQUAD output, UQUAD output_size)
{
    register UQUAD ax __asm__("rax") = ((UQUAD)CUIS_SERVICE_CLASS << 32) | function;
    register UQUAD di __asm__("rdi") = address, si __asm__("rsi") = size;
    register UQUAD dx __asm__("rdx") = output, r10 __asm__("r10") = output_size;
    __asm__ volatile ("syscall" : "+a"(ax)
        : "r"(di), "r"(si), "r"(dx), "r"(r10)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
static LONG query(ULONG handle)
{
    CUIS_SESSION_REQUEST r = { sizeof(r), CUIS_VERSION, CUIS_SESSION_QUERY, 0, 0, { 0 } };
    CUIS_SESSION_INFO info;
    r.handle = handle;
    return raw(0, (uintptr_t)&r, sizeof(r), (uintptr_t)&info, sizeof(info));
}
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
static int malformed(void)
{
    CUIS_SESSION_REQUEST r = { sizeof(r), CUIS_VERSION, CUIS_SESSION_ATTACH, 0, 0, { 0 } };
    CUIS_SESSION_INFO info;
    static const CUIS_SESSION_INFO readonly_info = { 0 };
    UQUAD a = (uintptr_t)&r, b = (uintptr_t)&info;
    CHECK(raw(99, 0, 0, 0, 0) == EINVFN);
    CHECK(raw(0, a, sizeof(r) | (1ULL << 32), b, sizeof(info)) == EINVAL);
    CHECK(raw(0, a | (1ULL << 32), sizeof(r), b, sizeof(info)) == EIMBA);
    CHECK(raw(0, a + 1, sizeof(r), b, sizeof(info)) == EIMBA);
    CHECK(raw(0, 0, sizeof(r), b, sizeof(info)) == EIMBA);
    CHECK(raw(0, a, sizeof(r), b | (1ULL << 32), sizeof(info)) == EIMBA);
    CHECK(raw(0, a, sizeof(r), (uintptr_t)&readonly_info, sizeof(info)) == EIMBA);
    CHECK(raw(0, a, sizeof(r), b, sizeof(info) | (1ULL << 32)) == EINVAL);
    r.version++;
    CHECK(raw(0, a, sizeof(r), b, sizeof(info)) == EOPNOTSUPP);
    r.version--; r.reserved[2] = 1;
    CHECK(raw(0, a, sizeof(r), b, sizeof(info)) == EINVAL);
    CHECK(aes_session_query(&info) == EACCDN);
    return 0;
}
int main(int argc, char **argv, char **envp)
{
    CUIS_SESSION_INFO info, again;
    GRECT first = { 0, 0, 20, 20 }, second = { 10, 10, 20, 20 };
    ULONG owner = 0, previous;
    char tail[] = { 9, 'c', '0', '0', '0', '0', '0', '0', '0', '0', 0 };
    static const char digits[] = "0123456789abcdef";
    int i, rc;
    (void)envp;
    CHECK(argc == 1 || argc == 2);
    CHECK(aes_session_query(&info) == EACCDN);
    if (argc == 2) {
        for (i = 1; i <= 8; i++) {
            char c = argv[1][i];
            CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
            owner = (owner << 4) | (c <= '9' ? c - '0' : c - 'a' + 10);
        }
        CHECK(query(owner) == EACCDN);
    } else {
        rc = malformed();
        if (rc) return rc;
    }
    CHECK(aes_session_open(&info) == E_OK);
    CHECK(info.version == CUIS_VERSION && info.size == sizeof(info) && info.handle);
    CHECK(aes_session_open(&again) == E_OK && again.handle == info.handle);
    CHECK(rc_intersect(&first, &second) && second.g_x == 10 && second.g_y == 10 &&
          second.g_w == 10 && second.g_h == 10 && inside(19, 19, &second));
    if (argc == 2) {
        CHECK(info.handle != owner && query(owner) == EACCDN);
        if (argv[1][0] == 'f') __asm__ volatile ("ud2");
        if (argv[1][0] == 'l') _Exit(77);
        return 77;             /* libaes destructor closes the child session */
    }
    for (i = 0; i < 8; i++) tail[2 + i] = digits[(info.handle >> ((7 - i) * 4)) & 15];
    for (i = 0; i < 3; i++) {
        tail[1] = i == 0 ? 'c' : i == 1 ? 'l' : 'f';
        CHECK(client_pexec(argv[0], tail, "") == (i == 2 ? 0xffff : 77));
        CHECK(aes_session_query(&again) == E_OK && again.handle == info.handle);
    }
    previous = info.handle;
    CHECK(aes_session_close() == E_OK && aes_session_close() == E_OK);
    CHECK(query(previous) == EACCDN);
    CHECK(aes_session_open(&info) == E_OK && info.handle != previous);
    CHECK(query(previous) == EACCDN);
    CHECK(aes_session_close() == E_OK);
    {
        static const char pass[] = "x32 libaes/CUIS sessions: PASS\r\n";
        CHECK(ptos_write(1, pass, sizeof(pass) - 1) == sizeof(pass) - 1);
    }
    return 77;
}
