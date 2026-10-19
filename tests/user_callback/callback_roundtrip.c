/* Real ring-3 callbacks, nesting, alternate stacks, revocation and faults. */
#include "config.h"
#include "portab.h"
#include "user_callback.h"
#include "vdi_service.h"
#include "gemerror.h"

int callback_roundtrip_checks(const char *tail);
int callback_register_checks(volatile ULONG *counter);
void callback_badreturn(int ms);
void callback_badstack(void);

static LONG raw(ULONG cls, ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    register UQUAD result __asm__("rax") = ((UQUAD)cls << 32) | fn;
    register UQUAD fourth __asm__("r10") = d;
    __asm__ volatile ("syscall" : "+a"(result)
                      : "D"(a), "S"(b), "d"(c), "r"(fourth)
                      : "rcx", "r11", "memory", "cc");
    return (LONG)result;
}

#if CONF_WITH_USER_CALLBACKS && CONF_WITH_X32_VDI
static WORD handle;
static volatile ULONG ticks, buttons, cursors, wheels, motion_calls;
static volatile int handler_failure;
static USER_CALLBACK_STACK stack;
static ULONG previous_motion;
static char mode;
static ULONG *volatile fault_address;

#define CHECK(expr) do { if (!(expr) && !failure) failure = __LINE__; } while (0)
#define HCHECK(expr) do { if (!(expr) && !handler_failure) handler_failure = __LINE__; } while (0)

static LONG exchange(WORD code, ULONG handler, VDI_SERVICE_VECTOR_RESULT *old)
{
    return raw(VDI_SERVICE_CLASS, VDI_SERVICE_VECTOR, handle, code, handler, (uintptr_t)old);
}

static void handler_checks(void)
{
    USER_CALLBACK_STACK old, disabled = { 0, 0, USER_CALLBACK_SS_DISABLE };
    ULONG rsp;
    UWORD cs;

    __asm__ volatile ("mov %%esp,%0; mov %%cs,%1" : "=r"(rsp), "=r"(cs));
    HCHECK((cs & 3) == 3);
    if (!(stack.ss_flags & USER_CALLBACK_SS_DISABLE)) {
        HCHECK(rsp >= stack.ss_sp && rsp < stack.ss_sp + stack.ss_size);
        HCHECK(Psigaltstack(NULL, &old) == E_OK);
        HCHECK(old.ss_flags == USER_CALLBACK_SS_ONSTACK);
        HCHECK(Psigaltstack(&disabled, NULL) == EACCDN);
        HCHECK(raw(1, 0x49, stack.ss_sp, 0, 0, 0) == EACCDN);
    }
    HCHECK(raw(1, 0x10b, 0, 0, 0, 0) > 0);
    HCHECK(raw(13, 2, 2, 0, 0, 0) == EACCDN); /* blocking Bconin */
    HCHECK(raw(USER_CALLBACK_CLASS, USER_CALLBACK_RETURN, 0, 0, 0, 0) == EACCDN);
}

static void timer(int ms)
{
    if (mode == 'T')
        for (;;)
            __asm__ volatile ("pause");
    if (mode == 'X')
        __asm__ volatile ("cli");
    if (mode == 'S')
        __asm__ volatile ("1: subq $4096,%%rsp; movq $0,(%%rsp); jmp 1b" ::: "memory");
    handler_checks();
    HCHECK(ms > 0 && ms <= 1000);
    ticks++;
    __asm__ volatile ("pxor %%xmm15,%%xmm15" ::: "xmm15");
}

static void button(WORD status)
{
    handler_checks();
    HCHECK(status == 0x1234);
    buttons++;
}

static void cursor(WORD x, WORD y)
{
    handler_checks();
    HCHECK(x == -7 && y == 32767);
    cursors++;
}

static void wheel(WORD n, WORD amount)
{
    handler_checks();
    HCHECK(n == 1 && amount == -3);
    wheels++;
}

static ULONG child_motion(WORD x, WORD y)
{
    handler_checks();
    motion_calls++;
    return MAKE_ULONG(x + 10, y + 20);
}

