/*
 * aes_service.c - kernel-owned coordination for native x32 AES clients
 *
 * Copyright (C) 2026 The pTOS development team.
 * This file is distributed under the GPL, version 2 or any later version.
 */
#include "emutos.h"
#include "aes_service.h"
#include "gemerror.h"
#include "kheap.h"
#include "string.h"
#include "tosvars.h"
#if CONF_WITH_AES
#include "aes_accessory.h"
#include "aes_window.h"
#include "aes_objects_kernel.h"
#endif
#include "../../../bdos/kproc.h"

extern PD *run;

struct aes_client_state {
    AES_CLIENT_STATE *next;
    PD *owner;
    WORD id;
    char name[AES_NAME_MAX + 1];
    UWORD head, count;
    AES_MESSAGE queue[AES_MESSAGE_QUEUE];
};

static AES_CLIENT_STATE *clients;
static struct {
    AES_CLIENT_STATE *owner;
    UWORD depth;
} update[2];

static AES_CLIENT_STATE *find_id(WORD id)
{
    AES_CLIENT_STATE *p;

    for (p = clients; p; p = p->next)
        if (p->id == id)
            return p;
    return NULL;
}

static AES_CLIENT_STATE *find_name(const char *name)
{
    AES_CLIENT_STATE *p;

    for (p = clients; p; p = p->next)
        if (!strcmp(p->name, name))
            return p;
    return NULL;
}

/* Copy only through the terminator, bounded to the classic 8-byte name.
 * All bytes after the terminator in the trusted copy stay zero. */
static LONG copy_name(UQUAD address, char name[AES_NAME_MAX + 1])
{
    ULONG i;
    char c;

    memset(name, 0, AES_NAME_MAX + 1);
    for (i = 0; i <= AES_NAME_MAX; i++) {
        if (address > 0xffffffffULL - i ||
            !kproc_copy_from_user(&c, address + i, 1))
            return EIMBA;
        if (!c)
            return i ? E_OK : EINVAL;
        if (i == AES_NAME_MAX)
            return EINVAL;
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.'))
            return EINVAL;
        name[i] = c;
    }
    return EINVAL;
}

void aes_service_process_exit(PD *pd)
{
    AES_CLIENT_STATE *p = kproc_aes_get(pd);
    AES_CLIENT_STATE **link;
    ULONG i;

    if (!p)
        return;
#if CONF_WITH_AES
    x86_64_aes_registration(pd, FALSE);
    x86_64_aes_objects_exit(pd);
#endif
    for (i = 0; i < 2; i++)
        if (update[i].owner == p) {
            update[i].owner = NULL;
            update[i].depth = 0;
        }
    for (link = &clients; *link; link = &(*link)->next)
        if (*link == p) {
            *link = p->next;
            break;
        }
    kproc_aes_set(pd, NULL);
#if CONF_WITH_AES
    x86_64_aes_window_exit(pd);
#endif
    kfree(p);
}

static LONG register_client(UQUAD address, UQUAD output)
{
    char name[AES_NAME_MAX + 1];
    AES_APP_INFO info = { 0 };
    AES_CLIENT_STATE *p = kproc_aes_get(run);
    LONG rc;
    BOOL fresh = FALSE;

    rc = copy_name(address, name);
    if (rc)
        return rc;
    if ((output & 3) || !kproc_validate_user_write(output, sizeof(info)))
        return EIMBA;
    if (p) {
        if (strcmp(p->name, name))
            return EACCDN;
    } else {
        if (find_name(name))
            return EACCDN;
        p = kalloc(sizeof(*p));
        if (!p)
            return ENSMEM;
        memset(p, 0, sizeof(*p));
        p->owner = run;
        p->id = kproc_pid(run);
        memcpy(p->name, name, sizeof(name));
        if (p->id <= 0 || !kproc_aes_set(run, p)) {
            kfree(p);
            return EACCDN;
        }
        p->next = clients;
        clients = p;
        fresh = TRUE;
    }
    info.version = AES_CLIENT_VERSION;
    info.id = p->id;
    info.queue_capacity = AES_MESSAGE_QUEUE;
    info.features = AES_FEATURE_MESSAGE | AES_FEATURE_UPDATE;
#if CONF_WITH_AES
    if (x86_64_aes_associated(run)) {
        info.features |= AES_FEATURE_WAIT | AES_FEATURE_INPUT | AES_FEATURE_WINDOW | AES_FEATURE_OBJECT;
        x86_64_aes_registration(run, TRUE);
    }
#endif
    if (!kproc_copy_to_user(output, &info, sizeof(info))) {
        if (fresh)
            aes_service_process_exit(run);
        return EIMBA;
    }
    return E_OK;
}

