/* Real ring-3 registration, queue isolation, event delivery and ownership. */
#include "config.h"
#include "aes_client.h"
#include "aes_objects.h"
#include "aes_shell.h"
#include "aes_service.h"
#include "gemerror.h"
#include "vdipb.h"
#include "bdosdefs.h"

int aes_local_checks(void);
int aes_roundtrip_checks(const char *tail, const char *child_path);

static LONG raw(ULONG cls, ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    register UQUAD rax __asm__("rax") = ((UQUAD)cls << 32) | fn;
    register UQUAD rdi __asm__("rdi") = a;
    register UQUAD rsi __asm__("rsi") = b;
    register UQUAD rdx __asm__("rdx") = c;
    register UQUAD r10 __asm__("r10") = d;

    __asm__ volatile ("syscall" : "+r"(rax)
                      : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10)
                      : "rcx", "r11", "memory", "cc");
    return (LONG)rax;
}

#if CONF_WITH_X32_AES || defined(AES_IMPORT_TEST)
static const AES_APP_INFO immutable_info = { 0 };
static const AES_EVENT immutable_event = { 0 };
static WORD *volatile fault_address;

#define CHECK(expr) do { if (!(expr) && !failure) failure = __LINE__; } while (0)

static int edge_buffer_checks(void)
{
    AES_APP_INFO info = { 0 };
    AES_CLIENT_MESSAGE message = { 77, 999, 0, { 42 } }, received;
    UBYTE *buffer;
    LONG allocation;
    int failure = 0;

    allocation = raw(1, 0x48, 4096, 0, 0, 0);
    if (allocation <= 0)
        return __LINE__;
    buffer = (UBYTE *)(uintptr_t)(ULONG)allocation;
    buffer[4091] = 'E';
    buffer[4092] = 'D';
    buffer[4093] = 'G';
    buffer[4094] = 'E';
    buffer[4095] = 0;
    CHECK(aes_appl_init("BOUND", (AES_APP_INFO *)(buffer + 4092)) == EIMBA);
    CHECK(buffer[4092] == 'D' && !buffer[4095]);
    /* The next heap page is unreserved: a bounded string read must stop
     * at this terminator, while output records require their full span. */
    CHECK(aes_appl_init((const char *)buffer + 4091, &info) == E_OK);
    buffer[4091] = 'X';                  /* the kernel kept its own name copy */
    CHECK(aes_appl_find("EDGE") == info.id);
    CHECK(aes_appl_write(info.id, &message) == E_OK);
    CHECK(aes_appl_read((AES_CLIENT_MESSAGE *)(buffer + 4088)) == EIMBA);
    CHECK(aes_evnt_poll(AES_EVENT_MESSAGE, (AES_EVENT *)(buffer + 4088)) == EIMBA);
    CHECK(aes_appl_read(&received) == E_OK && received.data[0] == 42 && received.sender == info.id);
    CHECK(aes_appl_exit() == E_OK);
    CHECK(raw(1, 0x49, (uintptr_t)buffer, 0, 0, 0) == E_OK);
    return failure;
}

