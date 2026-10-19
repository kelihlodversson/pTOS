/* Real loaded .ACC consumers linked with libcmini's full/minimal startup.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later.
 */
#include <mint/mintbind.h>
#include <mint/basepage.h>
#include "aes_client.h"
#include "gemerror.h"
#include "vdipb.h"

extern short _app;
long _stksize = 65536;
UQUAD acc_entry_base, acc_entry_type, acc_entry_stack;
UWORD acc_entry_cs;
static volatile ULONG private_word = 0x389548;

#define CHECK(c) do { if (!(c)) { (void)Cconws("accessory FAIL: " #c "\r\n"); return 99; } } while (0)

int main(int argc, char **argv, char **envp)
{
    AES_APP_INFO info;
    AES_EVENT event;
    AES_MESSAGE message = { 0 };
    VDICONTROL control = { 0 };
    WORD input[11] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2 };
    WORD output[57], points[12];
    VDIPB pb = { &control, input, points, output, points };
    LONG heap;
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
    __asm__ volatile ("stmxcsr %0" : "=m"(mxcsr));
    CHECK(mxcsr == desired && private_word == ACC_ROLE);
    CHECK(*(volatile ULONG *)heap == 0x548389);
    (void)Cconws("accessory " ACC_NAME ": PASS\r\n");
#if ACC_ROLE == 1
    return 17;
#elif ACC_ROLE == 3
    __asm__ volatile ("ud2");
#else
    for (;;) {
        CHECK(aes_evnt_wait(AES_EVENT_MESSAGE, &event) == AES_EVENT_MESSAGE);
        CHECK(event.message.type == 41); /* AC_CLOSE */
    }
#endif
    return 98;
}