static LONG send_message(AES_CLIENT_STATE *sender, UQUAD id, UQUAD input)
{
    AES_CLIENT_STATE *receiver;
    AES_MESSAGE message;

    if (!id || id > 32767)
        return EINVAL;
    if ((input & 1) || !kproc_copy_from_user(&message, input, sizeof(message)))
        return EIMBA;
    if (message.reserved || message.type < 0)
        return EINVAL;
    receiver = find_id((WORD)id);
    if (!receiver)
        return ESRCH;
    if (receiver->count == AES_MESSAGE_QUEUE)
        return EAGAIN;
    message.sender = sender->id;
    receiver->queue[(receiver->head + receiver->count) % AES_MESSAGE_QUEUE] = message;
    receiver->count++;
#if CONF_WITH_AES
    x86_64_aes_wake(receiver->owner);
#endif
    return E_OK;
}

static void flush_native_messages(AES_CLIENT_STATE *p)
{
#if CONF_WITH_AES
    AES_MESSAGE message;

    x86_64_aes_pending_message(p->owner);
    while (p->count < AES_MESSAGE_QUEUE && x86_64_aes_window_pending(p->owner, &message)) {
        p->queue[(p->head + p->count) % AES_MESSAGE_QUEUE] = message;
        p->count++;
    }
#else
    (void)p;
#endif
}

static LONG receive_message(AES_CLIENT_STATE *p, UQUAD output, BOOL poll)
{
    AES_EVENT event = { 0 };
    ULONG size = poll ? sizeof(event) : sizeof(AES_MESSAGE);

    if ((output & 1) || !kproc_validate_user_write(output, size))
        return EIMBA;
    flush_native_messages(p);
    if (p->count) {
        event.events = AES_EVENT_MESSAGE;
        event.message = p->queue[p->head];
    } else if (!poll)
        return EAGAIN;
    if (!kproc_copy_to_user(output, poll ? (const void *)&event : &event.message, size))
        return EIMBA;
    /* A rejected/faulting output must not consume the queued message. */
    if (event.events) {
#if CONF_WITH_AES
        if (event.message.type == 41) /* AC_CLOSE */
            x86_64_aes_flags(p->owner, 4); /* AP_ACCLOSE */
#endif
        p->head = (p->head + 1) % AES_MESSAGE_QUEUE;
        p->count--;
    }
    return poll ? event.events : E_OK;
}

static LONG update_ownership(AES_CLIENT_STATE *p, UQUAD code)
{
    ULONG index;

    if (code > AES_BEG_MCTRL)
        return EINVAL;
    index = (ULONG)code / 2;
    if (code & 1) {
        if (update[index].owner && update[index].owner != p)
            return EAGAIN;
        if (update[index].depth == 32767)
            return ERANGE;
        update[index].owner = p;
        update[index].depth++;
    } else {
        if (update[index].owner != p)
            return EACCDN;
        if (!--update[index].depth)
            update[index].owner = NULL;
    }
    return E_OK;
}

BOOL aes_service_window_unlocked(PD *pd)
{
    AES_CLIENT_STATE *p = kproc_aes_get(pd);

    return p && (!update[0].owner || update[0].owner == p) &&
        (!update[1].owner || update[1].owner == p);
}

#if CONF_WITH_X32_ACCESSORY_TEST
LONG aes_service_test_coordination(WORD code)
{
    AES_CLIENT_STATE *p = find_name("ACCWAIT");

    return p ? update_ownership(p, code) : ESRCH;
}
#endif

