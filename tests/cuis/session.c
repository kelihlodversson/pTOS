/* Exercise the production scalar CUIS service and allocation failures. */
#include "emutos.h"
#include "bdosdefs.h"
long xexec_directory(char *path, char *tail, char *environment, ULONG session, ULONG token, BOOL deferred, char *directory);
long xexec_observed(char *path, char *tail, char *environment, ULONG session, ULONG token);
long xexec_deferred(char *path, char *tail, char *environment, ULONG session);
/* Exercise the production cleanup-to-checkpoint call, with scheduler/THREAD
 * termination mocked below so the host can inspect the post-cleanup state. */
#undef CONF_WITH_PTERMGRP
#define CONF_WITH_PTERMGRP 1
#include "../../aes/cuis_session.c"

static MFORM displayed_pointer;
static BOOL displayed_hidden, displayed_style;
void vdi_cuis_pointer(const MFORM *form, BOOL hidden)
{
    displayed_style = form != NULL; displayed_hidden = hidden;
    if (form) displayed_pointer = *form;
}
static BOOL text_console_active;
void vdi_cuis_console(BOOL active) { text_console_active = active; }
static PD first, second, third, fourth, fifth;
PD *run = &first;
static union {
    CUIS_CLIENT client;
    CUIS_WINDOW window;
    CUIS_RECT clips[CUIS_CLIP_LIMIT];
    UBYTE packet[sizeof(CUIS_PACKET) + CUIS_STREAM_MAX];
} pool[1024];
static UBYTE stream_rings[8][CUIS_STREAM_BYTES * (1 + sizeof(UWORD))] __attribute__((aligned(16)));
static ULONG stream_ring_count;
/* Stream lifetime cases use real allocation/free so ASan observes retirement
 * UAFs, while the older broad fixtures retain their deterministic pool. */
