/* Bounded cooperative kernel tasks, independent of the legacy AES scheduler.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "ksched.h"
#include "kproc.h"
#include "process.h"
#include "kheap.h"
#include "gemerror.h"
#include "string.h"
#if CONF_WITH_X86_64_MEMTEST
#include "kprint.h"
#endif
extern PD *run;

typedef enum { TASK_UNUSED, TASK_NEW, TASK_RUNNING, TASK_BLOCKED, TASK_DONE, TASK_ZOMBIE } TASK_STATE;
typedef struct {
    PD *program, *parent;
    WORD pid, signal;
    BOOL asynchronous;
    UQUAD stack_phys, stack_top;
    X86_64_PROCESS_CONTEXT context;
    jmp_buf resume;
    TASK_STATE state;
    BOOL (*ready)(void *);
    void *argument;
    LONG result;
    ULONG usage[2];
} KERNEL_TASK;
static struct {
    BOOL active;
    UWORD initial, next;
    KERNEL_TASK *tasks, *current;
    X86_64_PROCESS_CONTEXT root;
    jmp_buf resume;
    UQUAD flags;
} scheduler;
extern void native_sched_enter(UQUAD top, void (*entry)(void)) NORETURN;

BOOL ksched_spawn_allowed(void) { return scheduler.active && scheduler.current != NULL; }

BOOL ksched_pid_reserved(WORD pid)
{
    UWORD i;
    if (!scheduler.active) return FALSE;
    for (i = 0; i < KSCHED_LIMIT; i++)
        if (scheduler.tasks[i].state != TASK_UNUSED && scheduler.tasks[i].pid == pid) return TRUE;
    return FALSE;
}

BOOL ksched_parent_pid(PD *pd, WORD *pid)
{
    UWORD i;
    if (!scheduler.active) return FALSE;
    for (i = scheduler.initial; i < KSCHED_LIMIT; i++) {
        KERNEL_TASK *task = &scheduler.tasks[i];
        if (task->state != TASK_UNUSED && task->program == pd) {
            *pid = kproc_pid(task->parent ? task->parent : scheduler.root.pd);
            return TRUE;
        }
    }
    return FALSE;
}

/* Called before a process's record/basepage is freed, including nested
 * synchronous children. No orphan keeps a pointer to its former parent. */
void ksched_exiting(PD *pd, UWORD result)
{
    UWORD i;
    if (!scheduler.active) return;
    for (i = 0; i < KSCHED_LIMIT; i++) {
        KERNEL_TASK *task = &scheduler.tasks[i];
        if (task->state == TASK_UNUSED) continue;
        if (task->parent == pd) {
            task->parent = NULL;
            if (task->state == TASK_ZOMBIE) task->state = TASK_DONE;
        }
        if (task->program == pd) {
            ULONG usage[8];
            kproc_usage(pd, usage);
            task->usage[0] = usage[1] + usage[3];
            task->usage[1] = 0;
            task->result = result;
        }
    }
}

void ksched_fault(PD *pd, WORD signal)
{
    KERNEL_TASK *task = scheduler.current;
    if (scheduler.active && task && task->program == pd && !task->signal) task->signal = signal;
}