static BOOL valid_rectangle(const AES_EVENT_RECT *mouse)
{
    return (mouse->outside == 0 || mouse->outside == 1) &&
        mouse->rectangle.g_w > 0 && mouse->rectangle.g_h > 0 &&
        (LONG)mouse->rectangle.g_x + mouse->rectangle.g_w <= 32767L &&
        (LONG)mouse->rectangle.g_y + mouse->rectangle.g_h <= 32767L;
}

static LONG input_events(UQUAD input, UQUAD output)
{
    AES_EVENT_REQUEST request;
    AES_INPUT_EVENT event;
#if CONF_WITH_AES
    AES_CLIENT_STATE *p;
    LONG rc;
    ULONG started, timeout, elapsed;
#endif

    if ((input & 3) || !kproc_copy_from_user(&request, input, sizeof(request)))
        return EIMBA;
    if (!request.mask || (request.mask & ~AES_EVENT_ALL) ||
        (request.flags & ~AES_EVENT_POLL) || request.reserved ||
        request.timer_ms > 0x7fffffffUL ||
        ((request.mask & AES_EVENT_BUTTON) &&
         (!request.clicks || request.clicks > 255 ||
          !request.button_mask || request.button_mask > 3 ||
          (request.button_state & ~request.button_mask))) ||
        ((request.mask & AES_EVENT_MOUSE1) && !valid_rectangle(&request.mouse1)) ||
        ((request.mask & AES_EVENT_MOUSE2) && !valid_rectangle(&request.mouse2)))
        return EINVAL;
    if ((output & 1) || !kproc_validate_user_write(output, sizeof(event)))
        return EIMBA;
#if CONF_WITH_AES
    if (!x86_64_aes_owned(run))
        return EACCDN;
    timeout = request.timer_ms;
    started = (ULONG)hz_200;
#if CONF_WITH_X32_ACCESSORY_TEST
    /* Make the one-millisecond poll regression cross the clock-read
     * boundary below, without changing the clock or dispatching tasks. */
    if ((request.flags & AES_EVENT_POLL) &&
        (request.mask & AES_EVENT_TIMER) && timeout == 1)
        while ((ULONG)hz_200 == started)
            __asm__ volatile ("pause");
#endif
    do {
        p = kproc_aes_get(run);
        if (!p)
            return EACCDN;
        flush_native_messages(p);
        /* Only blocking waits have deadlines. A callback may drain a
         * message after waking this task; keep the original timeout when
         * retrying such a spurious wake. Polls ignore positive timers. */
        if (!(request.flags & AES_EVENT_POLL) &&
            (request.mask & AES_EVENT_TIMER) && timeout) {
            elapsed = (ULONG)hz_200 - started;
            request.timer_ms = elapsed >= (timeout + 4) / 5 ?
                0 : timeout - elapsed * 5;
        }
        memset(&event, 0, sizeof(event));
        rc = x86_64_aes_events(run, &request, &event,
                              (request.mask & AES_EVENT_MESSAGE) && p->count);
        if (rc)
            return rc;
        /* An intervening callback can revoke/recreate the registration. */
        p = kproc_aes_get(run);
        if (!p)
            return EACCDN;
        flush_native_messages(p);
        if ((request.mask & AES_EVENT_MESSAGE) && p->count) {
            event.events |= AES_EVENT_MESSAGE;
            event.message = p->queue[p->head];
        }
    } while (!event.events && !(request.flags & AES_EVENT_POLL));
    if (!kproc_copy_to_user(output, &event, sizeof(event)))
        return EIMBA;
    if (event.events & AES_EVENT_MESSAGE) {
        if (event.message.type == 41)
            x86_64_aes_flags(run, 4); /* AP_ACCLOSE */
        p->head = (p->head + 1) % AES_MESSAGE_QUEUE;
        p->count--;
    }
    return event.events;
#else
    return EACCDN;
#endif
}

