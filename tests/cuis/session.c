/* Exercise the production CUIS service with copy/allocation failures. */
#include "../../aes/cuis_session.c"

static PD first, second;
PD *run = &first;
static CUIS_SESSION_REQUEST input;
static CUIS_SESSION_INFO output;
static CUIS_CLIENT pool[16];
static ULONG allocations, frees;
static BOOL fail_allocation, fail_copy;

void *kalloc(ULONG bytes)
{
    if (bytes != sizeof(CUIS_CLIENT) || fail_allocation || allocations == 16)
        return NULL;
    return &pool[allocations++];
}
void kfree(void *memory) { (void)memory; frees++; }
WORD kproc_pid(PD *pd) { return pd == &first ? 1 : 2; }
BOOL kproc_copy_from_user(void *dst, UQUAD address, ULONG size)
{
    if (address != (uintptr_t)&input || size != sizeof(input)) return FALSE;
    __builtin_memcpy(dst, &input, size);
    return TRUE;
}
BOOL kproc_validate_user_write(UQUAD address, ULONG size)
{
    return address == (uintptr_t)&output && size == sizeof(output);
}
BOOL kproc_copy_to_user(UQUAD address, const void *src, ULONG size)
{
    if (fail_copy || !kproc_validate_user_write(address, size)) return FALSE;
    __builtin_memcpy(&output, src, size);
    return TRUE;
}
static LONG call(ULONG operation, ULONG handle)
{
    input.operation = operation;
    input.handle = handle;
    return cuis_service(CUIS_SESSION, (uintptr_t)&input, sizeof(input),
                           (uintptr_t)&output, sizeof(output));
}
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
int main(void)
{
    ULONG owner, other, reopened, i;
    input.size = sizeof(input); input.version = CUIS_VERSION;
    CHECK(cuis_service(99, 0, 0, 0, 0) == EINVFN);
    CHECK(cuis_service(0, 0, sizeof(input), 0, 0) == EIMBA);
    CHECK(cuis_service(0, (uintptr_t)&input, 0x100000020ULL, 0, 0) == EINVAL);
    input.version++;
    CHECK(call(CUIS_SESSION_ATTACH, 0) == EOPNOTSUPP);
    input.version--;
    input.flags = 1;
    CHECK(call(CUIS_SESSION_ATTACH, 0) == EINVAL);
    input.flags = 0;
    for (i = 0; i < 3; i++) {
        input.reserved[i] = 1;
        CHECK(call(CUIS_SESSION_ATTACH, 0) == EINVAL);
        input.reserved[i] = 0;
    }
    input.size--;
    CHECK(call(CUIS_SESSION_ATTACH, 0) == EINVAL);
    input.size++;
    CHECK(call(99, 0) == EINVFN);
    CHECK(call(CUIS_SESSION_ATTACH, 1) == EINVAL);
    CHECK(cuis_service(0, (uintptr_t)&input, sizeof(input), 0, sizeof(output)) == EINVAL);
    input.handle = 0;
    CHECK(cuis_service(0, (uintptr_t)&input, sizeof(input), 0, sizeof(output)) == EIMBA);
    CHECK(!allocations && !cuis_session_count());
    fail_allocation = TRUE;
    CHECK(call(CUIS_SESSION_ATTACH, 0) == ENSMEM);
    fail_allocation = FALSE; fail_copy = TRUE;
    CHECK(call(CUIS_SESSION_ATTACH, 0) == EIMBA);
    CHECK(allocations == 1 && frees == 1 && !cuis_session_count());
    fail_copy = FALSE;
    CHECK(call(CUIS_SESSION_ATTACH, 0) == E_OK);
    owner = output.handle;
    CHECK(owner && output.size == sizeof(output) && output.version == CUIS_VERSION);
    CHECK(output.features == CUIS_FEATURE_SESSION);
    for (i = 0; i < 4; i++) CHECK(!output.reserved[i]);
    CHECK(call(CUIS_SESSION_ATTACH, 0) == E_OK && output.handle == owner);
    CHECK(allocations == 2 && cuis_session_count() == 1);
    fail_copy = TRUE;
    CHECK(call(CUIS_SESSION_QUERY, owner) == EIMBA && cuis_session_count() == 1);
    fail_copy = FALSE;
    run = &second;
    CHECK(call(CUIS_SESSION_QUERY, owner) == EACCDN);
    CHECK(call(CUIS_SESSION_DETACH, owner) == EACCDN);
    CHECK(call(CUIS_SESSION_ATTACH, 0) == E_OK);
    other = output.handle;
    CHECK(other != owner && cuis_session_count() == 2);
    run = &first;
    CHECK(call(CUIS_SESSION_DETACH, other) == EACCDN);
    CHECK(call(CUIS_SESSION_QUERY, owner) == E_OK && output.handle == owner);
    CHECK(call(CUIS_SESSION_DETACH, owner) == E_OK && cuis_session_count() == 1);
    CHECK(call(CUIS_SESSION_ATTACH, 0) == E_OK);
    reopened = output.handle;
    CHECK(reopened != owner && call(CUIS_SESSION_QUERY, owner) == EACCDN);
    cuis_process_exit(&first); cuis_process_exit(&first);
    CHECK(cuis_session_count() == 1);
    cuis_process_exit(&second);
    CHECK(!cuis_session_count() && allocations == frees);
    next_handle = 0xffffffffUL;
    CHECK(call(CUIS_SESSION_ATTACH, 0) == E_OK && output.handle == 0xffffffffUL);
    cuis_process_exit(&first);
    CHECK(call(CUIS_SESSION_ATTACH, 0) == ENSMEM && !cuis_session_count());
    return 0;
}