static ULONG parent_motion(WORD x, WORD y)
{
    VDI_SERVICE_VECTOR_RESULT old;
    ULONG result;

    handler_checks();
    motion_calls++;
    /* The child's vector is OLD here: reenter its process on a second
     * kernel stack while this owner's first callback is still suspended. */
    HCHECK(exchange(126, (uintptr_t)parent_motion, &old) == E_OK);
    previous_motion = old.old_handler;
    result = ((ULONG (*)(WORD, WORD))(uintptr_t)previous_motion)(x, y);
    return MAKE_ULONG(HIWORD(result) + 1, LOWORD(result) + 2);
}

static int prepare_stack(void)
{
    USER_CALLBACK_STACK bad, old;
    LONG allocation;
    int failure = 0;

    CHECK(Psigaltstack(NULL, &old) == E_OK && old.ss_flags == USER_CALLBACK_SS_DISABLE);
    allocation = raw(1, 0x48, 32768, 0, 0, 0);
    if (allocation <= 0)
        return __LINE__;
    stack.ss_sp = allocation;
    stack.ss_size = 32768;
    bad = stack;
    bad.ss_size = 4096;
    CHECK(Psigaltstack(&bad, NULL) == EINVAL);
    bad = stack;
    bad.ss_sp++;
    CHECK(Psigaltstack(&bad, NULL) == EINVAL);
    bad = stack;
    bad.ss_sp = 0x7fff0000;
    CHECK(Psigaltstack(&bad, NULL) == EINVAL);
    bad = stack;
    bad.ss_sp = 0x30000000;
    CHECK(Psigaltstack(&bad, NULL) == EIMBA);
    bad = stack;
    bad.ss_sp = 0xfffffff0UL;
    CHECK(Psigaltstack(&bad, NULL) == EINVAL);
    bad = stack;
    bad.ss_flags = USER_CALLBACK_SS_ONSTACK;
    CHECK(Psigaltstack(&bad, NULL) == EINVAL);
    CHECK(Psigaltstack(&stack, &old) == E_OK && old.ss_flags == USER_CALLBACK_SS_DISABLE);
    CHECK(raw(1, 0x49, stack.ss_sp, 0, 0, 0) == EACCDN);
    CHECK(raw(1, 0x4a, 0, stack.ss_sp, 4096, 0) == EACCDN);
    return failure;
}

static int child_checks(BOOL fault_parent)
{
    VDI_SERVICE_VECTOR_RESULT old, direct;
    int failure = 0;

    if (fault_parent) {
        CHECK(exchange(125, (uintptr_t)button, &old) == E_OK);
        ((void (*)(WORD))(uintptr_t)old.old_handler)(0x1234);
        /* A foreign owner's fault must return here, not unwind this child. */
        CHECK(raw(1, 0x10b, 0, 0, 0, 0) > 0);
        return failure;
    }
    CHECK(exchange(125, (uintptr_t)button, &old) == E_OK);
    ((void (*)(WORD))(uintptr_t)old.old_handler)(0x1234);
    CHECK(exchange(127, (uintptr_t)cursor, &old) == E_OK);
    ((void (*)(WORD, WORD))(uintptr_t)old.old_handler)(-7, 32767);
    CHECK(exchange(134, (uintptr_t)wheel, &old) == E_OK);
    ((void (*)(WORD, WORD))(uintptr_t)old.old_handler)(1, -3);
    CHECK(exchange(126, (uintptr_t)child_motion, &old) == E_OK);
    CHECK(((ULONG (*)(WORD, WORD))(uintptr_t)old.old_handler)(2, 3) == MAKE_ULONG(13, 25));
    CHECK(motion_calls == 1);
    /* Same-owner OLD is the exact typed code address; wrong-signature
     * capability stubs cannot be installed as another callback kind. */
    CHECK(exchange(126, (uintptr_t)child_motion, &direct) == E_OK);
    CHECK(exchange(126, (uintptr_t)child_motion, &direct) == E_OK);
    CHECK(direct.old_handler == (uintptr_t)child_motion);
    CHECK(exchange(125, old.old_handler, &direct) == EIMBA);
    return failure ? failure : handler_failure;
}

