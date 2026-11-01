/* Real loaded .ACC consumers linked with libcmini's full/minimal startup.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later.
 */
#include <mint/mintbind.h>
#include <mint/basepage.h>
#include "aes_client.h"
#include "aes_objects.h"
#include "gemerror.h"
#include "vdipb.h"

extern short _app;
int accessory_input_checks(const AES_APP_INFO *info, WORD role, WORD handle);
int accessory_window_checks(const AES_APP_INFO *info, WORD role, WORD vdi_handle);
int accessory_window_redraw(WORD handle, WORD vdi_handle);
int accessory_window_reopen(WORD role, WORD vdi_handle);
int accessory_window_borrow(void);
int accessory_object_checks(WORD vdi_handle);
int accessory_object_fault(WORD vdi_handle);
int accessory_window_object_restore(WORD vdi_handle);
int accessory_window_pool(WORD command, WORD *handle);
long _stksize = 65536;
UQUAD acc_entry_base, acc_entry_type, acc_entry_stack;
UWORD acc_entry_cs;
static volatile ULONG private_word = 0x389548;

#define CHECK(c) do { if (!(c)) { (void)Cconws("accessory FAIL: " #c "\r\n"); return 99; } } while (0)

int main(int argc, char **argv, char **envp)
{
    AES_APP_INFO info;
#if ACC_ROLE != 3
    AES_EVENT event;
    AES_CLIENT_MESSAGE message = { 0 };
#endif
    VDICONTROL control = { 0 };
    WORD input[11] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2 };
    WORD output[57], points[12];
    VDIPB pb = { &control, input, points, output, points };
    LONG heap;
#if ACC_ROLE == 4
    WORD queued = 0;
#endif
#if ACC_ROLE == 2
    AES_EVENT_REQUEST request = { 0 };
    AES_INPUT_EVENT input_event;
    BOOL combined = FALSE;
#endif
#if ACC_ROLE == 1
    LONG peer;
    WORD i;
#endif
    ULONG mxcsr, desired = 0x1f80 | ((ACC_ROLE & 3) << 13);

    (void)argc; (void)argv; (void)envp;
    CHECK((acc_entry_cs & 3) == 3 && !((acc_entry_stack + 8) & 15));
    CHECK(acc_entry_base == (uintptr_t)_base && _base->p_lowtpa == (char *)_base);
    CHECK(acc_entry_type == 1 && _app == 0);
    CHECK(Psetlimit(PSETLIMIT_STACK, -1) == _stksize);
    CHECK(private_word == 0x389548);
    private_word = ACC_ROLE;
    heap = Malloc(4096);
    CHECK(heap > 0);
    *(volatile ULONG *)heap = 0x548389;
    /* Ordinary nested Pexec keeps ENTRY_PROGRAM and resumes this accessory. */
    CHECK(Pexec(0, "X32HELLO.TOS", "", NULL) == E_OK);
    CHECK(aes_appl_init(ACC_NAME, &info) == E_OK);
    CHECK(info.features & AES_FEATURE_WAIT);
    CHECK(aes_evnt_wait(AES_EVENT_MESSAGE, (AES_EVENT *)1) == EIMBA);
    control.code = 100; control.nintin = 11; control.handle = 1;
    CHECK(vdi(&pb) == E_OK && control.handle > 0);
    /* Leave one workstation, a file and a heap block to process cleanup. */
    CHECK(Fopen("X32AES.TOS", 0) >= 0);
    __asm__ volatile ("ldmxcsr %0" :: "m"(desired));
#if ACC_ROLE == 1
    for (i = 0; i < 100; i++) {
        peer = aes_appl_find("ACCTWO");
        if (peer > 0) break;
        CHECK(aes_appl_yield() == E_OK);
    }
    CHECK(peer > 0);
    message.type = 389; message.data[0] = 548;
    CHECK(aes_appl_write((WORD)peer, &message) == E_OK);
    CHECK(aes_evnt_wait(AES_EVENT_MESSAGE, &event) == AES_EVENT_MESSAGE);
    CHECK(event.message.type == 407 && event.message.data[0] == 548);
    CHECK(aes_appl_yield() == E_OK);
#elif ACC_ROLE == 2
    CHECK(aes_evnt_wait(AES_EVENT_MESSAGE, &event) == AES_EVENT_MESSAGE);
    CHECK(event.message.type == 389 && event.message.data[0] == 548);
    message.type = 407; message.data[0] = 548;
    CHECK(aes_appl_write(event.message.sender, &message) == E_OK);
    CHECK(aes_appl_yield() == E_OK);
#endif
    CHECK(accessory_input_checks(&info, ACC_ROLE, control.handle) == 0);
