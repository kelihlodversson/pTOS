/* #1015: a managed THREAD's Pexec(0) child is an anchor, a PROCESS with a
 * scheduler task of its own, as in FreeMiNT. This probe runs inside such a
 * child (the tail "--abi-anchored") and uses what the nested carrier could
 * never offer: a separate Pclone PROCESS it reaps itself, a Pclone sibling
 * THREAD it joins, and Pfork. It needs pclone_trap(). The child's final
 * status is above 255, so the parent sees whether Pexec(0) returns all 16
 * bits of it rather than the low byte a Pwaitpid() status carries. */
#ifndef TEST_PCLONE_ANCHORED_PROBE_H
#define TEST_PCLONE_ANCHORED_PROBE_H

#define ANCHORED_PROBE_TAIL "\016--abi-anchored"
#define ANCHORED_PROBE_STATUS 0x1f15L
#define ANCHORED_PROBE_STACK 16384L

#define ANCHORED_PROBE_REQUESTED(cmdlin) \
    ((unsigned char)(cmdlin)[0] == 14 && !memcmp((cmdlin) + 1, "--abi-anchored", 14))
static volatile int anchored_sibling_ran;
static void anchored_process_entry(void *argument)
{
    (void)argument;
    Pterm(46);
}
#if defined(TEST_WITH_PTHJOIN)
static void anchored_sibling_entry(void *argument)
{
    (void)argument;
    anchored_sibling_ran = 1;
    Pterm(47);
}
#endif
/* Returns ANCHORED_PROBE_STATUS, or the bit set of the checks that failed. */
static long anchored_probe(void)
{
    struct ptos_clone_args args;
    int32_t pid = 0, tid = 0;
    long stack[2], child, result, bad = 0;
    int i;
    if (Pgetppid() <= 0 || Pgetppid() == Pgetpid()) bad |= 1;
    for (i = 0; i < 2; i++) {
        stack[i] = Malloc(ANCHORED_PROBE_STACK);
        if (stack[i] <= 0) return 0x200;
    }
    /* A separate PROCESS sharing this address space, reaped here. */
    memset(&args, 0, sizeof(args));
    args.entry = anchored_process_entry;
    args.stack_base = (void *)stack[0]; args.stack_size = ANCHORED_PROBE_STACK;
    args.child_pid = &pid; args.child_tid = &tid; args.flags = CLONE_VM;
    child = pclone_trap(&args, sizeof(args));
    if (child <= 0 || pid <= 0 || pid == Pgetpid()) bad |= 2;
    else {
        result = Pwaitpid(pid, 0, 0);
        if ((result >> 16) != pid || (result & 65535) != 46) bad |= 4;
    }
#if defined(TEST_WITH_PTHJOIN)
    /* A sibling THREAD of this PROCESS, joined here. */
    {
        int32_t status = -1;
        memset(&args, 0, sizeof(args));
        args.entry = anchored_sibling_entry;
        args.stack_base = (void *)stack[1]; args.stack_size = ANCHORED_PROBE_STACK;
        args.child_tid = &tid; tid = 0;
        args.flags = CLONE_VM | CLONE_SAME_PROCESS;
        child = pclone_trap(&args, sizeof(args));
        if (child <= 0 || tid <= 0) bad |= 8;
        else if (trap_1_wll(GEMDOS_PTHJOIN, tid, (long)&status) != 0 || status != 47 ||
                 !anchored_sibling_ran) bad |= 16;
    }
#endif
#if defined(TEST_WITH_PFORK)
    child = Pfork();
    if (child == 0) Pterm(48);
#if defined(__mc68000__)
    /* m68k admits the return image only on a 68040 with an idle FPU:
     * elsewhere Pfork is refused at run time with EINVFN, a gate, not a
     * nested context (which the Pclone and Pthjoin checks above rule out). */
    if (child == -32L) child = 0;
    else
#endif
    if (child <= 0) bad |= 32;
    if (child > 0) {
        result = Pwaitpid(child, 0, 0);
        if ((result >> 16) != child || (result & 65535) != 48) bad |= 64;
    }
#endif
    if (Pwaitpid(-1, 1, 0) != -33L) bad |= 128;
    for (i = 0; i < 2; i++) if (Mfree((void *)stack[i])) bad |= 256;
    return bad ? bad : ANCHORED_PROBE_STATUS;
}
#endif
