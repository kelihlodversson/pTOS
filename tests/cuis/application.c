/* Exercise the public GEM bindings against a failing/private transport. */
#include "gem.h"
#include "libaes.h"
#include "gemerror.h"
#include "../../lib/aes/transport.h"
#define CHECK(c) do { if (!(c)) { __builtin_printf("GEM API failure: %d\n", __LINE__); return __LINE__; } } while (0)
static LONG pid = 7, handle = 2147483600L, failure, yield_result, close_result;
static BOOL live;
static ULONG starts, closes, yields, delays;
static UQUAD interval;
LONG aes_process_call(ULONG fn)
{
    if (fn == 0x10b) return pid;
    if (fn == 0xff) { yields++; return yield_result; }
    return EINVFN;
}
LONG cuis_call(ULONG fn, LONG id)
{
    if (fn == CUIS_SESSION_START) {
        starts++;
        if (failure) return failure;
        live = TRUE; return handle;
    }
    if (!live || id != handle) return EACCDN;
    if (fn == CUIS_SESSION_QUERY) return E_OK;
    if (fn == CUIS_SESSION_END) {
        if (close_result) return close_result;
        live = FALSE; closes++; return E_OK;
    }
    return EINVFN;
}
LONG cuis_call6(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e, UQUAD f)
{
    (void)c; (void)d; (void)e; (void)f;
    if (fn != CUIS_SESSION_DELAY || a != (UQUAD)handle) return EINVAL;
    delays++; interval = b;
    return failure;
}
int main(void)
{
    short other[AES_GLOBMAX], a = 1, b = 2, c = 3, d = 4;
    ULONG count;
    CHECK(gl_apid == -1 && aes_global[2] == -1);
    CHECK(appl_exit() == 0 && appl_yield() == 0 && evnt_timer(0) == 0);
    CHECK(mt_appl_init(NULL) == -1);
    pid = 32768; CHECK(appl_init() == -1 && !starts && gl_apid == -1);
    pid = -1; CHECK(appl_init() == -1 && !starts);
    pid = 7; failure = ENSMEM;
    CHECK(appl_init() == -1 && gl_ap_version == 0 && !live);
    failure = 0;
    CHECK(appl_init() == 7 && gl_apid == 7 && _AESapid == 7 && _AESnumapps == -1);
    CHECK(gl_ap_version == aes_global[0] && gl_ap_version == 0x0400);
    CHECK(mt_appl_init(other) == 7 && other[2] == 7 && gl_apid == 7);
    CHECK(appl_init() == 7 && closes == 0); /* Repeated registration is idempotent. */
    CHECK(appl_yield() == 1 && yields == 1);
    yield_result = EACCDN; CHECK(mt_appl_yield(other) == 0);
    yield_result = 0;
    CHECK(evnt_timer(0) == 1 && delays == 1 && interval == 0);
    CHECK(mt_evnt_timer(0xffffffffUL, other) == 1 && interval == 0xffffffffULL);
    count = delays;
    if (sizeof(unsigned long) > 4) CHECK(evnt_timer((unsigned long)(1ULL << 32)) == 0 && delays == count);
    failure = EACCDN; CHECK(evnt_timer(1) == 0); failure = 0;
    CHECK(appl_getinfo(4, &a, &b, &c, &d) == 0 && !(a | b | c | d));
    CHECK(mt_appl_getinfo(-1, NULL, NULL, NULL, NULL, other) == 0);
    other[2] = 8; CHECK(mt_appl_exit(other) == 0 && live); other[2] = 7;
    close_result = EACCDN;
    CHECK(appl_exit() == 0 && live && gl_apid == 7);
    close_result = 0;
    CHECK(mt_appl_exit(other) == 1 && !live && closes == 1 && other[2] == -1);
    CHECK(gl_apid == -1 && aes_global[2] == -1 && gl_ap_version == 0);
    CHECK(mt_appl_yield(other) == 0 && appl_exit() == 0);
    CHECK(appl_init() == 7 && live);
    live = FALSE; /* Authoritative kernel teardown invalidates stale library state. */
    CHECK(appl_yield() == 0 && evnt_timer(0) == 0);
    CHECK(appl_init() == 7 && appl_exit() == 1 && closes == 2);
    return 0;
}