extern void *malloc(__SIZE_TYPE__ size);
extern void free(void *memory);
static BOOL stream_heap_mode, stream_heap_bad, stream_resident_copy;
static struct { void *address; BOOL live; } stream_heap[1024];
static ULONG stream_heap_count;
static void (*stream_operation_hook)(int phase); /* 0 preflight, 1 copyin, 2 copyout */
static void stream_hook(int phase)
{
    if (stream_operation_hook) {
        void (*hook)(int)=stream_operation_hook; stream_operation_hook=NULL; hook(phase);
    }
}
static ULONG allocations, frees;
static void *reuse_allocation;
static BOOL fail_allocation;
static ULONG fail_at_allocation;
static LONG copied[5];
static BOOL fail_copy;
static UQUAD fail_copy_address;
static BOOL input_irqs = TRUE, input_bad_idle;
static ULONG input_ticks, input_sleeps, input_inject_after;
static ULONG delay_tick_step = 1, delay_yields;
static UBYTE stream_input[32768], stream_output[32768];
static BOOL stream_kill_peer, stream_drain_peer, stream_send_peer, stream_yield_peer;
static ULONG stream_change_after;
static PD *stream_peer;
static LONG stream_peer_handle;
static ULONG input_packet_mode;
static BOOL input_drop_focus;
static BOOL region_exit_on_output, notice_withdraw_on_copy;
static void context_copy_hook(void);
static void context_read_hook(void);
static void child_copy_hook(void);
static void child_idle_hook(void);
static void cohort_idle_hook(void);
static PD *mouse_release_owner;
static ULONG mouse_release_steps;
static BOOL mouse_release_exit;
static PD *display_release_owner;
static BOOL display_release_exit;
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
static ULONG input_modifiers;
void cuis_input_snapshot(LONG *x, LONG *y, ULONG *buttons, ULONG *modifiers)
{
    *x = input_pointer_x; *y = input_pointer_y;
    *buttons = input_pointer_buttons; *modifiers = input_modifiers;
}
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
static void (*input_operation_hook)(void);
BOOL ksched_block(BOOL (*ready)(void *), void *argument)
{
    (void)ready; (void)argument;
    if (input_irqs) input_bad_idle = TRUE;
    if (input_operation_hook) {
        void (*hook)(void)=input_operation_hook; input_operation_hook=NULL; hook();
        return TRUE;
    }
    return FALSE;
}
static BOOL cancel_pending;
static CUIS_CLIENT *cancellation_owner;
static BOOL cancellation_bad;
static ULONG cancellation_visits;
void ksched_cancel_point(void)
{
    cancellation_visits++;
    if (!input_irqs) cancellation_bad=TRUE;
    if (cancellation_owner && (cancellation_owner->input_wait || cancellation_owner->wait_operation ||
        cancellation_owner->resume_operation || cancellation_owner->references!=1)) cancellation_bad=TRUE;
}
static int cancel_on_wait;
static BOOL cancel_after_wake;
static void (*lifetime_wait_hook)(void);
BOOL ksched_cancellation_pending(void) { return cancel_pending; }
KSCHED_WAIT_RESULT ksched_wait_cancelable(BOOL (*ready)(void *), void *argument)
{
    if (lifetime_wait_hook) {
        void (*hook)(void)=lifetime_wait_hook; lifetime_wait_hook=NULL; hook();
        return KSCHED_WAIT_READY;
    }
    if (cancel_on_wait) { cancel_on_wait--; return KSCHED_WAIT_CANCELLED; }
    if (cancel_after_wake) { cancel_after_wake=FALSE; cancel_pending=TRUE; return KSCHED_WAIT_READY; }
    return ksched_block(ready, argument) ? KSCHED_WAIT_READY : KSCHED_WAIT_UNMANAGED;
}
LONG ksched_yield(void)
{
    delay_yields++;
    if (stream_yield_peer) {
        PD *saved = run;
        stream_yield_peer = FALSE; run = stream_peer;
        cuis_stream_service(CUIS_STREAM_WRITE, stream_peer_handle, 0x10000, 16, 0, 0);
        run = saved;
    }
    return E_OK;
}
void cuis_input_idle(void)
{
    if (input_irqs) input_bad_idle = TRUE;
    input_irqs = TRUE;
    input_ticks += delay_tick_step;
    input_sleeps++;
    if (stream_change_after && input_sleeps == stream_change_after) {
        PD *saved = run;
        if (stream_kill_peer) cuis_process_exit(stream_peer);
        if (stream_drain_peer) {
            stream_input[0] = 99; /* Modify the sender's input while its write is blocked. */
            run = stream_peer;
            cuis_stream_service(CUIS_STREAM_READ, stream_peer_handle, 0x20000, 16, 0, 0);
            run = saved;
        }
        if (stream_send_peer) {
            run = stream_peer;
            cuis_stream_service(CUIS_STREAM_WRITE, stream_peer_handle, 0x10000, 16, 0, 0);
            run = saved;
        }
        stream_change_after = 0;
    }
    if (input_packet_mode) {
        if (input_packet_mode == 9) {
            if (input_sleeps == 1)
                cuis_pointer_packet((WORD)(40-pointer_x),(WORD)(60-pointer_y),1,NULL,NULL);
            if (input_sleeps == 2) {
                cuis_pointer_packet(0,0,0,NULL,NULL);
                cuis_pointer_packet((WORD)(5-pointer_x),(WORD)(5-pointer_y),0,NULL,NULL);
                input_packet_mode = 0;
            }
        } else if (input_packet_mode == 8) {
            cuis_pointer_packet((WORD)(210 - pointer_x), (WORD)(210 - pointer_y), 0, NULL, NULL);
            input_packet_mode = 0;
        } else if (input_packet_mode == 5 || input_packet_mode == 6) {
            cuis_pointer_packet((WORD)(10 - pointer_x), (WORD)(10 - pointer_y), 1, NULL, NULL);
            cuis_pointer_packet(0, 0, 0, NULL, NULL);
            if (input_packet_mode == 6) input_ticks += 100;
            cuis_pointer_packet(10, 0, 1, NULL, NULL);
            cuis_pointer_packet(0, 0, 0, NULL, NULL);
            input_packet_mode = 0;
        } else if (input_packet_mode <= 2 || input_packet_mode == 4) {
            if (input_sleeps == 1 || ((input_packet_mode == 1 || input_packet_mode == 4) && input_sleeps == 3))
                cuis_pointer_packet((WORD)(10 - pointer_x), (WORD)(10 - pointer_y), 1, NULL, NULL);
            if (input_sleeps == 2 || (input_packet_mode == 1 && input_sleeps == 4)) {
                cuis_pointer_packet(0, 0, 0, NULL, NULL);
                if (input_packet_mode == 2 || input_sleeps == 4) input_packet_mode = 0;
            }
            if (input_packet_mode == 4 && input_sleeps == 4) input_packet_mode = 0;
        } else {
            cuis_pointer_packet((WORD)(110 - pointer_x), (WORD)(110 - pointer_y), 0, NULL, NULL);
            cuis_pointer_packet((WORD)(10 - pointer_x), (WORD)(10 - pointer_y), 0, NULL, NULL);
            input_packet_mode = 0;
        }
    }
    if (input_inject_after && input_sleeps == input_inject_after)
        cuis_keyboard_event(0xff123456UL);
    if (mouse_release_owner) {
        PD *saved = run;
        run = mouse_release_owner;
        if (mouse_release_exit) {
            mouse_release_exit = FALSE; mouse_release_owner = NULL;
            cuis_process_exit(run);
        } else {
            cuis_mouse_lock((*find_owner(run))->handle, 0, 0);
            if (!--mouse_release_steps) mouse_release_owner = NULL;
        }
        run = saved;
    }
    if (display_release_owner) {
        PD *saved = run;
        run = display_release_owner;
        display_release_owner = NULL;
        if (display_release_exit) { display_release_exit = FALSE; cuis_process_exit(run); }
        else cuis_display_lock((*find_owner(run))->handle, 0, 0);
        run = saved;
    }
    child_idle_hook();
    cohort_idle_hook();
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
    if (!stream_resident_copy) stream_hook(0);
    if (region_exit_on_output) {
        region_exit_on_output = FALSE;
        cuis_process_exit(run);
        return FALSE;
    }
    if (address >= 0x20000 && address <= 0x28000 && size <= 0x28000 - address) return TRUE;
    return size == sizeof(LONG) && address >= 0x1000 && address <= 0x1010 &&
           !(address & 3);
}
BOOL kproc_copy_to_user(UQUAD address, const void *source, ULONG size)
{
    if (fail_copy || address == fail_copy_address || !kproc_validate_user_write(address, size)) return FALSE;
    context_read_hook();
    if (address >= 0x20000) {
        memcpy(stream_output + (address - 0x20000), source, size); return TRUE;
    }
    copied[(address - 0x1000) / 4] = *(const LONG *)source;
    return TRUE;
}
BOOL kproc_copy_from_user(void *destination, UQUAD address, ULONG size)
{
    if (fail_copy || address < 0x10000 || address > 0x18000 || size > 0x18000 - address) return FALSE;
    if (notice_withdraw_on_copy) {
        CUIS_WINDOW *window;
        UQUAD flags;
        notice_withdraw_on_copy = FALSE;
        if (!input_irqs) input_bad_idle = TRUE;
        flags = cuis_input_lock();
        for (window = (*find_owner(run))->windows; window; window = window->next) window->notices = 0;
        cuis_input_unlock(flags);
    }
    context_copy_hook();
    child_copy_hook();
    memcpy(destination, stream_input + (address - 0x10000), size); return TRUE;
}
BOOL kproc_validate_user_range(UQUAD address, ULONG size)
{
    stream_hook(0);
    return !fail_copy && address >= 0x10000 && address <= 0x18000 && size <= 0x18000-address;
}
BOOL kproc_copy_from_user_resident(void *destination, UQUAD address, ULONG size)
{
    BOOL result;
    stream_hook(1); stream_resident_copy=TRUE;
    result=kproc_copy_from_user(destination,address,size);
    stream_resident_copy=FALSE; return result;
}
BOOL kproc_copy_to_user_resident(UQUAD address, const void *source, ULONG size)
{
    BOOL result;
    stream_hook(2); stream_resident_copy=TRUE;
    result=kproc_copy_to_user(address,source,size);
    stream_resident_copy=FALSE; return result;
}

