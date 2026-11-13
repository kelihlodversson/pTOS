/* Explicit userspace context ownership, independent of kernel session policy. */
#include <pthread.h>
#include "libaes.h"
#include "gemerror.h"
#include "../../lib/aes/application.h"
#include "../../lib/aes/app_context.h"
#include "../../lib/aes/transport.h"
#include "../../lib/aes/lifecycle.h"

#undef CHECK
#define CHECK(c) do { if (!(c)) { __builtin_printf("AES context failure: %d\n", __LINE__); return 1; } } while (0)
extern void *malloc(__SIZE_TYPE__ bytes);
extern void free(void *memory);

static ULONG allocations, releases, opens, closes, resets;
static BOOL fail_allocate, fail_open, fail_label, fail_tag, fail_close, fail_free;
static LONG pid = 7, next_session = 100;
static BOOL live[4];

UQUAD aes_memory_call(ULONG function, UQUAD argument)
{
    if (function == 0x48) {
        void *memory;
        if (fail_allocate) return (QUAD)ENSMEM;
        memory = malloc(argument);
        if (memory) allocations++;
        return (uintptr_t)memory;
    }
    if (function == 0x49) {
        if (fail_free) return (QUAD)EACCDN;
        releases++; free((void *)(uintptr_t)argument); return E_OK;
    }
    return (QUAD)EINVFN;
}
LONG aes_process_call(ULONG function) { return function == 0x10b ? pid : E_OK; }
LONG cuis_call(ULONG function, LONG session)
{
    if (function == CUIS_SESSION_START) {
        opens++;
        if (fail_open) return ENSMEM;
        live[next_session-100] = TRUE;
        return next_session++;
    }
    if (session < 100 || session >= next_session || !live[session-100]) return EACCDN;
    if (function == CUIS_SESSION_QUERY) return E_OK;
    if (function == CUIS_SESSION_END) {
        if (fail_close) return EACCDN;
        closes++; live[session-100] = FALSE; return E_OK;
    }
    return EINVFN;
}
LONG cuis_call6(ULONG function, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e, UQUAD f)
{
    LONG peer;
    (void)e; (void)f;
    if (function == CUIS_SESSION_NAME) {
        __builtin_memcpy((void *)(uintptr_t)b,"TEST.PRG",9); return 8;
    }
    if (function == CUIS_SESSION_LABEL) return fail_label ? EACCDN : E_OK;
    if (function == CUIS_SESSION_TAG) return fail_tag ? EACCDN : E_OK;
    if (function == CUIS_PEER_NEXT) {
        for (peer = 100; peer < next_session; peer++)
            if (live[peer-100] && (b == ~0ULL || peer > (LONG)b)) return peer;
        return EFILNF;
    }
    if (function == CUIS_PEER_QUERY) {
        if (c == CUIS_PEER_BACKGROUND) return EFILNF;
        if (c == CUIS_PEER_TAG) return APP_APPLICATION;
        if (c == CUIS_PEER_LABEL) {
            __builtin_memcpy((void *)(uintptr_t)d,"TESTAPP ",8); return 8;
        }
    }
    if (function == CUIS_PEER_PID) return a >= 100 && a < (UQUAD)next_session ? 7 : EFILNF;
    return EINVFN;
}
static BOOL fail_resource_cleanup;
static ULONG resource_cleanups;
void aes_menu_reset(AESGLOBAL *global) { (void)global; resets++; }
static ULONG shell_initializations;
void aes_shell_reset(AESGLOBAL *global) { (void)global; }
void aes_shell_init(AES_APP_CONTEXT *context)
{
    shell_initializations++; context->shell.command[0] = 'A';
}
void aes_windows_reset(AESGLOBAL *global) { (void)global; }
void aes_desktop_reset(AESGLOBAL *global) { (void)global; }
void aes_graphics_reset(AESGLOBAL *global) { (void)global; }
BOOL aes_resources_reset(AESGLOBAL *global)
{
    (void)global; resource_cleanups++; return !fail_resource_cleanup;
}

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    unsigned waiting, generation;
} SEARCH_GATE;
typedef struct {
    AESGLOBAL *global;
    SEARCH_GATE *gate;
    BOOL failed;
} SEARCH_TASK;

