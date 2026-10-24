/* A real ordinary GEM process, nested child and cooperating desk accessory.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "bdosdefs.h"
#include "aes_objects.h"
#include "aes_shell.h"
#include "gemerror.h"
#include "syscall.h"
#include "vdipb.h"

UQUAD entry_base, entry_type, entry_stack;
UWORD entry_cs;
static char shell_saved[AES_SHELL_BUFFER_MAX], shell_probe[AES_SHELL_BUFFER_MAX];
static char long_command[AES_SHELL_PATH_MAX];
static char boundary_tail[AES_SHELL_TAIL_BYTES];
static const char teardown_tail[128] = { 8, 't', 'e', 'a', 'r', 'd', 'o', 'w', 'n' };
static const char fault_tail[128] = { 5, 'f', 'a', 'u', 'l', 't' };
static const char child_tail[128] = { 5, 'c', 'h', 'i', 'l', 'd' };

static void record(const char *text)
{
    while (*text)
        desk_syscall(13, 3, 1, *text++, 0, 0);
}

static void finish(LONG result) NORETURN;
static void finish(LONG result)
{
    desk_syscall(1, 0x4c, result, 0, 0, 0);
    for (;;)
        ;
}

static void fail(WORD line) NORETURN;
static void fail(WORD line)
{
    char digits[8];
    WORD i = 0;

    record("x32 desktop process FAIL line ");
    do {
        digits[i++] = '0' + line % 10;
        line /= 10;
    } while (line);
    while (i)
        desk_syscall(13, 3, 1, digits[--i], 0, 0);
    record("\r\n");
    finish(99);
}
#define CHECK(condition) do { if (!(condition)) fail(__LINE__); } while (0)

static void accessory(void) NORETURN;
static void accessory(void)
{
    AES_APP_INFO app;
    AES_EVENT event;
    AES_MESSAGE reply = { 651, 0, 0, { 0 } };
    GRECT rectangle = { 850, 500, 260, 120 };
    LONG h, rc;
    BOOL opened = FALSE;

    CHECK(aes_appl_init("DESKACC", &app) == E_OK);
    CHECK((app.features & (AES_FEATURE_WAIT | AES_FEATURE_WINDOW)) ==
                          (AES_FEATURE_WAIT | AES_FEATURE_WINDOW));
    /* The shell resets the window table when it starts the first desktop
     * too. Allocate lazily on AC_OPEN, as at subsequent application returns. */
    h = 0;
    record("x32 desktop accessory READY\r\n");
    for (;;) {
        CHECK(aes_evnt_wait(AES_EVENT_MESSAGE, &event) == AES_EVENT_MESSAGE);
        if (event.message.type == 652) {
            AES_MESSAGE ack = { 653, 0, 0, { 0 } };
            AES_EVENT_REQUEST wait = { AES_EVENT_TIMER, 0, 0, 0, 0, 0, { 0 }, { 0 }, 150 };
            AES_INPUT_EVENT input;
            WORD mode = event.message.data[0];

            CHECK(aes_wind_update(mode) == E_OK);
            CHECK(aes_appl_write(event.message.sender, &ack) == E_OK);
            CHECK(aes_evnt_multi(&wait, &input) == AES_EVENT_TIMER);
            CHECK(aes_wind_update(mode - 1) == E_OK);
        } else if (event.message.type == 41) {
            if (opened) {
                do {
                    rc = aes_wind_close(h);
                    if (rc == EAGAIN)
                        CHECK(aes_appl_yield() == E_OK);
                } while (rc == EAGAIN);
                CHECK(rc == E_OK || rc == EIHNDL || rc == EACCDN);
            }
            /* The native shell resets window handles at an application
             * boundary. Discard the old handle even if it was closed. */
            h = 0;
            opened = FALSE;
            record("x32 desktop accessory CLOSE\r\n");
        } else if (event.message.type == 22 && opened) {
            do {
                rc = aes_wind_close(h);
                if (rc == EAGAIN)
                    CHECK(aes_appl_yield() == E_OK);
            } while (rc == EAGAIN);
            CHECK(rc == E_OK);
            opened = FALSE;
            record("x32 desktop accessory window CLOSED\r\n");
        } else if (event.message.type == 40 || event.message.type == 650) {
            if (!h) {
                do {
                    h = aes_wind_create(AES_WINDOW_NAME_GADGET | AES_WINDOW_MOVER | AES_WINDOW_CLOSER, &rectangle);
                    if (h == EAGAIN)
                        CHECK(aes_appl_yield() == E_OK);
                } while (h == EAGAIN);
                CHECK(h > 0);
                do {
                    rc = aes_wind_set_text(h, AES_WINDOW_NAME, "Ring-3 accessory");
                    if (rc == EAGAIN)
                        CHECK(aes_appl_yield() == E_OK);
                } while (rc == EAGAIN);
                CHECK(rc == E_OK);
            }
            if (!opened) {
                /* AC_OPEN may arrive before the desktop releases its
                 * menu/update lock. Typed window services ask us to yield. */
                do {
                    rc = aes_wind_open(h, &rectangle);
                    if (rc == EAGAIN)
                        CHECK(aes_appl_yield() == E_OK);
                } while (rc == EAGAIN);
                CHECK(rc == E_OK);
            }
            opened = TRUE;
            record("x32 desktop accessory OPEN\r\n");
            if (event.message.type == 650)
                CHECK(aes_appl_write(event.message.sender, &reply) == E_OK);
        }
    }
}