void *kalloc(ULONG bytes)
{
    if (stream_heap_mode) {
        void *p;
        if (!bytes || fail_allocation || (fail_at_allocation && allocations==fail_at_allocation) ||
            stream_heap_count==1024) return NULL;
        p=malloc(bytes);
        if (!p) return NULL;
        memset(p,0xa5,bytes);
        stream_heap[stream_heap_count].address=p; stream_heap[stream_heap_count++].live=TRUE;
        allocations++; return p;
    }
    if (bytes == sizeof(stream_rings[0])) {
        if (fail_allocation || (fail_at_allocation && allocations == fail_at_allocation) || stream_ring_count == 8) return NULL;
        allocations++; return stream_rings[stream_ring_count++];
    }
    if (!bytes || bytes > sizeof(pool[0]) ||
        fail_allocation || (fail_at_allocation && allocations == fail_at_allocation) ||
        allocations == 1024)
        return NULL;
    if (reuse_allocation) {
        void *result=reuse_allocation; reuse_allocation=NULL; allocations++;
        memset(result,0xa5,bytes); return result;
    }
    if (bytes == sizeof(CUIS_CLIENT)) memset(&pool[allocations], 0xa5, bytes);
    return &pool[allocations++];
}
void kfree(void *memory)
{
    if (stream_heap_mode) {
        ULONG i=stream_heap_count;
        while (i) if (stream_heap[--i].address==memory && stream_heap[i].live) {
            stream_heap[i].live=FALSE; free(memory); frees++; return;
        }
        stream_heap_bad=TRUE; return;
    }
    (void)memory; frees++;
}
static ULONG process_references;
KPROC *kproc_retain(PD *pd)
{
    if (!pd) return NULL;
    process_references++; return (KPROC *)pd;
}
void kproc_release(KPROC *process)
{
    if (!process || !process_references) __builtin_abort();
    process_references--;
}
UQUAD kproc_ref_number(const KPROC *process)
{
    return process == (KPROC *)&first ? 0x100000001ULL : 0x200000001ULL;
}
WORD kproc_pid(PD *pd) { return pd == &first ? 1 : 2; }
#define CHECK(condition) do { if (!(condition)) { __builtin_printf("%s:%d\n", __FILE__, __LINE__); return __LINE__; } } while (0)
#include "windows.c"
#include "visibility.c"
#include "regions.c"
#include "focus.c"
#include "input.c"
#include "pointer.c"
#include "delay.c"
#include "streams.c"
#include "stream_lifetime.c"
#include "combined.c"
#include "display.c"
#include "notices.c"
#include "input_events.c"
#include "mouse_control.c"
#include "surfaces.c"
#include "controls.c"
#include "pointer_style.c"
#include "window_at.c"
#include "background.c"
#include "contexts.c"
#include "child_completion.c"
#include "entries.c"
#include "peers.c"
#include "client_identity.c"
#include "cohorts.c"
#include "lifetime.c"
#include "input_lifetime.c"
#include "input_history.c"
#include "resume_transaction.c"
#include "input_transaction.c"

