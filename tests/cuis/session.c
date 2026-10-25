/* Exercise the production scalar CUIS service and allocation failures. */
#include "../../aes/cuis_session.c"

static PD first, second;
PD *run = &first;
static union {
    CUIS_CLIENT client;
    CUIS_WINDOW window;
    CUIS_RECT clips[CUIS_CLIP_LIMIT];
} pool[256];
static ULONG allocations, frees;
static BOOL fail_allocation;
static ULONG fail_at_allocation;
static LONG copied[4];
static BOOL fail_copy;
static UWORD display_width = 1024, display_height = 768;
void screen_get_current_mode_desc(SCREEN_MODE_DESC *mode)
{
    mode->width = display_width;
    mode->height = display_height;
}
BOOL kproc_validate_user_write(UQUAD address, ULONG size)
{
    return size == sizeof(LONG) && address >= 0x1000 && address <= 0x100c &&
           !(address & 3);
}
BOOL kproc_copy_to_user(UQUAD address, const void *source, ULONG size)
{
    if (fail_copy || !kproc_validate_user_write(address, size)) return FALSE;
    copied[(address - 0x1000) / 4] = *(const LONG *)source;
    return TRUE;
}

void *kalloc(ULONG bytes)
{
    if ((bytes != sizeof(CUIS_CLIENT) && bytes != sizeof(CUIS_WINDOW) &&
         bytes != CUIS_CLIP_LIMIT * sizeof(CUIS_RECT)) ||
        fail_allocation || (fail_at_allocation && allocations == fail_at_allocation) ||
        allocations == 256)
        return NULL;
    return &pool[allocations++];
}
void kfree(void *memory) { (void)memory; frees++; }
WORD kproc_pid(PD *pd) { return pd == &first ? 1 : 2; }
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
#include "windows.c"
#include "visibility.c"
#include "regions.c"
int main(void)
{
    LONG owner, other, reopened;
    int rc;
    CHECK(cuis_service(99, ~0ULL) == EINVFN);
    CHECK(cuis_service(CUIS_SESSION_QUERY, 0) == EACCDN);
    CHECK(cuis_service(CUIS_SESSION_END, 0) == EACCDN);
    fail_allocation = TRUE;
    CHECK(cuis_service(CUIS_SESSION_START, 0) == ENSMEM);
    CHECK(!allocations && !cuis_session_count());
    fail_allocation = FALSE;
    owner = cuis_service(CUIS_SESSION_START, ~0ULL);
    CHECK(owner == 0 && cuis_session_count() == 1);
    CHECK(cuis_service(CUIS_SESSION_START, 0) == owner && allocations == 1);
    CHECK(cuis_service(CUIS_SESSION_QUERY, 0x100000000ULL | (ULONG)owner) == EINVAL);
    CHECK(cuis_service(CUIS_SESSION_END, 0x100000000ULL | (ULONG)owner) == EINVAL);
    CHECK(cuis_service(CUIS_SESSION_QUERY, 0x80000000ULL) == EINVAL);
    CHECK(cuis_service(CUIS_SESSION_END, ~0ULL) == EINVAL);
    CHECK(cuis_service(CUIS_SESSION_QUERY, owner) == E_OK && cuis_session_count() == 1);
    run = &second;
    CHECK(cuis_service(CUIS_SESSION_QUERY, owner) == EACCDN);
    CHECK(cuis_service(CUIS_SESSION_END, owner) == EACCDN);
    other = cuis_service(CUIS_SESSION_START, 0);
    CHECK(other > owner && cuis_session_count() == 2);
    run = &first;
    CHECK(cuis_service(CUIS_SESSION_END, other) == EACCDN);
    CHECK(cuis_service(CUIS_SESSION_QUERY, owner) == E_OK);
    CHECK(cuis_service(CUIS_SESSION_END, owner) == E_OK && cuis_session_count() == 1);
    CHECK(cuis_service(CUIS_SESSION_END, owner) == EACCDN);
    reopened = cuis_service(CUIS_SESSION_START, 0);
    CHECK(reopened > other && cuis_service(CUIS_SESSION_QUERY, owner) == EACCDN);
    cuis_process_exit(&first); cuis_process_exit(&first);
    CHECK(cuis_session_count() == 1);
    run = &second;
    CHECK(cuis_service(CUIS_SESSION_QUERY, other) == E_OK);
    cuis_process_exit(&second);
    CHECK(!cuis_session_count() && allocations == frees);
    run = &first;
    next_handle = 0x7fffffffUL;
    CHECK(cuis_service(CUIS_SESSION_START, 0) == 0x7fffffffL);
    /* An existing session can still be returned after handle exhaustion. */
    CHECK(cuis_service(CUIS_SESSION_START, ~0ULL) == 0x7fffffffL);
    cuis_process_exit(&first);
    CHECK(cuis_service(CUIS_SESSION_START, 0) == ENSMEM && !cuis_session_count());
    CHECK(allocations == frees);
    rc = window_checks();
    if (rc) return rc;
    rc = visibility_checks();
    return rc ? rc : region_checks();
}