static void nested(void) NORETURN;
static void nested(void)
{
    AES_APP_INFO app;
    AES_WINDOW_INFO window;
    AES_EVENT_REQUEST request = { AES_EVENT_TIMER, AES_EVENT_POLL, 0, 0, 0, 0, { 0 }, { 0 }, 0 };
    AES_INPUT_EVENT event;

    CHECK(aes_appl_init("GEMCHILD", &app) == E_OK);
    CHECK(!(app.features & (AES_FEATURE_INPUT | AES_FEATURE_WINDOW | AES_FEATURE_OBJECT)));
    CHECK(aes_wind_get(0, &window) == EACCDN);
    CHECK(aes_evnt_multi(&request, &event) == EACCDN);
    CHECK(aes_appl_exit() == E_OK);
    record("x32 desktop nested ownership PASS\r\n");
    finish(0);
}

static void schedule(const char *tail)
{
    AES_SHELL_REQUEST request = { 0 };

    request.operation = AES_SHELL_WRITE;
    request.value = request.extra = request.bytes = 1;
    request.data = (ULONG)(uintptr_t)"C:\\APPTEST.PRG";
    request.second = (ULONG)(uintptr_t)tail;
    CHECK(aes_shell(&request) > 0);
}

static void tail_boundary(PD *basepage) NORETURN;
static void tail_boundary(PD *basepage)
{
    AES_APP_INFO app;
    WORD i;

    CHECK(aes_appl_init("TAILTEST", &app) == E_OK);
    CHECK(basepage->p_cmdlin[0] == 126 && basepage->p_cmdlin[127] == 0);
    for (i = 1; i < 127; i++)
        CHECK(basepage->p_cmdlin[i] == 'b');
    schedule(teardown_tail);
    CHECK(aes_appl_exit() == E_OK);
    record("x32 desktop shell tail boundary PASS\r\n");
    finish(0);
}

static void teardown(BOOL fault) NORETURN;
static void teardown(BOOL fault)
{
    AES_APP_INFO app;
    LONG handles[12], handle, rc;
    WORD count = 0, i;
    AES_EVENT_REQUEST poll = { AES_EVENT_MESSAGE, AES_EVENT_POLL, 0, 0, 0, 0, { 0 }, { 0 }, 0 };
    AES_INPUT_EVENT input;
    GRECT rectangle = { 360, 140, 260, 160 };

    CHECK(aes_appl_init("TEARDOWN", &app) == E_OK);
    while (count < 12) {
        handle = aes_wind_create(AES_WINDOW_NAME_GADGET, &rectangle);
        if (handle == ENSMEM)
            break;
        CHECK(handle > 0);
        handles[count++] = handle;
    }
    CHECK(count >= 4);
    /* Cleanup visits ascending handles. Open in reverse so every close
     * exposes a different live window, generating distinct redraw messages. */
    for (i = count - 1; i >= 0; i--) {
        CHECK(aes_wind_open(handles[i], &rectangle) == E_OK);
        do {
            rc = aes_evnt_multi(&poll, &input);
            CHECK(rc >= 0);
        } while (rc);
    }
    if (!fault) {
        schedule(fault_tail);
        record("x32 desktop teardown normal READY\r\n");
        finish(0);
    }
    record("x32 desktop teardown fault READY\r\n");
    __asm__ volatile ("ud2");
    finish(99);
}