static int cancellation_adapter_checks(void)
{
    LONG a, b;
    run=&first; a=cuis_service(CUIS_SESSION_START,0);
    run=&second; b=cuis_service(CUIS_SESSION_START,0);
    CHECK(a>=0 && b>=0 && input_irqs);
    cancel_pending=TRUE;
    CHECK(cuis_delay_service(b,1000)==EACCDN && input_irqs);
    CHECK(cuis_display_lock(b,1,1)==EACCDN && !display_owner && input_irqs);
    CHECK(cuis_mouse_lock(b,1,1)==EACCDN && !mouse_owner && input_irqs);
    cancel_pending=FALSE; cancel_after_wake=TRUE;
    CHECK(cuis_delay_service(b,1000)==EACCDN && input_irqs && !cancel_after_wake);
    cancel_pending=FALSE;
    run=&first; CHECK(cuis_display_lock(a,1,0)==0);
    run=&second; cancel_on_wait=1;
    CHECK(cuis_display_lock(b,1,1)==EACCDN && input_irqs && display_owner->owner==&first);
    cancel_on_wait=1;
    CHECK(cuis_delay_service(b,1000)==EACCDN && input_irqs);
    run=&first; CHECK(cuis_display_lock(a,0,0)==0);
    CHECK(cuis_mouse_lock(a,1,0)==0);
    run=&second; cancel_on_wait=1;
    CHECK(cuis_mouse_lock(b,1,1)==EACCDN && input_irqs && mouse_owner->owner==&first);
    run=&first; CHECK(cuis_mouse_lock(a,0,0)==0);
    cuis_process_exit(&first); cuis_process_exit(&second);
    CHECK(allocations==frees && !cancel_on_wait && !input_bad_idle);
    next_handle=0; allocations=frees=0;
    return 0;
}