static LONG window_service(UQUAD input, UQUAD output, BOOL *defer_return_work)
{
    AES_WINDOW_REQUEST r;
    AES_WINDOW_INFO info;
    GRECT rectangle = { 0 };
    char text[AES_WINDOW_TEXT_MAX + 1] = { 0 };
    ULONG size = 0, i;
    BOOL geometry_input;
    LONG result;

    if ((input & 3) || !kproc_copy_from_user(&r, input, sizeof(r)))
        return EIMBA;
    if (r.operation < AES_WIN_CREATE || r.operation > AES_WIN_FIND)
        return EINVFN;
    geometry_input = r.operation == AES_WIN_CREATE || r.operation == AES_WIN_OPEN ||
        r.operation == AES_WIN_RECT || r.operation == AES_WIN_CALC;
    if (r.reserved || (r.kind & ~0x0fff) ||
        (r.operation != AES_WIN_CREATE && r.operation != AES_WIN_CALC && r.kind) ||
        ((r.operation == AES_WIN_CREATE || r.operation == AES_WIN_CALC ||
          r.operation == AES_WIN_FIND) && r.handle) ||
        (r.operation != AES_WIN_SLIDER && r.value) ||
        (r.operation == AES_WIN_SLIDER &&
         (r.selector > AES_WINDOW_VSLSIZ || r.value > 1000 ||
          r.value < ((r.selector >= AES_WINDOW_HSLSIZ) ? -1 : 0))) ||
        ((r.operation == AES_WIN_TEXT || r.operation == AES_WIN_VISIBLE ||
          r.operation == AES_WIN_CALC) && r.selector > 1) ||
        (r.operation != AES_WIN_TEXT && r.operation != AES_WIN_VISIBLE &&
         r.operation != AES_WIN_CALC && r.operation != AES_WIN_SLIDER && r.selector) ||
        (r.operation != AES_WIN_FIND &&
         (r.rectangle.g_x || r.rectangle.g_y || r.rectangle.g_w || r.rectangle.g_h)) ||
        (r.operation == AES_WIN_FIND && (r.rectangle.g_w || r.rectangle.g_h)) ||
        (!geometry_input && r.operation != AES_WIN_TEXT && r.data))
        return EINVAL;
    if (geometry_input) {
        if ((r.data & 1) || !kproc_copy_from_user(&r.rectangle, r.data, sizeof(r.rectangle)))
            return EIMBA;
        if (r.rectangle.g_w <= 0 || r.rectangle.g_h <= 0 ||
            r.rectangle.g_w > 32765 || r.rectangle.g_h > 32765 ||
            r.rectangle.g_x < -32765 || r.rectangle.g_y < -32765 ||
            (LONG)r.rectangle.g_x + r.rectangle.g_w > 32765L ||
            (LONG)r.rectangle.g_y + r.rectangle.g_h > 32765L)
            return EINVAL;
    }
    if (r.operation == AES_WIN_TEXT) {
        for (i = 0; i < sizeof(text); i++) {
            if ((UQUAD)r.data + i > 0xffffffffULL ||
                !kproc_copy_from_user(text + i, (UQUAD)r.data + i, 1))
                return EIMBA;
            if (!text[i])
                break;
        }
        if (i == sizeof(text))
            return EINVAL;
    }
    if (r.operation == AES_WIN_GET)
        size = sizeof(info);
    if (r.operation == AES_WIN_VISIBLE || r.operation == AES_WIN_CALC)
        size = sizeof(rectangle);
    if (size) {
        if ((output & 1) || !kproc_validate_user_write(output, size))
            return EIMBA;
    } else if (output)
        return EINVAL;
#if CONF_WITH_AES
    if (!x86_64_aes_owned(run))
        return EACCDN;
    result = x86_64_aes_window(run, &r, text, &info, &rectangle, output);
    if (result < 0)
        return result;
    /* Visible rectangles copy and commit their cursor together, without
     * yielding cleanup between selection, copyout and return. */
    if (size && r.operation != AES_WIN_VISIBLE && !kproc_copy_to_user(output,
            r.operation == AES_WIN_GET ? (const void *)&info : &rectangle, size))
        return EIMBA;
    if (r.operation == AES_WIN_VISIBLE)
        *defer_return_work = TRUE;
    return result;
#else
    (void)result;
    (void)defer_return_work;
    return EACCDN;
#endif
}