void desktop_test_start(PD *basepage, LONG kind) NORETURN;
void desktop_test_start(PD *basepage, LONG kind)
{
    AES_APP_INFO app;
    AES_WINDOW_INFO info;
    AES_EVENT event;
    AES_MESSAGE message = { 650, 0, 0, { 0 } };
    AES_SHELL_REQUEST shell = { 0 };
    AES_OBJECT_REQUEST object = { 0 };
    AES_OBJECT_RESULT result;
    AES_EVENT_REQUEST timer = { AES_EVENT_TIMER, 0, 0, 0, 0, 0, { 0 }, { 0 }, 20 };
    AES_INPUT_EVENT input;
    GRECT rectangle = { 360, 140, 260, 160 }, visible;
    VDICONTROL control = { 0 };
    WORD workin[11] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2 };
    WORD workout[57], points[12];
    VDIPB pb = { &control, workin, points, workout, points };
    LONG h, peer, rc;
    WORD i;

    CHECK((entry_cs & 3) == 3 && ((entry_stack + 8) & 15) == 0);
    CHECK(entry_base == (uintptr_t)basepage && entry_type == (ULONG)kind);
    if (kind == 1)
        accessory();
    CHECK(kind == 0);
    if (basepage->p_cmdlin[0] == 126 && basepage->p_cmdlin[1] == 'b')
        tail_boundary(basepage);
    if (basepage->p_cmdlin[1] == 't' || basepage->p_cmdlin[1] == 'f')
        teardown(basepage->p_cmdlin[1] == 'f');
    if (basepage->p_cmdlin[0] == 5 && basepage->p_cmdlin[1] == 'c')
        nested();
    CHECK(aes_appl_init("GEMPROBE", &app) == E_OK);
    CHECK((app.features & 0x3f) == 0x3f);
    record("x32 desktop application CPL3 PASS\r\n");
    /* The legacy GEM parameter-block entry remains closed to user mode. */
    CHECK(desk_syscall(2, 200, 1, 0, 0, 0) == -1);
    CHECK(aes_wind_get(0, &info) == E_OK);
    CHECK(aes_wind_rect(0, 0, &visible) == E_OK && visible.g_w > 0 && visible.g_h > 0);
    CHECK(aes_wind_get(0, (AES_WINDOW_INFO *)1) == EIMBA);
    CHECK(aes_object((AES_OBJECT_REQUEST *)1, &result) == EIMBA);
    object.operation = AES_OBJECT_METRICS;
    CHECK(aes_object(&object, (AES_OBJECT_RESULT *)1) == EIMBA);
    CHECK(aes_object(&object, &result) == E_OK && result.value > 0);
    shell.operation = AES_SHELL_GET; shell.data = (ULONG)(uintptr_t)shell_saved;
    shell.bytes = sizeof(shell_saved);
    CHECK(aes_shell(&shell) == E_OK);
    CHECK(aes_shell((AES_SHELL_REQUEST *)1) == EIMBA);
    shell.reserved = 1;
    CHECK(aes_shell(&shell) == EINVAL);
    shell.reserved = 0; shell.bytes++;
    CHECK(aes_shell(&shell) == ERANGE);
    shell.bytes = sizeof(shell_probe); shell.data = (ULONG)(uintptr_t)shell_probe;
    for (i = 0; i < sizeof(shell_probe); i++)
        shell_probe[i] = i & 127;
    shell.operation = AES_SHELL_PUT;
    CHECK(aes_shell(&shell) == E_OK);
    shell.operation = AES_SHELL_GET; shell.data = (ULONG)(uintptr_t)shell_saved;
    /* Restore the desktop's saved context after checking a separate buffer. */
    shell.data = (ULONG)(uintptr_t)workout; shell.bytes = sizeof(workout);
    CHECK(aes_shell(&shell) == E_OK);
    for (i = 0; i < sizeof(workout); i++)
        CHECK(((UBYTE *)workout)[i] == (i & 127));
    shell.operation = AES_SHELL_PUT; shell.data = (ULONG)(uintptr_t)shell_saved;
    shell.bytes = sizeof(shell_saved);
    CHECK(aes_shell(&shell) == E_OK);
    for (i = 0; i < sizeof(long_command) - 1; i++)
        long_command[i] = 'A';
    long_command[sizeof(long_command) - 1] = 0;
    shell.operation = AES_SHELL_WRITE; shell.value = 1; shell.extra = 1;
    shell.bytes = 1; shell.data = (ULONG)(uintptr_t)long_command;
    shell.second = (ULONG)(uintptr_t)child_tail;
    CHECK(aes_shell(&shell) == ERANGE);
    shell.operation = AES_SHELL_WDEF; shell.value = shell.extra = 0; shell.bytes = 0;
    shell.second = (ULONG)(uintptr_t)"C:\\";
    CHECK(aes_shell(&shell) == ERANGE);
    shell.operation = AES_SHELL_DAMAGE; shell.data = shell.second = 0;
    shell.rectangle.g_x = 32767; shell.rectangle.g_w = shell.rectangle.g_h = 1;
    CHECK(aes_shell(&shell) == EINVAL);
    record("x32 desktop rejected blocks and shell copies PASS\r\n");
    h = aes_wind_create(AES_WINDOW_NAME_GADGET | AES_WINDOW_MOVER, &rectangle);
    CHECK(h > 0);
    CHECK(aes_wind_set_text(h, AES_WINDOW_NAME, "Ordinary ring-3 application") == E_OK);
    CHECK(aes_wind_open(h, &rectangle) == E_OK);
    CHECK(aes_evnt_multi(&timer, &input) == AES_EVENT_TIMER);
    CHECK(desk_syscall(1, 0x4b, 0, (long)"C:\\APPTEST.PRG", (long)child_tail, 0) == 0);
    CHECK(aes_wind_get(h, &info) == E_OK && info.handle == h);
    peer = aes_appl_find("DESKACC");
    CHECK(peer > 0);
    CHECK(aes_appl_write(peer, &message) == E_OK);
    do {
        rc = aes_evnt_wait(AES_EVENT_MESSAGE, &event);
        CHECK(rc == AES_EVENT_MESSAGE);
    } while (event.message.type != 651);
    CHECK(event.message.sender == peer);
    control.code = 100; control.nintin = 11; control.handle = 1;
    CHECK(vdi(&pb) == E_OK && control.handle > 0);
    control.code = 101; control.nintin = 0;
    CHECK(vdi(&pb) == E_OK);
    CHECK(aes_wind_close(h) == E_OK);
    CHECK(aes_wind_delete(h) == E_OK);
    /* The longest safe tail reaches the next basepage intact. Rejecting
     * its nonterminated variant must preserve this pending shell request. */
    boundary_tail[0] = 126;
    for (i = 1; i < sizeof(boundary_tail) - 1; i++)
        boundary_tail[i] = 'b';
    boundary_tail[sizeof(boundary_tail) - 1] = 0;
    schedule(boundary_tail);
    boundary_tail[sizeof(boundary_tail) - 1] = 'b';
    shell.operation = AES_SHELL_WRITE; shell.value = shell.extra = shell.bytes = 1;
    shell.rectangle.g_x = shell.rectangle.g_w = shell.rectangle.g_h = 0;
    shell.data = (ULONG)(uintptr_t)"C:\\APPTEST.PRG";
    shell.second = (ULONG)(uintptr_t)boundary_tail;
    CHECK(aes_shell(&shell) == ERANGE);
    shell.second = 1;
    CHECK(aes_shell(&shell) == EIMBA);
    CHECK(aes_appl_exit() == E_OK);
    record("x32 desktop application and accessory PASS\r\n");
    finish(0);
}
