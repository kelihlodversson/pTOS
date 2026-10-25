/* Exercise the production scalar CUIS service and allocation failures. */
#include "../../aes/cuis_session.c"

static PD first, second, third, fourth, fifth;
PD *run = &first;
static union {
    CUIS_CLIENT client;
    CUIS_WINDOW window;
    CUIS_RECT clips[CUIS_CLIP_LIMIT];
} pool[1024];
static ULONG allocations, frees;
static BOOL fail_allocation;
static ULONG fail_at_allocation;
static LONG copied[5];
static PD *input_message_sender;
static LONG input_message_destination;
static BOOL fail_copy;
static UQUAD fail_copy_address;
static BOOL input_irqs = TRUE, input_bad_idle;
static ULONG input_ticks, input_sleeps, input_inject_after;
static ULONG delay_tick_step = 1, delay_yields;
static BOOL input_drop_focus, input_drop_on_validate;
UQUAD cuis_input_lock(void)
{
    UQUAD flags = input_irqs ? 0x200 : 0;
    input_irqs = FALSE;
    return flags;
}
void cuis_input_unlock(UQUAD flags) { input_irqs = (flags & 0x200) != 0; }
ULONG cuis_input_ticks(void) { return input_ticks; }
static BOOL input_pointer_source;
static LONG input_pointer_x, input_pointer_y, input_pointer_max_x = 1023, input_pointer_max_y = 767;
static ULONG input_pointer_buttons, legacy_pointer_calls;
BOOL cuis_input_standard_pointer(void) { return input_pointer_source; }
BOOL cuis_input_pointer_state(LONG *x, LONG *y, ULONG *buttons)
{
    if (!input_pointer_source) return FALSE;
    *x = input_pointer_x; *y = input_pointer_y; *buttons = input_pointer_buttons;
    return TRUE;
}
void cuis_input_clip_pointer(LONG *x, LONG *y)
{
    *x = *x < 0 ? 0 : *x > input_pointer_max_x ? input_pointer_max_x : *x;
    *y = *y < 0 ? 0 : *y > input_pointer_max_y ? input_pointer_max_y : *y;
}
void cuis_input_call_pointer(SBYTE *packet) { (void)packet; legacy_pointer_calls++; }

void cuis_input_test_wait(ULONG events) { (void)events; }
BOOL ksched_block(BOOL (*ready)(void *), void *argument)
{
    (void)ready; (void)argument;
    return FALSE;
}
LONG ksched_yield(void) { delay_yields++; return E_OK; }
void cuis_input_idle(void)
{
    if (input_irqs) input_bad_idle = TRUE;
    input_irqs = TRUE;
    input_ticks += delay_tick_step;
    input_sleeps++;
    if (input_inject_after && input_sleeps == input_inject_after)
        cuis_keyboard_event(0xff123456UL);
    if (input_message_sender) {
        PD *saved = run;
        run = input_message_sender;
        input_message_sender = NULL;
        cuis_message_service(CUIS_MESSAGE_SEND, input_message_destination, 11, 22, 33, 44, 0);
        run = saved;
    }
    if (input_drop_focus) { input_drop_focus = FALSE; set_focus(NULL); }
    input_irqs = FALSE;
}
static UWORD display_width = 1024, display_height = 768;
void screen_get_current_mode_desc(SCREEN_MODE_DESC *mode)
{
    mode->width = display_width;
    mode->height = display_height;
}
BOOL kproc_validate_user_write(UQUAD address, ULONG size)
{
    if (input_drop_on_validate) {
        input_drop_on_validate = FALSE;
        set_focus(NULL);
    }
    return size == sizeof(LONG) && address >= 0x1000 && address <= 0x1010 &&
           !(address & 3);
}
BOOL kproc_copy_to_user(UQUAD address, const void *source, ULONG size)
{
    if (fail_copy || address == fail_copy_address || !kproc_validate_user_write(address, size)) return FALSE;
    copied[(address - 0x1000) / 4] = *(const LONG *)source;
    return TRUE;
}

void *kalloc(ULONG bytes)
{
    if ((bytes != sizeof(CUIS_CLIENT) && bytes != sizeof(CUIS_WINDOW) &&
         bytes != CUIS_CLIP_LIMIT * sizeof(CUIS_RECT)) ||
        fail_allocation || (fail_at_allocation && allocations == fail_at_allocation) ||
        allocations == 1024)
        return NULL;
    return &pool[allocations++];
}
void kfree(void *memory) { (void)memory; frees++; }
WORD kproc_pid(PD *pd) { return pd == &first ? 1 : 2; }
#define CHECK(condition) do { if (!(condition)) { __builtin_printf("%s:%d\n", __FILE__, __LINE__); return __LINE__; } } while (0)
#include "windows.c"
#include "visibility.c"
#include "regions.c"
#include "focus.c"
#include "input.c"
#include "pointer.c"
#include "messages.c"
#include "delay.c"
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
    if (rc) return rc;
    rc = region_checks();
    if (rc) return rc;
    rc = focus_checks();
    if (rc) return rc;
    rc = input_checks();
    if (rc) return rc;
    rc = pointer_checks();
    if (rc) return rc;
    rc = window_limit_checks();
    if (rc) return rc;
    rc = message_checks();
    return rc ? rc : delay_checks();
}