LONG aes_service(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d,
                   BOOL *defer_return_work)
{
    AES_CLIENT_STATE *p;
    char name[AES_NAME_MAX + 1];
    LONG rc;

    if (!kproc_user_aspace(run))
        return EACCDN;
    if (fn > AES_SERVICE_OBJECT)
        return EINVFN;
    if (c || d || ((fn == AES_SERVICE_EXIT || fn == AES_SERVICE_YIELD) && (a || b)) ||
        ((fn == AES_SERVICE_FIND || fn == AES_SERVICE_READ ||
          fn == AES_SERVICE_UPDATE) && b))
        return EINVAL;
    if (fn == AES_SERVICE_INIT)
        return register_client(a, b);
    p = kproc_aes_get(run);
    if (!p)
        return EACCDN;
    switch (fn) {
    case AES_SERVICE_EXIT:
        aes_service_process_exit(run);
#if CONF_WITH_AES
        x86_64_aes_window_unregister(run);
        x86_64_aes_objects_reap();
#endif
        return E_OK;
    case AES_SERVICE_FIND:
        rc = copy_name(a, name);
        if (rc)
            return rc;
        p = find_name(name);
        return p ? p->id : ESRCH;
    case AES_SERVICE_WRITE:
        return send_message(p, a, b);
    case AES_SERVICE_READ:
        return receive_message(p, a, FALSE);
    case AES_SERVICE_POLL:
        if (a != AES_EVENT_MESSAGE)
            return EINVAL;
        return receive_message(p, b, TRUE);
    case AES_SERVICE_UPDATE:
        return update_ownership(p, a);
    case AES_SERVICE_MULTI:
        return input_events(a, b);
    case AES_SERVICE_WINDOW:
        return window_service(a, b, defer_return_work);
    case AES_SERVICE_OBJECT:
#if CONF_WITH_AES
        return x86_64_aes_objects(a, b);
#else
        return EACCDN;
#endif
    case AES_SERVICE_WAIT:
        if (a != AES_EVENT_MESSAGE)
            return EINVAL;
#if CONF_WITH_AES
        if (!x86_64_aes_owned(run))
            return EACCDN;
        if ((b & 1) || !kproc_validate_user_write(b, sizeof(AES_EVENT)))
            return EIMBA;
        /* A successful previous wait can leave a pending wake flag. Check
         * the queue again after every resume, and validate copyout afresh. */
        do {
            flush_native_messages(p);
            x86_64_aes_flags(run, 2); /* AP_MESAG, even if already queued */
            if (p->count)
                return receive_message(p, b, TRUE);
            rc = x86_64_aes_wait(run);
            p = kproc_aes_get(run);
            if (!p)
                return EACCDN;
        } while (!rc);
        return rc;
#else
        return EACCDN;
#endif
    case AES_SERVICE_YIELD:
#if CONF_WITH_AES
        return x86_64_aes_yield(run);
#else
        return EACCDN;
#endif
    }
    return EINVFN;
}

/* Legacy kernel AES sends AC_OPEN/AC_CLOSE to its associated typed client.
 * The payload is copied, never a legacy AESPB or a user pointer. */
LONG aes_service_kernel_message(PD *pd, const WORD *words)
{
    AES_CLIENT_STATE *p = kproc_aes_get(pd);
    AES_MESSAGE message;

    if (!p)
        return EACCDN;
#if CONF_WITH_AES
    if (x86_64_aes_window_redraw(pd, words))
        return E_OK;
#endif
    if (p->count == AES_MESSAGE_QUEUE)
        return EAGAIN;
    memcpy(&message, words, sizeof(message));
#if CONF_WITH_AES
    x86_64_aes_tree_message(pd, (WORD *)&message);
#endif
    message.sender = 0;
    message.reserved = 0;
    p->queue[(p->head + p->count) % AES_MESSAGE_QUEUE] = message;
    p->count++;
#if CONF_WITH_AES
    x86_64_aes_wake(pd);
#endif
    return E_OK;
}