static void search_gate(SEARCH_GATE *gate)
{
    unsigned generation;
    pthread_mutex_lock(&gate->mutex);
    generation = gate->generation;
    if (++gate->waiting == 2) {
        gate->waiting = 0;
        gate->generation++;
        pthread_cond_broadcast(&gate->condition);
    } else {
        while (generation == gate->generation)
            pthread_cond_wait(&gate->condition,&gate->mutex);
    }
    pthread_mutex_unlock(&gate->mutex);
}

static void *parallel_search(void *argument)
{
    SEARCH_TASK *task = argument;
    unsigned i;
    char name[9];
    short type, id;
    for (i = 0; i < 128; i++) {
        if (mt_appl_search(APP_FIRST,name,&type,&id,task->global) != 1) task->failed = TRUE;
        search_gate(task->gate); /* Both cursors must survive the other's FIRST. */
        if (mt_appl_search(APP_NEXT,name,&type,&id,task->global) != 1 ||
            mt_appl_search(APP_NEXT,name,&type,&id,task->global) != 0) task->failed = TRUE;
        search_gate(task->gate);
    }
    return NULL;
}

static int parallel_checks(AESGLOBAL *first, AESGLOBAL *second)
{
    SEARCH_GATE gate = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0 };
    SEARCH_TASK tasks[2] = { { first,&gate,FALSE }, { second,&gate,FALSE } };
    pthread_t threads[2];
    CHECK(!pthread_create(&threads[0],NULL,parallel_search,&tasks[0]));
    CHECK(!pthread_create(&threads[1],NULL,parallel_search,&tasks[1]));
    CHECK(!pthread_join(threads[0],NULL) && !pthread_join(threads[1],NULL));
    CHECK(!tasks[0].failed && !tasks[1].failed);
    CHECK(!pthread_cond_destroy(&gate.condition) && !pthread_mutex_destroy(&gate.mutex));
    __builtin_printf("AES independent contexts on concurrent host threads: PASS\n");
    return 0;
}