static void task_entry(void)
{
    KERNEL_TASK *task = scheduler.current;
    task->result = proc_start_scheduled(task->program);
    proc_discard_accessory(task->program); /* also consumes a refused start */
#if CONF_WITH_X86_64_MEMTEST
    if (task->asynchronous && !task->parent && task->result)
        kcprintf("orphan PID %d returned %ld\n", task->pid, (long)task->result);
#endif
    task->program = NULL;
    task->state = task->asynchronous && task->parent ? TASK_ZOMBIE : TASK_DONE;
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

static BOOL always_ready(void *argument) { (void)argument; return TRUE; }
LONG ksched_yield(void)
{
    UQUAD flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    ksched_block(always_ready, NULL);
    if (flags & 0x200) __asm__ volatile ("sti" ::: "memory");
    return E_OK;
}

LONG ksched_spawn(PD *pd)
{
    UWORD i;
    KERNEL_TASK *task;
    if (!ksched_spawn_allowed()) return EINVFN;
    if (!kproc_unlaunched(pd) || kproc_user_root(pd)) return EIMBA;
    for (i = 0; i < KSCHED_LIMIT; i++)
        if (scheduler.tasks[i].program == pd) return EACCDN;
    for (i = scheduler.initial; i < KSCHED_LIMIT; i++)
        if (scheduler.tasks[i].state == TASK_UNUSED) break;
    if (i == KSCHED_LIMIT) return EAGAIN;
    task = &scheduler.tasks[i];
    memset(task, 0, sizeof(*task));
    task->stack_phys = kstack_alloc(&task->stack_top);
    if (!task->stack_phys) return ENSMEM;
    /* Allocate the task stack before adopting the image. Preparation maps
     * the stable controller for exit, while ancestor copies retain PPID. */
    if (!kproc_prepare_async(pd, run, scheduler.root.pd)) {
        kstack_free(task->stack_phys);
        memset(task, 0, sizeof(*task));
        return ENSMEM;
    }
    task->pid = kproc_pid(pd);
    task->program = pd;
    task->parent = run;
    task->asynchronous = TRUE;
    kproc_commit_async(pd);
    task->state = TASK_NEW;
    return task->pid;
}

typedef struct { PD *parent; WORD pid; } CHILD_WAIT;
static BOOL child_ready(void *argument)
{
    CHILD_WAIT *wait = argument;
    UWORD i;
    BOOL found = FALSE;
    for (i = scheduler.initial; i < KSCHED_LIMIT; i++) {
        KERNEL_TASK *task = &scheduler.tasks[i];
        if (task->state == TASK_UNUSED || task->parent != wait->parent ||
            (wait->pid > 0 && task->pid != wait->pid)) continue;
        found = TRUE;
        if (task->state == TASK_ZOMBIE) return TRUE;
    }
    return !found;
}

LONG ksched_wait(WORD pid, WORD options, UQUAD address)
{
    CHILD_WAIT wait;
    UQUAD flags;
    UWORD i;
    if (options & ~3) return EINVAL;
    /* No process-group or stopped-process model is implemented yet. */
    if (!pid || pid < -1) return EINVFN;
    if (!ksched_spawn_allowed()) return EFILNF;
    wait.parent = run; wait.pid = pid;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    for (;;) {
        BOOL found = FALSE;
        for (i = scheduler.initial; i < KSCHED_LIMIT; i++) {
            KERNEL_TASK *task = &scheduler.tasks[i];
            ULONG status;
            if (task->state == TASK_UNUSED || task->parent != run ||
                (pid > 0 && task->pid != pid)) continue;
            found = TRUE;
            if (task->state != TASK_ZOMBIE) continue;
            /* Back/copy output outside IRQ exclusion. This path never yields,
             * so the zombie remains owned and cannot change under the copy. */
            if (flags & 0x200) __asm__ volatile ("sti" ::: "memory");
            if (address && !kproc_copy_to_user(address, task->usage, sizeof(task->usage))) return EIMBA;
            kproc_add_child_usage(run, task->usage[0]);
            __asm__ volatile ("cli" ::: "memory");
            status = ((ULONG)(UWORD)task->pid << 16) |
                     (task->signal ? ((ULONG)task->signal << 8) : (task->result & 255));
            task->parent = NULL;
            task->pid = 0;
            task->state = TASK_UNUSED;
            if (flags & 0x200) __asm__ volatile ("sti" ::: "memory");
            return (LONG)status;
        }
        if (!found || (options & 1)) {
            if (flags & 0x200) __asm__ volatile ("sti" ::: "memory");
            return found ? 0 : EFILNF;
        }
        ksched_block(child_ready, &wait);
    }
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
    scheduler.tasks = kalloc(KSCHED_LIMIT * sizeof(*scheduler.tasks));
    if (!scheduler.tasks) return ENSMEM;
    memset(scheduler.tasks, 0, KSCHED_LIMIT * sizeof(*scheduler.tasks));
    for (i = 0; i < count; i++) {
        KERNEL_TASK *task = &scheduler.tasks[i];
        task->program = programs[i];
        task->pid = kproc_pid(programs[i]);
        task->state = TASK_NEW;
        task->stack_phys = kstack_alloc(&task->stack_top);
        if (!task->stack_phys) {
            for (j = 0; j < i; j++) kstack_free(scheduler.tasks[j].stack_phys);
            kfree(scheduler.tasks);
            scheduler.tasks = NULL;
            return ENSMEM;
        }
    }
    scheduler.initial = count;
    scheduler.next = 0;
    scheduler.current = NULL;
    scheduler.active = TRUE;
    x86_64_process_save(&scheduler.root);
    scheduler.flags = scheduler.root.flags;
    scheduler.root.flags &= ~0x200ULL;
    if (setjmp(scheduler.resume)) x86_64_process_restore(&scheduler.root);
    for (;;) {
        BOOL pending = FALSE;
        scheduler.current = NULL;
        /* Reclaim a finished task's stack only from this root stack. */
        for (i = 0; i < KSCHED_LIMIT; i++) {
            KERNEL_TASK *task = &scheduler.tasks[i];
            if (task->state == TASK_DONE || task->state == TASK_ZOMBIE) {
                kstack_free(task->stack_phys);
                task->stack_phys = 0;
                if (task->asynchronous && task->state == TASK_DONE) {
                    task->pid = 0;
                    task->state = TASK_UNUSED;
                }
            }
        }
        for (i = 0; i < KSCHED_LIMIT; i++) {
            UWORD slot = (scheduler.next + i) % KSCHED_LIMIT;
            KERNEL_TASK *task = &scheduler.tasks[slot];
            if (task->state == TASK_UNUSED || task->state == TASK_DONE || task->state == TASK_ZOMBIE) continue;
            pending = TRUE;
            if (task->state == TASK_BLOCKED && !task->ready(task->argument)) continue;
            scheduler.next = (slot + 1) % KSCHED_LIMIT;
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
        __asm__ volatile ("sti; hlt; cli" ::: "memory");
    }
    for (i = 0; i < count; i++) results[i] = scheduler.tasks[i].result;
    kfree(scheduler.tasks);
    scheduler.tasks = scheduler.current = NULL;
    scheduler.active = FALSE;
    scheduler.root.flags = scheduler.flags;
    x86_64_process_restore(&scheduler.root);
    return E_OK;
}