int main(void)
{
    LONG owner, other, reopened;
    int rc;
    rc=stream_lifetime_checks();
    if (rc) return rc;
    rc=input_transaction_checks();
    if (rc) return rc;
    CHECK(!cancellation_bad);
    rc=resume_transaction_checks();
    if (rc) return rc;
    CHECK(!cancellation_bad);
    rc=input_history_checks();
    if (rc) return rc;
    rc=input_lifetime_checks();
    if (rc) return rc;
    rc=lifetime_checks();
    if (rc) return rc;
    rc=cancellation_adapter_checks();
    if (rc) return rc;
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
    rc = delay_checks();
    if (rc) return rc;
    rc = stream_checks();
    if (rc) return rc;
    rc = combined_checks();
    if (rc) return rc;
    rc = display_checks();
    if (rc) return rc;
    rc = notice_checks();
    if (rc) return rc;
    rc = input_event_checks();
    if (rc) return rc;
    rc = input_capture_checks();
    if (rc) return rc;
    rc = mouse_control_checks();
    if (rc) return rc;
    rc = fresh_mouse_checks();
    if (rc) return rc;
    rc = surface_checks();
    if (rc) return rc;
    rc = surface_cleanup_checks();
    if (rc) return rc;
    rc = control_checks();
    if (rc) return rc;
    rc = pointer_style_checks();
    if (rc) return rc;
    rc = window_at_checks();
    if (rc) return rc;
    rc = background_checks();
    if (rc) return rc;
    rc = background_keyboard_checks();
    if (rc) return rc;
    rc = background_release_checks();
    if (rc) return rc;
    rc = context_checks();
    if (rc) return rc;
    rc = child_completion_checks();
    if (rc) return rc;
    rc = button_resume_checks();
    if (rc) return rc;
    rc = child_launch_checks();
    if (rc) return rc;
    rc = console_launch_checks();
    if (rc) return rc;
    rc = console_abandoned_launch_checks();
    if (rc) return rc;
    rc = entry_checks();
    if (rc) return rc;
    rc = client_identity_checks();
    if (rc) return rc;
    rc = peer_checks();
    if (rc) return rc;
    rc = cohort_checks();
    if (rc) return rc;
    rc = cohort_click_checks();
    if (rc) return rc;
    rc = cohort_origin_checks();
    if (rc) return rc;
    rc = cohort_result_checks();
    return rc ? rc : process_defer_checks();
}
