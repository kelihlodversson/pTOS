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
void callback_lazy_exec(const char *file, const char *tail);

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
static USER_CALLBACK_STACK *oom_output;
static ULONG free_child, free_environment;
static volatile ULONG free_checks_done;
static volatile ULONG quota_calls;
extern const UBYTE callback_quota_handlers[], callback_quota_handlers_end[];

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
    HCHECK(raw(0x101, 7, 0, 0, 0, 0) == EACCDN); /* AES event wait */
    HCHECK(raw(0x101, 8, 0, 0, 0, 0) == EACCDN); /* AES yield */
    HCHECK(raw(13, 2, 2, 0, 0, 0) == EACCDN); /* blocking Bconin */
    HCHECK(raw(USER_CALLBACK_CLASS, USER_CALLBACK_RETURN, 0, 0, 0, 0) == EACCDN);
}

static void timer(int ms)
{
    if (mode == 'T' || mode == '~')
        for (;;)
            __asm__ volatile ("pause");
    if (mode == 'X' || mode == 'Y')
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

static ULONG queued_motion(WORD x, WORD y)
{
    static const WORD expected_x[3] = { 100, 107, 110 };
    static const WORD expected_y[3] = { 100, 109, 113 };

    HCHECK(motion_calls < 3);
    if (motion_calls < 3) {
        HCHECK(x == expected_x[motion_calls] && y == expected_y[motion_calls]);
        motion_calls++;
    }
    /* The kernel test injects a real mouse packet at the first two calls,
     * while this handler is active, and checks the native cursor state. */
    HCHECK(raw(1, 0x10b, 0, 0, 0, 0) > 0);
    return MAKE_ULONG(x + 10, y + 20);
}

static void free_blocks(int ms)
{
    LONG allocation;

    (void)ms;
    if (free_checks_done)
        return;
    HCHECK(raw(1, 0x49, free_child, 0, 0, 0) == EACCDN);
    HCHECK(raw(1, 0x49, free_environment, 0, 0, 0) == EACCDN);
    allocation = raw(1, 0x48, 8192, 0, 0, 0);
    HCHECK(allocation > 0);
    if (allocation > 0) {
        HCHECK(raw(1, 0x49, (1ULL << 32) | (ULONG)allocation, 0, 0, 0) == EACCDN);
        HCHECK(raw(1, 0x4a, 0, (ULONG)allocation, 4096, 0) == E_OK);
        HCHECK(raw(1, 0x49, (ULONG)allocation, 0, 0, 0) == E_OK);
    }
    free_checks_done = 1;
}

static int callback_free_checks(void)
{
    VDI_SERVICE_VECTOR_RESULT old, direct;
    PD *child;
    LONG loaded;
    int i, failure = 0;

    for (i = 0; i < 2; i++) {
        loaded = raw(1, 0x4b, i ? PE_BASEPAGE : PE_LOAD,
                     i ? 0 : (uintptr_t)"X32HELLO.TOS", (uintptr_t)"", 0);
        if (loaded <= 0)
            return __LINE__;
        child = (PD *)(uintptr_t)(ULONG)loaded;
        free_child = (ULONG)loaded;
        free_environment = (ULONG)(uintptr_t)child->p_env;
        free_checks_done = 0;
        CHECK(exchange(118, (uintptr_t)free_blocks, &old) == E_OK);
        CHECK(callback_register_checks(&free_checks_done) == 0);
        CHECK(free_checks_done && !handler_failure);
        CHECK(exchange(118, old.old_handler, &direct) == E_OK);
        /* Denial must leave the child and its inherited references intact:
         * the same release succeeds after callback delivery has finished. */
        CHECK(raw(1, 0x49, free_child, 0, 0, 0) == E_OK);
        CHECK(raw(1, 0x49, free_environment, 0, 0, 0) == E_OK);
    }
    return failure;
}

static int native_old_checks(void)
{
    VDI_SERVICE_VECTOR_RESULT old_cursor, old_wheel, old;
    int failure = 0;

    CHECK(exchange(127, (uintptr_t)cursor, &old_cursor) == E_OK);
    CHECK(exchange(134, (uintptr_t)wheel, &old_wheel) == E_OK);
    /* Same ABI, different native meaning: reject without changing either
     * installed handler or the caller's OLD output. */
    old.old_handler = 0x12345678;
    old.tick_ms = 123;
    old.reserved = 456;
    CHECK(exchange(134, old_cursor.old_handler, &old) == EIMBA);
    CHECK(old.old_handler == 0x12345678 && old.tick_ms == 123 && old.reserved == 456);
    CHECK(exchange(127, old_wheel.old_handler, &old) == EIMBA);
    CHECK(old.old_handler == 0x12345678 && old.tick_ms == 123 && old.reserved == 456);
    CHECK(exchange(127, old_cursor.old_handler, &old) == E_OK);
    CHECK(old.old_handler == (uintptr_t)cursor);
    CHECK(exchange(134, old_wheel.old_handler, &old) == E_OK);
    CHECK(old.old_handler == (uintptr_t)wheel);
    /* Repeated native exchanges preserve the exact original capabilities. */
    CHECK(exchange(127, 0, &old) == E_OK && old.old_handler == old_cursor.old_handler);
    CHECK(exchange(134, 0, &old) == E_OK && old.old_handler == old_wheel.old_handler);
    return failure;
}

static ULONG quota_handler(ULONG index)
{
    return (ULONG)(uintptr_t)callback_quota_handlers + index * 16;
}

static void quota_parent(WORD status)
{
    VDI_SERVICE_VECTOR_RESULT old = { 0x12345678, 123, 456 };
    ULONG i;

    HCHECK(status == quota_calls);
    quota_calls++;
    if (status < USER_CALLBACK_CAPS - 1) {
        HCHECK(exchange(125, (uintptr_t)quota_parent, &old) == E_OK);
        HCHECK(old.old_handler == USER_CALLBACK_IMAGE + USER_CALLBACK_STUBS +
               ((ULONG)status + 1) * USER_CALLBACK_STUB_SIZE);
    } else if (status == USER_CALLBACK_CAPS - 1) {
        /* All OLD slots are occupied. Failure with an existing target must
         * retain it, and failures with new targets must return their quota. */
        HCHECK(exchange(125, (uintptr_t)quota_parent, &old) == ENSMEM);
        for (i = 0; i < USER_CALLBACK_CAPS; i++)
            HCHECK(exchange(125, quota_handler(i), &old) == ENSMEM);
        HCHECK(old.old_handler == 0x12345678 && old.tick_ms == 123 && old.reserved == 456);
    }
    HCHECK(raw(1, 0x10b, 0, 0, 0, 0) > 0);
}

static int quota_checks(BOOL child)
{
    VDI_SERVICE_VECTOR_RESULT old, direct, native;
    ULONG i, parent = 0;
    int failure = 0;

    CHECK(callback_quota_handlers_end - callback_quota_handlers == USER_CALLBACK_CAPS * 32);
    if (child) {
        for (i = 0; i < USER_CALLBACK_CAPS; i++) {
            CHECK(exchange(125, quota_handler(i), &old) == E_OK);
            if (!i)
                parent = old.old_handler;
            CHECK(old.old_handler == parent);
            if (failure)
                return failure;
            ((void (*)(WORD))(uintptr_t)parent)((WORD)i);
        }
        /* Failed exchanges left this child's last handler installed. Restore
         * the original parent capability and prove it still delivers. */
        CHECK(exchange(125, parent, &direct) == E_OK);
        CHECK(direct.old_handler == quota_handler(USER_CALLBACK_CAPS - 1));
        ((void (*)(WORD))(uintptr_t)parent)(USER_CALLBACK_CAPS);
    } else {
        CHECK(exchange(125, (uintptr_t)quota_parent, &native) == E_OK);
        CHECK(raw(1, 0x4b, PE_LOADGO, (uintptr_t)"X32CB.TOS", (uintptr_t)"+", 0) == E_OK);
        CHECK(quota_calls == USER_CALLBACK_CAPS + 1 && !handler_failure);
        /* The full OLD table must not prevent same-owner exchanges. Exactly
         * 63 registration slots remain after the one original handler. Use
         * fresh addresses: retrying failed targets could reuse leaked records. */
        for (i = 0; i < USER_CALLBACK_CAPS - 1; i++) {
            CHECK(exchange(125, quota_handler(USER_CALLBACK_CAPS + i), &old) == E_OK);
            CHECK(old.old_handler == (i ? quota_handler(USER_CALLBACK_CAPS + i - 1) :
                                     (uintptr_t)quota_parent));
        }
        old.old_handler = 0x12345678;
        CHECK(exchange(125, quota_handler(2 * USER_CALLBACK_CAPS - 1), &old) == ENSMEM);
        CHECK(old.old_handler == 0x12345678);
        CHECK(exchange(125, (uintptr_t)quota_parent, &old) == E_OK);
        CHECK(old.old_handler == quota_handler(2 * USER_CALLBACK_CAPS - 2));
        CHECK(exchange(125, native.old_handler, &old) == E_OK);
        CHECK(old.old_handler == (uintptr_t)quota_parent);
    }
    return failure;
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

static int loaded_stack_checks(void)
{
    USER_CALLBACK_STACK candidate, old;
    PD *child;
    ULONG env;
    LONG loaded, allocation;
    char *environment;
    int i, j, failure = 0;

    for (i = 0; i < 2; i++) {
        loaded = raw(1, 0x4b, PE_LOAD, (uintptr_t)"X32HELLO.TOS", (uintptr_t)"", 0);
        if (loaded <= 0)
            return __LINE__;
        child = (PD *)(uintptr_t)(ULONG)loaded;
        env = (ULONG)(uintptr_t)child->p_env;
        /* The image's writable startup slack belongs to the loader now,
         * but freeing/launching this child can release/transfer its pages. */
        candidate.ss_sp = ((ULONG)(uintptr_t)child->p_bbase + child->p_blen + 4095UL) & ~4095UL;
        candidate.ss_size = USER_CALLBACK_MIN_STACK;
        candidate.ss_flags = 0;
        CHECK(Psigaltstack(&candidate, NULL) == EACCDN);
        CHECK(Psigaltstack(NULL, &old) == E_OK && old.ss_sp == stack.ss_sp &&
              old.ss_size == stack.ss_size && old.ss_flags == 0);
        if (i) {
            CHECK(raw(1, 0x4b, PE_GOTHENFREE, 0, loaded, 0) == E_OK);
        } else {
            CHECK(raw(1, 0x49, loaded, 0, 0, 0) == E_OK);
            CHECK(raw(1, 0x49, env, 0, 0, 0) == E_OK);
        }
        CHECK(Psigaltstack(NULL, &old) == E_OK && old.ss_sp == stack.ss_sp);
    }
    /* A large child environment is another writable caller grant that
     * Mfree can revoke without going through the ordinary heap pin check. */
    allocation = raw(1, 0x48, 32752, 0, 0, 0);
    if (allocation <= 0)
        return __LINE__;
    environment = (char *)(uintptr_t)(ULONG)allocation;
    environment[0] = 'X';
    environment[1] = '=';
    for (j = 2; j < 32749; j++)
        environment[j] = 'a';
    environment[32749] = environment[32750] = 0;
    loaded = raw(1, 0x4b, PE_BASEPAGE, 0, (uintptr_t)"", (uintptr_t)environment);
    if (loaded <= 0)
        return __LINE__;
    child = (PD *)(uintptr_t)(ULONG)loaded;
    env = (ULONG)(uintptr_t)child->p_env;
    candidate.ss_sp = env;
    CHECK(Psigaltstack(&candidate, NULL) == EACCDN);
    CHECK(raw(1, 0x49, loaded, 0, 0, 0) == E_OK);
    CHECK(raw(1, 0x49, env, 0, 0, 0) == E_OK);
    CHECK(raw(1, 0x49, allocation, 0, 0, 0) == E_OK);
    CHECK(Psigaltstack(NULL, &old) == E_OK && old.ss_sp == stack.ss_sp);
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
    if (mode == 'O')
        Psigaltstack(NULL, oom_output);  /* backing this output is forced to fail */
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
    if (mode == 'K' || mode == '+')
        return quota_checks(mode == '+');
    if (mode == 'J' || mode == '&')
        return child_checks(mode == '&');
    if (mode == 'R') {
        USER_CALLBACK_STACK current;
        ULONG attempts = 100000;

        CHECK(exchange(126, (uintptr_t)queued_motion, &old) == E_OK);
        while (motion_calls < 3 && attempts--)
            CHECK(Psigaltstack(NULL, &current) == E_OK);
        CHECK(motion_calls == 3 && !handler_failure);
        CHECK(exchange(126, old.old_handler, &direct) == E_OK);
        return failure;
    }
    if (mode == '%' || mode == 'Q' || mode == 'O') {
        CHECK(exchange(125, (uintptr_t)parent_fault, &old) == E_OK);
        if (mode == 'Q') {
            CHECK(Psigaltstack(&disabled, NULL) == E_OK);
            stack.ss_flags = USER_CALLBACK_SS_DISABLE;
            /* Keep the callback frame just below the last backed normal
             * stack page, so the entry copy must allocate a lazy page. */
            callback_lazy_exec("X32CB.TOS", "&");
        }
        if (mode == 'O') {
            rc = raw(1, 0x48, 4096, 0, 0, 0);
            if (rc <= 0)
                return __LINE__;
            oom_output = (USER_CALLBACK_STACK *)(uintptr_t)(ULONG)rc;
        }
        rc = raw(1, 0x4b, 0, (uintptr_t)"X32CB.TOS", (uintptr_t)"&", 0);
        /* Deferred termination is checked before this parent resumes user. */
        return __LINE__;
    }
    if (mode == 'C') {
        failure = loaded_stack_checks();
        if (failure)
            return failure;
        failure = callback_free_checks();
        if (failure)
            return failure;
        failure = native_old_checks();
        if (failure)
            return failure;
    }
    CHECK(exchange(118, mode == 'Z' ? (uintptr_t)callback_badreturn : (uintptr_t)timer, &old) == E_OK && old.tick_ms > 0);
    if (mode == 'Y' || mode == '~') {
        /* The kernel harness drains old events before this registration.
         * Make no more syscalls: only an actual PIT IRQ can deliver it. */
        CHECK(callback_register_checks(&ticks) == 0);
        return __LINE__;
    }
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
#if CONF_WITH_USER_CALLBACKS
    {
        USER_CALLBACK_STACK old;

        return raw(USER_CALLBACK_CLASS, USER_CALLBACK_ALTSTACK, 0, (uintptr_t)&old, 0, 0) == E_OK &&
            old.ss_flags == USER_CALLBACK_SS_DISABLE ? 0 : __LINE__;
    }
#else
    return raw(USER_CALLBACK_CLASS, USER_CALLBACK_ALTSTACK, 0, 0, 0, 0) == EINVFN ? 0 : __LINE__;
#endif
#endif
}
