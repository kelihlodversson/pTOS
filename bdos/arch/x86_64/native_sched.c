/* Bounded cooperative kernel tasks, independent of the legacy AES scheduler.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "ksched.h"
#include "kproc.h"
#include "process.h"
#include "kheap.h"
#include "gemerror.h"
#include "string.h"
#include "kprint.h"

typedef enum { TASK_NEW, TASK_RUNNING, TASK_BLOCKED, TASK_DONE } TASK_STATE;
typedef struct {
    PD *program;
    UQUAD stack_phys, stack_top;
    X86_64_PROCESS_CONTEXT context;
    jmp_buf resume;
    TASK_STATE state;
    BOOL (*ready)(void *);
    void *argument;
    LONG result;
} KERNEL_TASK;
static struct {
    BOOL active;
    UWORD count, next;
    KERNEL_TASK *tasks, *current;
    X86_64_PROCESS_CONTEXT root;
    jmp_buf resume;
    UQUAD flags;
} scheduler;
extern void native_sched_enter(UQUAD top, void (*entry)(void)) NORETURN;

static void task_entry(void)
{
    KERNEL_TASK *task = scheduler.current;
    task->result = proc_start_scheduled(task->program);
    proc_discard_accessory(task->program); /* also consumes a refused start */
    task->program = NULL;
    task->state = TASK_DONE;
    /* Normalise GS/CR3 before transferring to another kernel stack. */
    x86_64_process_save(&task->context);
    longjmp(scheduler.resume, 1);
}

BOOL ksched_block(BOOL (*ready)(void *), void *argument)
{
    KERNEL_TASK *task = scheduler.current;
    if (!scheduler.active || !task) return FALSE;
    task->ready = ready;
    task->argument = argument;
    task->state = TASK_BLOCKED;
    x86_64_process_save(&task->context);
    if (!setjmp(task->resume)) longjmp(scheduler.resume, 1);
    task->ready = NULL;
    task->argument = NULL;
    return TRUE;
}

LONG ksched_run(PD * const *programs, UWORD count, LONG *results)
{
    UWORD i, j;
    if (scheduler.active) return EAGAIN;
    if (!count || count > KSCHED_LIMIT) return EINVAL;
    for (i = 0; i < count; i++) {
        if (!kproc_unlaunched(programs[i]) || !kproc_user_root(programs[i])) return EIMBA;
        for (j = 0; j < i; j++) if (programs[i] == programs[j]) return EINVAL;
    }
    scheduler.tasks = kalloc(count * sizeof(*scheduler.tasks));
    if (!scheduler.tasks) return ENSMEM;
    memset(scheduler.tasks, 0, count * sizeof(*scheduler.tasks));
    for (i = 0; i < count; i++) {
        scheduler.tasks[i].program = programs[i];
        scheduler.tasks[i].stack_phys = kstack_alloc(&scheduler.tasks[i].stack_top);
        if (!scheduler.tasks[i].stack_phys) {
            for (j = 0; j < i; j++) kstack_free(scheduler.tasks[j].stack_phys);
            kfree(scheduler.tasks);
            scheduler.tasks = NULL;
            return ENSMEM;
        }
    }
    scheduler.count = count;
    scheduler.next = 0;
    scheduler.current = NULL;
    scheduler.active = TRUE;
    x86_64_process_save(&scheduler.root);
    scheduler.flags = scheduler.root.flags;
    scheduler.root.flags &= ~0x200ULL; /* dispatch and predicate checks stay atomic */
    if (setjmp(scheduler.resume)) x86_64_process_restore(&scheduler.root);
    for (;;) {
        BOOL pending = FALSE;
        scheduler.current = NULL;
        for (i = 0; i < scheduler.count; i++) {
            UWORD slot = (scheduler.next + i) % scheduler.count;
            KERNEL_TASK *task = &scheduler.tasks[slot];
            if (task->state == TASK_DONE) continue;
            pending = TRUE;
            if (task->state == TASK_BLOCKED && !task->ready(task->argument)) continue;
            scheduler.next = (slot + 1) % scheduler.count;
            scheduler.current = task;
            if (task->state == TASK_NEW) {
                task->state = TASK_RUNNING;
                native_sched_enter(task->stack_top, task_entry);
            }
            task->state = TASK_RUNNING;
            x86_64_process_restore(&task->context);
            longjmp(task->resume, 1);
        }
        if (!pending) break;
        /* All remaining tasks are blocked. A pending IRQ cannot be lost
         * between the predicate pass and sleeping on this root stack. */
        __asm__ volatile ("sti; hlt; cli" ::: "memory");
    }
    for (i = 0; i < count; i++) {
        results[i] = scheduler.tasks[i].result;
        kstack_free(scheduler.tasks[i].stack_phys);
    }
    kfree(scheduler.tasks);
    scheduler.tasks = scheduler.current = NULL;
    scheduler.active = FALSE;
    scheduler.root.flags = scheduler.flags;
    x86_64_process_restore(&scheduler.root);
    return E_OK;
}