#if ACC_ROLE != 4
    CHECK(accessory_window_checks(&info, ACC_ROLE, control.handle) == 0);
#endif
    __asm__ volatile ("stmxcsr %0" : "=m"(mxcsr));
    CHECK(mxcsr == desired && private_word == ACC_ROLE);
    CHECK(*(volatile ULONG *)heap == 0x548389);
    (void)Cconws("accessory " ACC_NAME ": PASS\r\n");
#if ACC_ROLE == 1
    return 17;
#elif ACC_ROLE == 3
    CHECK(accessory_object_fault(control.handle) == 0);
    __asm__ volatile ("ud2");
#else
    for (;;) {
#if ACC_ROLE == 2
        if (combined) {
            /* Leave real input/timer EVBs suspended across desktop restart. */
            request.mask = AES_EVENT_KEYBOARD | AES_EVENT_MOUSE1 |
                AES_EVENT_MOUSE2 | AES_EVENT_TIMER | AES_EVENT_MESSAGE;
            request.timer_ms = 60000;
            request.mouse1.rectangle.g_x = request.mouse1.rectangle.g_y = 32760;
            request.mouse1.rectangle.g_w = request.mouse1.rectangle.g_h = 1;
            request.mouse2.outside = 1;
            request.mouse2.rectangle.g_w = request.mouse2.rectangle.g_h = 32767;
            CHECK(aes_evnt_multi(&request, &input_event) > 0);
            if (!(input_event.events & AES_EVENT_MESSAGE))
                continue;
            event.message = input_event.message;
        } else
#endif
        CHECK(aes_evnt_wait(AES_EVENT_MESSAGE, &event) == AES_EVENT_MESSAGE);
#if ACC_ROLE == 4
        if (event.message.type == 663) {
            AES_OBJECT_REQUEST foreign = { 0 };
            AES_OBJECT_RESULT result;
            foreign.tree = (UWORD)event.message.data[0] | (ULONG)(UWORD)event.message.data[1] << 16;
            foreign.operation = AES_OBJECT_GET;
            message.type = 664;
            message.data[0] = aes_object(&foreign, &result);
            CHECK(aes_appl_write(event.message.sender, &message) == E_OK);
            continue;
        }
        if (event.message.type == 640 || event.message.type == 642 || event.message.type == 644) {
            CHECK(accessory_window_pool(event.message.type, &message.data[0]) == 0);
            message.type = event.message.type + 1;
            CHECK(aes_appl_write(event.message.sender, &message) == E_OK);
            continue;
        }
        if (event.message.type == 636) {
            CHECK(accessory_window_borrow() == 0);
            continue;
        }
        if (event.message.type == 637) {
            message.type = 639;
            CHECK(aes_appl_write(event.message.sender, &message) == E_OK);
            continue;
        }
        if (event.message.type == 634 && !event.message.sender) {
            GRECT temporary = { 80, 80, 200, 160 };
            LONG result = aes_wind_create(0, &temporary);
            LONG receiver = aes_appl_find("ACCTWO");

            CHECK(receiver > 0);
            message.type = 635; message.data[0] = result;
            CHECK(aes_appl_write((WORD)receiver, &message) == E_OK);
            continue;
        }
        if (event.message.type == 626) {
            CHECK(accessory_window_checks(&info, ACC_ROLE, control.handle) == 0);
            continue;
        }
        if (event.message.type == 613) {
            message.type = 614;
            CHECK(aes_appl_write(event.message.sender, &message) == E_OK);
            continue;
        }
        if (event.message.type == 548) {
            CHECK(event.message.data[0] == queued && queued < AES_MESSAGE_QUEUE);
            queued++;
            continue;
        }
        if (event.message.type == 41) {
            CHECK(queued == AES_MESSAGE_QUEUE);
            queued = 0;
        }
#endif
        if (event.message.type == 662 && !event.message.sender) {
            CHECK(accessory_object_checks(control.handle) == 0);
            CHECK(accessory_window_object_restore(control.handle) == 0);
            continue;
        }
        if (event.message.type == 631 && !event.message.sender) {
            CHECK(accessory_window_reopen(ACC_ROLE, control.handle) == 0);
            continue;
        }
        if (event.message.type == 20 && !event.message.sender) {
            CHECK(accessory_window_redraw(event.message.data[0], control.handle) == 0);
            continue;
        }
        if (event.message.type >= 20 && event.message.type <= 31)
            continue;
        CHECK(event.message.type == 41); /* AC_CLOSE */
#if ACC_ROLE == 2
        combined = TRUE;
#endif
    }
#endif
    return 98;
}