int main(void)
{
    AESGLOBAL first = { 0 }, alias = { 0 }, second = { 0 };
    void *context;
    ULONG before, cleanup_before;
    char name[9];
    short type, id;
    first.ap_private = 0x12345678;
    CHECK(mt_appl_init(NULL) == -1);
    CHECK(!mt_appl_exit(NULL) && !mt_appl_exit(&first) && !releases);
    CHECK(!aes_lifecycle_write(SWM_NEWMSG,NM_APTERM,0,NULL,NULL));
    CHECK(!aes_lifecycle_write(SWM_SHUTDOWN,0,SD_ABORT,NULL,&first));
    fail_allocate = TRUE;
    CHECK(mt_appl_init(&first) == -1 && !first.ap_3resv && !allocations && !opens);
    fail_allocate = FALSE;
    fail_open = TRUE;
    CHECK(mt_appl_init(&first) == -1 && !first.ap_3resv && allocations == releases);
    fail_open = FALSE;
    fail_label = TRUE;
    CHECK(mt_appl_init(&first) == -1 && !first.ap_3resv && allocations == releases && closes == 1);
    fail_label = FALSE;
    fail_tag = TRUE;
    CHECK(mt_appl_init(&first) == -1 && !first.ap_3resv && allocations == releases && closes == 2);
    fail_tag = FALSE;
    CHECK(mt_appl_init(&first) == 7 && first.ap_3resv && first.ap_private == 0x12345678);
    context = first.ap_3resv;
    before = opens;
    CHECK(shell_initializations == 4);
    ((AES_APP_CONTEXT *)context)->shell.command[0] = 'B';
    first.ap_rscmem = (void *)(uintptr_t)0x1000;
    first.ap_rsclen = 42;
    CHECK(mt_appl_init(&first) == 7 && first.ap_3resv == context && opens == before);
    CHECK(first.ap_rscmem == (void *)(uintptr_t)0x1000 && first.ap_rsclen == 42);
    alias.ap_3resv = context;
    alias.ap_private = 99;
    alias.ap_rscmem = (void *)(uintptr_t)0x2000;
    alias.ap_rsclen = 17;
    CHECK(mt_appl_init(&alias) == 7 && alias.ap_3resv == context && opens == before);
    CHECK(alias.ap_private == 99 && alias.ap_rscmem == (void *)(uintptr_t)0x2000 && alias.ap_rsclen == 17);
    CHECK(aes_application_session(&alias) == aes_application_session(&first));
    CHECK(shell_initializations == 4 && ((AES_APP_CONTEXT *)context)->shell.command[0] == 'B');
    CHECK(mt_appl_init(&second) == 7 && second.ap_3resv && second.ap_3resv != context);
    CHECK(aes_application_session(&second) != aes_application_session(&first));
    CHECK(shell_initializations == 5 && ((AES_APP_CONTEXT *)second.ap_3resv)->shell.command[0] == 'A');
    CHECK(mt_appl_search(APP_FIRST,name,&type,&id,&first) == 1);
    CHECK(mt_appl_search(APP_FIRST,name,&type,&id,&second) == 1);
    CHECK(mt_appl_search(APP_NEXT,name,&type,&id,&first) == 1);
    CHECK(mt_appl_search(APP_NEXT,name,&type,&id,&second) == 1);
    CHECK(mt_appl_search(APP_NEXT,name,&type,&id,&alias) == 0);
    CHECK(parallel_checks(&first,&second) == 0);
    CHECK(!aes_lifecycle_listening(&first) && !aes_lifecycle_listening(&second));
    CHECK(aes_lifecycle_write(SWM_NEWMSG,NM_APTERM,0,NULL,&first) == 1);
    CHECK(aes_lifecycle_listening(&alias) && !aes_lifecycle_listening(&second));
    before = releases;
    cleanup_before = resets;
    alias.ap_id = 0;
    CHECK(!mt_appl_exit(&alias) && alias.ap_3resv == context && releases == before && resets == cleanup_before);
    alias.ap_id = 7;
    fail_close = TRUE;
    CHECK(!mt_appl_exit(&alias) && alias.ap_3resv == context && releases == before && resets == cleanup_before);
    fail_close = FALSE;
    fail_resource_cleanup = TRUE;
    before = resource_cleanups;
    CHECK(!mt_appl_exit(&alias) && alias.ap_3resv == context && resource_cleanups == before+1);
    CHECK(resets == cleanup_before+1);
    before = closes;
    CHECK(!mt_appl_exit(&alias) && closes == before && resets == cleanup_before+1);
    fail_resource_cleanup = FALSE;
    before = releases;
    fail_free = TRUE;
    CHECK(!mt_appl_exit(&alias) && alias.ap_3resv == context && releases == before && resets == cleanup_before+1);
    before = closes;
    fail_free = FALSE;
    CHECK(mt_appl_exit(&alias) == 1 && !alias.ap_3resv && alias.ap_id == -1);
    CHECK(closes == before && releases == allocations-1 && alias.ap_private == 99 && resets == cleanup_before+1);
    /* first is now a dangling alias and must never be passed to AES again. */
    first.ap_3resv = NULL;
    CHECK(aes_application_session(&second) >= 0);
    CHECK(mt_appl_exit(&second) == 1 && !second.ap_3resv && allocations == releases);
    __builtin_printf("AES explicit context allocation, sharing, rollback and exit: PASS\n");
    return 0;
}
