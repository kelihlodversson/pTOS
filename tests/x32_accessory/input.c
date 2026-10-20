/* Typed input consumer, run as a loaded ring-3 accessory. */
#include <mint/mintbind.h>
#include "aes_client.h"
#include "gemerror.h"

int accessory_input_checks(const AES_APP_INFO *info, WORD role);

#define CHECK(c) do { if (!(c)) { (void)Cconws("accessory input FAIL: " #c "\r\n"); return 99; } } while (0)

static LONG raw_events(UQUAD input, UQUAD output, UQUAD extra)
{
    register UQUAD rax __asm__("rax") = (0x101ULL << 32) | 9;
    register UQUAD rdi __asm__("rdi") = input;
    register UQUAD rsi __asm__("rsi") = output;
    register UQUAD rdx __asm__("rdx") = extra;
    register UQUAD r10 __asm__("r10") = 0;

    __asm__ volatile ("syscall" : "+r"(rax)
                      : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10)
                      : "rcx", "r11", "memory", "cc");
    return (LONG)rax;
}

int accessory_input_checks(const AES_APP_INFO *info, WORD role)
{
    AES_EVENT_REQUEST request = { 0 };
    AES_INPUT_EVENT event;
    static const AES_INPUT_EVENT immutable = { 0 };
    AES_MESSAGE message = { 613, 0, 0, { 548 } };
    union { AES_EVENT_REQUEST request; AES_INPUT_EVENT event; } alias;
    LONG allocation, peer;
    UBYTE *edge;
    WORD i, x, y;

    CHECK(info->features & AES_FEATURE_INPUT);
    if (role == 4) {
        /* These conditions are true globally, but this task is unfocused. */
        request.mask = AES_EVENT_ALL & ~(AES_EVENT_TIMER | AES_EVENT_MESSAGE);
        request.flags = AES_EVENT_POLL;
        request.clicks = 1; request.button_mask = 3;
        request.mouse1.rectangle.g_w = request.mouse1.rectangle.g_h = 32767;
        request.mouse2.outside = 1;
        request.mouse2.rectangle.g_w = request.mouse2.rectangle.g_h = 1;
        CHECK(aes_evnt_multi(&request, &event) == 0);
        CHECK(!event.events && !event.key && !event.clicks && !event.message.type);
        return 0;
    }
    if (role != 2)
        return 0;
    request.mask = AES_EVENT_TIMER;
    CHECK(aes_evnt_multi((AES_EVENT_REQUEST *)1, &event) == EIMBA);
    CHECK(aes_evnt_multi(&request, (AES_INPUT_EVENT *)1) == EIMBA);
    CHECK(aes_evnt_multi(&request, (AES_INPUT_EVENT *)&immutable) == EIMBA);
    CHECK(raw_events(0x100000000ULL + (uintptr_t)&request,
                     (uintptr_t)&event, 0) == EIMBA);
    CHECK(raw_events((uintptr_t)&request, 0xffff800000000000ULL, 0) == EIMBA);
    CHECK(raw_events((uintptr_t)&request, (uintptr_t)&event, 1) == EINVAL);
    allocation = Malloc(4096);
    CHECK(allocation > 0);
    edge = (UBYTE *)(uintptr_t)(ULONG)allocation;
    CHECK(aes_evnt_multi((AES_EVENT_REQUEST *)(edge + 4092), &event) == EIMBA);
    CHECK(aes_evnt_multi(&request, (AES_INPUT_EVENT *)(edge + 4092)) == EIMBA);
    CHECK(Mfree(edge) == E_OK);
    request.mask = 0;
    CHECK(aes_evnt_multi(&request, &event) == EINVAL);
    request.mask = 0x40;
    CHECK(aes_evnt_multi(&request, &event) == EINVAL);
    request.mask = AES_EVENT_TIMER; request.flags = 2;
    CHECK(aes_evnt_multi(&request, &event) == EINVAL);
    request.flags = 0; request.reserved = 1;
    CHECK(aes_evnt_multi(&request, &event) == EINVAL);
    request.reserved = 0; request.timer_ms = 0x80000000UL;
    CHECK(aes_evnt_multi(&request, &event) == EINVAL);
    request.timer_ms = 0; request.mask = AES_EVENT_BUTTON;
    CHECK(aes_evnt_multi(&request, &event) == EINVAL);
    request.clicks = 256; request.button_mask = 1;
    CHECK(aes_evnt_multi(&request, &event) == EINVAL);
    request.clicks = 1; request.button_state = 2;
    CHECK(aes_evnt_multi(&request, &event) == EINVAL);
    request.button_state = 0; request.button_mask = 4;
    CHECK(aes_evnt_multi(&request, &event) == EINVAL);
    request.mask = AES_EVENT_MOUSE1;
    CHECK(aes_evnt_multi(&request, &event) == EINVAL);
    request.mouse1.rectangle.g_w = request.mouse1.rectangle.g_h = 1;
    request.mouse1.outside = 2;
    CHECK(aes_evnt_multi(&request, &event) == EINVAL);
    request.mouse1.outside = 0; request.mouse1.rectangle.g_x = 32767;
    CHECK(aes_evnt_multi(&request, &event) == EINVAL);

    request.mask = AES_EVENT_MESSAGE | AES_EVENT_TIMER;
    CHECK(aes_appl_write(info->id, &message) == E_OK);
    CHECK(aes_evnt_multi(&request, (AES_INPUT_EVENT *)&immutable) == EIMBA);
    CHECK(aes_evnt_multi(&request, &event) == (AES_EVENT_MESSAGE | AES_EVENT_TIMER));
    CHECK(event.events == (AES_EVENT_MESSAGE | AES_EVENT_TIMER));
    CHECK(event.message.type == 613 && event.message.sender == info->id &&
          event.message.data[0] == 548 && !event.reserved && !event.message.reserved);
    alias.request = request;
    CHECK(aes_evnt_multi(&alias.request, &alias.event) == AES_EVENT_TIMER);
    CHECK(!alias.event.message.type && !alias.event.key && !alias.event.clicks);

    request.flags = AES_EVENT_POLL; request.timer_ms = 20;
    CHECK(aes_evnt_multi(&request, &event) == 0);
    CHECK(!event.events && !event.message.type);
    request.flags = 0; request.mask = AES_EVENT_ALL;
    request.clicks = 2; request.button_mask = 1; request.button_state = 1;
    request.mouse1.rectangle.g_x = 0;
    request.mouse2.outside = 1;
    request.mouse2.rectangle.g_w = request.mouse2.rectangle.g_h = 32767;
    for (i = 0; i < 16; i++) {
        CHECK(aes_evnt_multi(&request, &event) == AES_EVENT_TIMER);
        CHECK(!event.key && !event.clicks && !event.message.type);
    }
    peer = aes_appl_find("ACCWAIT");
    CHECK(peer > 0);
    CHECK(aes_appl_write((WORD)peer, &message) == E_OK);
    request.mask = AES_EVENT_MESSAGE | AES_EVENT_TIMER; request.timer_ms = 2000;
    CHECK(aes_evnt_multi(&request, &event) == AES_EVENT_MESSAGE);
    CHECK(event.message.type == 614 && event.message.sender == peer);
    request.mask = AES_EVENT_KEYBOARD | AES_EVENT_TIMER | AES_EVENT_MESSAGE;
    request.timer_ms = 2000;
    (void)Cconws("x32 input keyboard: READY\r\n");
    CHECK(aes_evnt_multi(&request, &event) == AES_EVENT_KEYBOARD);
    CHECK(event.key == 0x1e61 && !event.message.type && !event.reserved);

    request.mask = AES_EVENT_MOUSE1 | AES_EVENT_TIMER;
    x = event.mouse_x; y = event.mouse_y;
    request.mouse1.outside = 1;
    request.mouse1.rectangle.g_x = x - 2; request.mouse1.rectangle.g_y = y - 2;
    request.mouse1.rectangle.g_w = request.mouse1.rectangle.g_h = 5;
    (void)Cconws("x32 input mouse: READY\r\n");
    CHECK(aes_evnt_multi(&request, &event) == AES_EVENT_MOUSE1);
    CHECK(event.mouse_x < x - 2 || event.mouse_x >= x + 3 ||
          event.mouse_y < y - 2 || event.mouse_y >= y + 3);

    request.mask = AES_EVENT_BUTTON | AES_EVENT_TIMER; request.clicks = 1;
    (void)Cconws("x32 input button: READY\r\n");
    CHECK(aes_evnt_multi(&request, &event) == AES_EVENT_BUTTON);
    CHECK(event.clicks == 1 && (event.buttons & 1));
    request.button_state = 0;
    (void)Cconws("x32 input release: READY\r\n");
    CHECK(aes_evnt_multi(&request, &event) == AES_EVENT_BUTTON);
    CHECK(event.clicks == 1 && !(event.buttons & 1));
    (void)Cconws("x32 input events: PASS\r\n");
    return 0;
}