static int child_checks(char mode)
{
    AES_APP_INFO info;
    AES_CLIENT_MESSAGE message = { 77, -123, 0, { 0 } };
    LONG parent;
    int failure = 0;

    CHECK(aes_appl_read(&message) == EACCDN);
    CHECK(aes_appl_init("PARENT", &info) == EACCDN);
    CHECK(aes_appl_init("CHILD", &info) == E_OK);
    CHECK(aes_appl_find("child") == info.id);
    parent = aes_appl_find("PARENT");
    CHECK(parent > 0 && parent != info.id);
    CHECK(aes_appl_read(&message) == EAGAIN);
    CHECK(raw(AES_SERVICE_CLASS, AES_SERVICE_EXIT, parent, 0, 0, 0) == EINVAL);
    /* There is no recipient argument for receive. A foreign id cannot
     * select another process's queue or application state. */
    CHECK(raw(AES_SERVICE_CLASS, AES_SERVICE_READ, parent, 0, 0, 0) == EIMBA);
    CHECK(aes_wind_update(AES_END_UPDATE) == EACCDN);
    if (mode == 'c') {
        CHECK(aes_wind_update(AES_BEG_UPDATE) == EAGAIN);
        CHECK(aes_wind_update(AES_BEG_MCTRL) == EAGAIN);
    } else {
        CHECK(aes_wind_update(AES_BEG_UPDATE) == E_OK);
        CHECK(aes_wind_update(AES_BEG_MCTRL) == E_OK);
    }
    message.data[0] = info.id;
    message.data[1] = mode;
    CHECK(aes_appl_write((WORD)parent, &message) == E_OK);
    if (failure)
        return failure;
    if (mode == 'f')
        *fault_address = 1;
    if (mode == 'e')
        return 0;                       /* normal process exit must clean up */
    CHECK(aes_appl_exit() == E_OK);
    CHECK(aes_appl_exit() == EACCDN);
    {
        AES_OBJECT_REQUEST request = { 0 };
        AES_OBJECT_RESULT result;
        request.operation = AES_OBJECT_METRICS;
        CHECK(aes_object(&request, &result) == EACCDN);
        CHECK(aes_form_do(0, 0) == EACCDN);
        CHECK(aes_tree_validate(NULL, 0, NULL) == EINVAL);
    }
    CHECK(aes_wind_update(AES_BEG_UPDATE) == EACCDN);
    return failure;
}
#endif

