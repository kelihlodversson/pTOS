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
    return E_OK;
}

static LONG receive_message(AES_CLIENT_STATE *p, UQUAD output, BOOL poll)
{
    AES_EVENT event = { 0 };
    ULONG size = poll ? sizeof(event) : sizeof(AES_MESSAGE);

    if ((output & 1) || !kproc_validate_user_write(output, size))
        return EIMBA;
    if (p->count) {
        event.events = AES_EVENT_MESSAGE;
        event.message = p->queue[p->head];
    } else if (!poll)
        return EAGAIN;
    if (!kproc_copy_to_user(output, poll ? (const void *)&event : &event.message, size))
        return EIMBA;
    /* A rejected/faulting output must not consume the queued message. */
    if (event.events) {
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

LONG aes_service(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    AES_CLIENT_STATE *p;
    char name[AES_NAME_MAX + 1];
    LONG rc;

    if (!kproc_user_aspace(run))
        return EACCDN;
    if (fn > AES_SERVICE_UPDATE)
        return EINVFN;
    if (c || d || ((fn == AES_SERVICE_EXIT) && (a || b)) ||
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
    }
    return EINVFN;
}