static void parent_fault(WORD status)
{
    (void)status;
    *fault_address = 1;
}
#endif

int callback_roundtrip_checks(const char *tail)
{
#if CONF_WITH_USER_CALLBACKS && CONF_WITH_X32_VDI
    VDI_SERVICE_WORKSTATION ws;
    VDI_SERVICE_VECTOR_RESULT old, direct;
    USER_CALLBACK_STACK disabled = { 0, 0, USER_CALLBACK_SS_DISABLE };
    WORD input[11] = { 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 2 };
    int failure = 0;
    LONG rc;

    mode = tail[0];
    CHECK(raw(VDI_SERVICE_CLASS, VDI_SERVICE_OPEN, (uintptr_t)input, (uintptr_t)&ws, 0, 0) == E_OK);
    if (failure)
        return failure;
    handle = ws.handle;
    CHECK(prepare_stack() == 0);
    if (mode == 'J' || mode == '&')
        return child_checks(mode == '&');
    if (mode == '%') {
        CHECK(exchange(125, (uintptr_t)parent_fault, &old) == E_OK);
        rc = raw(1, 0x4b, 0, (uintptr_t)"X32CB.TOS", (uintptr_t)"&", 0);
        /* Deferred termination is checked before this parent resumes user. */
        return rc ? __LINE__ : __LINE__;
    }
    CHECK(exchange(118, mode == 'Z' ? (uintptr_t)callback_badreturn : (uintptr_t)timer, &old) == E_OK && old.tick_ms > 0);
    if (mode == 'Z') {
        CHECK(callback_register_checks(&ticks) == 0);
        return __LINE__;
    }
    if (mode == 'U') {
        CHECK(Psigaltstack(&disabled, NULL) == E_OK);
        stack.ss_flags = USER_CALLBACK_SS_DISABLE;
        callback_badstack();
    }
    CHECK(exchange(118, (uintptr_t)timer, &direct) == E_OK);
    CHECK(direct.old_handler == (uintptr_t)timer);
    ticks = 0;
    CHECK(callback_register_checks(&ticks) == 0);
    CHECK(ticks && !handler_failure);
    if (mode != 'C')
        return __LINE__;                 /* fault modes must not get here */
    CHECK(exchange(118, old.old_handler, &direct) == E_OK);
    CHECK(exchange(125, (uintptr_t)button, &old) == E_OK);
    CHECK(exchange(126, (uintptr_t)parent_motion, &old) == E_OK);
    CHECK(exchange(127, (uintptr_t)cursor, &old) == E_OK);
    CHECK(exchange(134, (uintptr_t)wheel, &old) == E_OK);
    rc = raw(1, 0x4b, 0, (uintptr_t)"X32CB.TOS", (uintptr_t)"J", 0);
    CHECK(rc == 0);
    CHECK(buttons == 1 && cursors == 1 && wheels == 1 && motion_calls == 1);
    CHECK(previous_motion && ((ULONG (*)(WORD, WORD))(uintptr_t)previous_motion)(5, 6) == MAKE_ULONG(5, 6));
    CHECK(!handler_failure);
    CHECK(Psigaltstack(&disabled, NULL) == E_OK);
    CHECK(raw(1, 0x49, stack.ss_sp, 0, 0, 0) == E_OK);
    stack.ss_flags = USER_CALLBACK_SS_DISABLE;
    /* Deliver on the normal stack too, preserving its red zone and state. */
    ticks = 0;
    CHECK(exchange(118, (uintptr_t)timer, &old) == E_OK);
    ticks = 0;
    CHECK(callback_register_checks(&ticks) == 0 && ticks);
    CHECK(!handler_failure);
    return failure;
#else
    (void)tail;
    return raw(USER_CALLBACK_CLASS, USER_CALLBACK_ALTSTACK, 0, 0, 0, 0) == EINVFN ? 0 : __LINE__;
#endif
}