int aes_roundtrip_checks(const char *tail, const char *child_path)
{
#if CONF_WITH_X32_AES || defined(AES_IMPORT_TEST)
    AES_APP_INFO info, saved;
    AES_CLIENT_MESSAGE message = { 77, 999, 0, { 10, 11, 12, 13, 14 } }, received;
    AES_EVENT event;
    AES_EVENT_REQUEST input_request = { AES_EVENT_TIMER };
    AES_INPUT_EVENT input_event;
    AES_WINDOW_INFO window_info;
    GRECT window_rect = { 20, 40, 320, 200 }, calculated;
    WORD id, child;
    const char modes[] = { 'c', 'c', 'n', 'e', 'f' };
    char child_tail[3] = { '$', 0, 0 };
    LONG rc;
    int i, failure = 0;

    if (tail[0] == '$')
        return child_checks(tail[1]);
    CHECK(aes_local_checks() == 0);
    CHECK(vdi((VDIPB *)0) == EIMBA);     /* simultaneous VDI + AES imports */
    CHECK(raw(2, 200, 0, 0, 0, 0) == -1);
    CHECK(aes_appl_find("PARENT") == EACCDN);
    CHECK(aes_appl_exit() == EACCDN);
    CHECK(aes_appl_yield() == EACCDN);
    CHECK(aes_shell(NULL) == EACCDN);
    CHECK(aes_evnt_wait(AES_EVENT_MESSAGE, &event) == EACCDN);
    CHECK(aes_appl_init(NULL, &info) == EIMBA);
    CHECK(aes_appl_init("", &info) == EINVAL);
    CHECK(aes_appl_init("TOO_LONG9", &info) == EINVAL);
    CHECK(aes_appl_init("BAD NAME", &info) == EINVAL);
    CHECK(aes_appl_init("PARENT", NULL) == EIMBA);
    CHECK(aes_appl_init("PARENT", (AES_APP_INFO *)&immutable_info) == EIMBA);
    CHECK(aes_appl_init("PARENT", (AES_APP_INFO *)((UBYTE *)&info + 1)) == EIMBA);
    CHECK(raw(AES_SERVICE_CLASS, AES_SERVICE_INIT, (uintptr_t)"PARENT", 0xffff800000000000ULL, 0, 0) == EIMBA);
    CHECK(raw(AES_SERVICE_CLASS, AES_SERVICE_INIT, (uintptr_t)"PARENT", (uintptr_t)&info, 1, 0) == EINVAL);
    CHECK(edge_buffer_checks() == 0);
    CHECK(aes_appl_init("PARENT", &info) == E_OK);
    /* Ordinary Pexec clients have no AES coroutine to block or yield. */
    CHECK(aes_evnt_wait(AES_EVENT_MESSAGE, &event) == EACCDN);
    CHECK(aes_appl_yield() == EACCDN);
    id = info.id;
    CHECK(aes_evnt_multi(&input_request, &input_event) == EACCDN);
    CHECK(aes_wind_create(AES_WINDOW_NAME_GADGET, &window_rect) == EACCDN);
    CHECK(aes_wind_open(1, &window_rect) == EACCDN);
    CHECK(aes_wind_close(1) == EACCDN);
    CHECK(aes_wind_delete(1) == EACCDN);
    CHECK(aes_wind_get(0, &window_info) == EACCDN);
    CHECK(aes_wind_set_rect(1, &window_rect) == EACCDN);
    CHECK(aes_wind_set_text(1, AES_WINDOW_NAME, "title") == EACCDN);
    CHECK(aes_wind_top(1) == EACCDN);
    CHECK(aes_wind_slider(1, AES_WINDOW_HSLIDE, 100) == EACCDN);
    CHECK(aes_wind_rect(1, 0, &calculated) == EACCDN);
    CHECK(aes_wind_find(0, 0) == EACCDN);
    CHECK(aes_wind_calc(AES_WINDOW_WORK, AES_WINDOW_NAME_GADGET,
                        &window_rect, &calculated) == EACCDN);
    CHECK(aes_evnt_multi((AES_EVENT_REQUEST *)1, &input_event) == EIMBA);
    CHECK(raw(AES_SERVICE_CLASS, AES_SERVICE_MULTI, (uintptr_t)&input_request,
              (uintptr_t)&input_event, 1, 0) == EINVAL);
    saved = info;
    CHECK(id > 0 && info.version == AES_CLIENT_VERSION && !info.reserved);
    CHECK(info.queue_capacity == AES_MESSAGE_QUEUE);
    CHECK(info.features == (AES_FEATURE_MESSAGE | AES_FEATURE_UPDATE));
    info.id = -1;                       /* no authority in writable client data */
    CHECK(aes_appl_init("parent", &info) == E_OK && info.id == id);
    CHECK(aes_appl_init("OTHER", &info) == EACCDN);
    CHECK(aes_appl_find("PARENT") == id);
    CHECK(aes_appl_find("ABSENT") == ESRCH);
    CHECK(aes_appl_find((const char *)0x30000000UL) == EIMBA);
    CHECK(raw(AES_SERVICE_CLASS, AES_SERVICE_FIND, 0x100000000ULL + (uintptr_t)"PARENT", 0, 0, 0) == EIMBA);
    CHECK(raw(AES_SERVICE_CLASS, 999, 0, 0, 0, 0) == EINVFN);
    CHECK(aes_evnt_poll(0, &event) == EINVAL);
    CHECK(aes_evnt_poll(0xffff, &event) == EINVAL);
    CHECK(aes_evnt_poll(AES_EVENT_MESSAGE, &event) == 0 && !event.events && !event.reserved);
    CHECK(aes_appl_read(&received) == EAGAIN);
    CHECK(aes_appl_write(id, &message) == E_OK);
    CHECK(aes_evnt_poll(AES_EVENT_MESSAGE, (AES_EVENT *)&immutable_event) == EIMBA);
    CHECK(aes_appl_read(NULL) == EIMBA);
    CHECK(aes_evnt_poll(AES_EVENT_MESSAGE, &event) == AES_EVENT_MESSAGE);
    CHECK(event.events == AES_EVENT_MESSAGE && event.message.sender == id && event.message.data[4] == 14);
    CHECK(!event.reserved && !event.message.reserved);
    for (i = 0; i < AES_MESSAGE_QUEUE; i++) {
        message.data[0] = i;
        CHECK(aes_appl_write(id, &message) == E_OK);
    }
    CHECK(aes_appl_write(id, &message) == EAGAIN);
    for (i = 0; i < AES_MESSAGE_QUEUE; i++) {
        CHECK(aes_appl_read(&received) == E_OK);
        CHECK(received.sender == id && received.data[0] == i);
    }
    CHECK(aes_appl_read(&received) == EAGAIN);
    CHECK(aes_appl_write(id, NULL) == EIMBA);
    CHECK(raw(AES_SERVICE_CLASS, AES_SERVICE_WRITE, 0x100000000ULL + id, (uintptr_t)&message, 0, 0) == EINVAL);
    message.reserved = 1;
    CHECK(aes_appl_write(id, &message) == EINVAL);
    message.reserved = 0;
    message.type = -1;
    CHECK(aes_appl_write(id, &message) == EINVAL);
    message.type = 77;
    CHECK(aes_wind_update(-1) == EINVAL);
    CHECK(aes_wind_update(AES_END_UPDATE) == EACCDN);
    CHECK(aes_wind_update(AES_BEG_UPDATE) == E_OK);
    CHECK(aes_wind_update(AES_BEG_UPDATE) == E_OK);
    CHECK(aes_wind_update(AES_BEG_MCTRL) == E_OK);
    if (failure)
        return failure;
    for (i = 0; i < (int)sizeof(modes); i++) {
        child_tail[1] = modes[i];
        rc = raw(1, 0x4b, PE_LOADGO, (uintptr_t)child_path, (uintptr_t)child_tail, 0);
        CHECK(rc == (modes[i] == 'f' ? 0xffff : 0));
        CHECK(aes_appl_find("CHILD") == ESRCH);
        CHECK(aes_appl_find("PARENT") == id);
        CHECK(aes_evnt_poll(AES_EVENT_MESSAGE, &event) == AES_EVENT_MESSAGE);
        child = event.message.data[0];
        CHECK(child > 0 && child != id && event.message.sender == child && event.message.data[1] == modes[i]);
        CHECK(aes_appl_write(child, &message) == ESRCH);
        if (i == 0)
            CHECK(aes_wind_update(AES_END_UPDATE) == E_OK);
        if (i == 1) {
            CHECK(aes_wind_update(AES_END_UPDATE) == E_OK);
            CHECK(aes_wind_update(AES_END_MCTRL) == E_OK);
        }
        if (i >= 2) {
            CHECK(aes_wind_update(AES_BEG_UPDATE) == E_OK);
            CHECK(aes_wind_update(AES_END_UPDATE) == E_OK);
            CHECK(aes_wind_update(AES_BEG_MCTRL) == E_OK);
            CHECK(aes_wind_update(AES_END_MCTRL) == E_OK);
        }
        if (failure)
            return failure;
    }
    CHECK(aes_wind_update(AES_BEG_UPDATE) == E_OK);
    CHECK(aes_appl_exit() == E_OK);
    CHECK(aes_appl_read(&received) == EACCDN);
    CHECK(aes_appl_init("PARENT", &info) == E_OK && info.id == saved.id);
    CHECK(aes_wind_update(AES_END_UPDATE) == EACCDN);
    CHECK(aes_appl_read(&received) == EAGAIN);
    CHECK(aes_appl_exit() == E_OK);
    return failure;
#undef CHECK
#else
    (void)tail;
    (void)child_path;
    return aes_local_checks() || aes_appl_exit() != EINVFN ||
        raw(AES_SERVICE_CLASS, AES_SERVICE_INIT, 0, 0, 0, 0) != EINVFN;
#endif
}
