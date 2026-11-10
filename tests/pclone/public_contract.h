/* Included only by the managed, real-trap Pclone suite on each MMU target.
 * No kernel calls or modeled child publication stand in for these assertions. */
static int phase1_contract_exercised;
static unsigned phase1_rejection_case;
static volatile unsigned phase1_entered;
static volatile unsigned long phase1_argument;
static volatile unsigned long phase1_expected_argument;
static volatile long phase1_pid;
static unsigned phase1_resources;
static int phase1_resource_error;
static char phase1_parent_path[128], phase1_child_path[128];

#if defined(__x86_64__)
volatile unsigned long long clone_abi_argument;
void clone_abi_entry(void *argument);
#define phase1_entry clone_abi_entry
#else
#define phase1_entry clone_abi_entry_body
#endif
void clone_abi_entry_body(void *argument);
void clone_abi_entry_body(void *argument)
{
    phase1_entered++;
    phase1_argument = (unsigned long)argument;
    phase1_pid = Pgetpid();
    if (phase1_resources) {
        long position = Fseek(1,0,0);
        if ((phase1_resources & CLONE_COPY_STANDARD_HANDLES) ? position < 0 : position >= 0)
            phase1_resource_error |= 1;
        if (Dgetpath(phase1_child_path,3) ||
            ((phase1_resources & CLONE_COPY_CWD) ?
             strcmp(phase1_child_path,phase1_parent_path) != 0 :
             strcmp(phase1_child_path,phase1_parent_path) == 0))
            phase1_resource_error |= 2;
        if (Dsetpath("C:\\")) phase1_resource_error |= 4;
    }
    /* Public void return ignores the incidental native result register. */
#if defined(__arm__)
    __asm__ volatile ("mov r0,#91" ::: "r0");
#elif defined(__mc68000__)
    __asm__ volatile ("moveq #91,%%d0" ::: "d0");
#else
    __asm__ volatile ("movl $91,%%eax" ::: "eax");
#endif
}

static void phase1_finish(long tid, long pid)
{
    long status;
    ptest_assert_msg(tid > 0,"Pclone returns a positive TID");
    if (tid <= 0) return;
    status = Pwaitpid(pid,0,0);
    if (status <= 0 || (status & 65535)) {
        char message[96]; sprintf(message,"ABI child tid=%ld pid=%ld status=%lx\r\n",tid,pid,status); conws(message);
    }
    ptest_assert_msg(status > 0 && !(status & 65535),"void entry returns status zero on native backend");
    ptest_assert_msg(phase1_entered == 1 && phase1_argument == phase1_expected_argument &&
                     (status >> 16) == phase1_pid && phase1_pid != Pgetpid(),
                     "opaque high-bit argument and independent PROCESS identity");
#if defined(__x86_64__)
    ptest_assert_msg(clone_abi_argument == (unsigned long long)phase1_expected_argument,
                     "actual x32 entry register zero-extends the ILP32 opaque pointer");
#endif
    ptest_assert_msg(Pwaitpid(phase1_pid,1,0) == -33L,"one PROCESS completion and one public reap");
}

static void phase1_reject(const void *request, unsigned long size, long expected,
                          int32_t *pid, int32_t *tid, unsigned char *stack)
{
    long result;
    unsigned long *frame = (unsigned long *)(stack + RUNNABLE_STACK_SIZE - 16);
    *pid = 0x12345678; *tid = 0x23456789;
    *frame = 0x3456789aUL; phase1_entered = 0;
    result = pclone_trap(request,size);
    phase1_rejection_case++;
    if (result != expected) {
        char message[96]; sprintf(message,"ABI rejection %u got=%ld expected=%ld size=%lu\r\n",
                                 phase1_rejection_case,result,expected,size); conws(message);
    }
    ptest_assert_msg(result == expected,"native Pclone rejection has exact ABI status");
    ptest_assert_msg(*pid == 0x12345678 && *tid == 0x23456789 && *frame == 0x3456789aUL,
                     "rejection preserves both outputs and startup frame");
    (void)Syield(); (void)Syield();
    ptest_assert_msg(!phase1_entered && Pwaitpid(-1,1,0) == -33L,
                     "failed public call publishes no runnable child or zombie");
}

#if defined(__x86_64__)
static long phase1_wide_trap(unsigned long long address, unsigned long long size)
{
    register unsigned long long call __asm__("rax") = (1ULL << 32) | GEMDOS_PCLONE;
    register unsigned long long arg1 __asm__("rdi") = address;
    register unsigned long long arg2 __asm__("rsi") = size;
    __asm__ volatile ("syscall" : "+r"(call) : "r"(arg1), "r"(arg2)
                      : "rcx", "r11", "memory", "cc");
    return (long)(int)call;
}
#endif

static void phase1_resource_matrix(struct ptos_clone_args good)
{
    long saved = Fdup(0), file, result;
    int32_t pid, tid;
    unsigned bits;
    ptest_assert_msg(saved >= 6,"retain original standard input binding");
    if (saved < 6) return;
    file = Fcreate("C:\\PCLABI.TMP",0);
    ptest_assert_msg(file >= 6,"resource-copy fixture file");
    if (file < 6) { (void)Fclose(saved); return; }
    ptest_assert_msg(Dsetpath("C:\\TESTS") == 0 &&
                     Dgetpath(phase1_parent_path,3) == 0 && phase1_parent_path[0] && Fwrite(file,2,"AB") == 2 &&
                     Fseek(0,file,0) == 0 && Fforce(0,file) == 0,
                     "prepare independent standard-handle and CWD policy");
    good.child_pid = &pid; good.child_tid = &tid;
    for (bits = 0; bits < 4; bits++) {
        good.flags = CLONE_VM | (bits << 2);
        phase1_resources = good.flags; phase1_resource_error = 0;
        phase1_entered = 0;
        result = pclone_trap(&good,32);
        ptest_assert_msg(result > 0 && result == tid,"resource-copy public TID");
        phase1_finish(result,pid);
        if (phase1_resource_error) {
            char message[192]; sprintf(message,"ABI resource bits=%u error=%d parent=%s child=%s\r\n",
                                      bits,phase1_resource_error,phase1_parent_path,phase1_child_path); conws(message);
        }
        ptest_assert_msg(!phase1_resource_error && Dgetpath(phase1_child_path,3) == 0 &&
                         !strcmp(phase1_parent_path,phase1_child_path),
                         "four independent copy policies and PROCESS-local CWD mutation");
        ptest_assert_msg(Fseek(0,file,1) == ((bits & 1) ? 1 : 0),
                         "copied standard binding shares OFD; default does not");
        ptest_assert_msg(Fseek(0,file,0) == 0,"reset shared OFD without changing binding");
    }
    phase1_resources = 0;
    ptest_assert_msg(Fforce(0,saved) == 0 && Fclose(saved) == 0 && Fclose(file) == 0 &&
                     Dsetpath("C:\\") == 0 &&
                     Fdelete("C:\\PCLABI.TMP") == 0,"resource-copy fixture once-only cleanup");
}

#if defined(TEST_WITH_MMAP)
static void phase1_mapping_matrix(struct ptos_clone_args good, unsigned char *stack)
{
    long memory = clone_test_sys6(GEMDOS_MMAP,0,8192,PROT_READ|PROT_WRITE,
                                  MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    long code = clone_test_sys6(GEMDOS_MMAP,0,4096,PROT_READ|PROT_WRITE,
                                MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    long result;
    int32_t pid, tid;
    unsigned char *cross;
    ptest_assert_msg(memory > 0 && code > 0,"native ABI permission/lifetime mappings");
    if (memory <= 0 || code <= 0) {
        if (memory > 0) (void)clone_test_sys6(GEMDOS_MUNMAP,memory,8192,0,0,0,0);
        if (code > 0) (void)clone_test_sys6(GEMDOS_MUNMAP,code,4096,0,0,0,0);
        return;
    }
    good.child_pid = &pid; good.child_tid = &tid;
    cross = (unsigned char *)memory + 4096 - 15;
    memcpy(cross,&good,sizeof(good)); /* deliberately unaligned, crossing a page */
    ptest_assert_msg(clone_test_sys6(GEMDOS_MPROT,memory+4096,4096,PROT_NONE,0,0,0) == 0,
                     "revoke only final input page");
    phase1_reject(cross,32,-40L,&pid,&tid,stack);
    ptest_assert_msg(clone_test_sys6(GEMDOS_MPROT,memory+4096,4096,PROT_READ,0,0,0) == 0,
                     "read-only snapshot input is permitted");
    phase1_entered = 0;
    result = pclone_trap(cross,32);
    phase1_finish(result,pid);
    ptest_assert_msg(clone_test_sys6(GEMDOS_MPROT,memory,8192,PROT_READ,0,0,0) == 0,
                     "remove startup write permission");
    good.stack_base = (void *)memory; good.stack_size = 8192;
    phase1_reject(&good,32,-40L,&pid,&tid,stack);
    ptest_assert_msg(clone_test_sys6(GEMDOS_MPROT,memory,8192,PROT_READ|PROT_WRITE,0,0,0) == 0,
                     "restore writable stack reservation");
    good.entry = (void (*)(void *))memory;
    phase1_reject(&good,32,-40L,&pid,&tid,stack); /* RW data is not executable. */
    /* Tail-jump from a separately owned RX mapping to the real void entry.
     * Keep argument registers and the kernel-installed return bridge intact. */
#if defined(__arm__)
    ((unsigned long *)code)[0] = 0xe51ff004UL;
    ((unsigned long *)code)[1] = (unsigned long)phase1_entry;
#elif defined(__mc68000__)
    ((unsigned short *)code)[0] = 0x4ef9;
    *(unsigned long *)(code+2) = (unsigned long)phase1_entry;
#else
    ((unsigned char *)code)[0] = 0xb8;
    { unsigned long entry = (unsigned long)phase1_entry; memcpy((unsigned char *)code+1,&entry,4); }
    ((unsigned char *)code)[5] = 0xff; ((unsigned char *)code)[6] = 0xe0;
#endif
    ptest_assert_msg(clone_test_sys6(GEMDOS_MPROT,code,4096,PROT_READ|PROT_EXEC,0,0,0) == 0,
                     "publish native instruction mapping without W+X");
    good.entry = (void (*)(void *))code;
    phase1_entered = 0;
    result = pclone_trap(&good,32);
    ptest_assert_msg(result > 0 && result == tid && !phase1_entered,"owned RX entry admitted without dispatch");
    ptest_assert_msg(clone_test_sys6(GEMDOS_MPROT,code,4096,PROT_READ|PROT_WRITE,0,0,0) == -36L &&
                     clone_test_sys6(GEMDOS_MUNMAP,code,4096,0,0,0,0) == -36L &&
                     clone_test_sys6(GEMDOS_MPROT,memory,8192,PROT_READ,0,0,0) == -36L &&
                     clone_test_sys6(GEMDOS_MUNMAP,memory,8192,0,0,0,0) == -36L,
                     "pending entry and stack holds reject destructive mutation");
    phase1_finish(result,pid);
    ptest_assert_msg(clone_test_sys6(GEMDOS_MUNMAP,code,4096,0,0,0,0) == 0 &&
                     clone_test_sys6(GEMDOS_MUNMAP,memory,8192,0,0,0,0) == 0,
                     "stopped child releases entry/stack holds before final unmap");
}
#endif

static void phase1_account(const char *stage, long baseline)
{
    long usage[8];
    if (Prusage(usage) == 0 && baseline >= 0 && usage[4] != baseline) {
        char message[120]; sprintf(message,"ABI private-bytes %s expected=%ld actual=%ld\r\n",stage,baseline,usage[4]); conws(message);
    }
}

static void test_public_contract(unsigned char *stack)
{
    struct {
        struct ptos_clone_args args;
        unsigned long tail;
    } request = {{0},0xffffffffUL};
    struct ptos_clone_args good, bad;
    int32_t pid, tid;
    long result, memory, usage[8], baseline;
    unsigned size, bit;
    request.args.flags = CLONE_VM;
    request.args.entry = phase1_entry;
    request.args.argument = (void *)0xfedcba98UL;
    request.args.stack_base = stack;
    request.args.stack_size = RUNNABLE_STACK_SIZE;
    request.args.child_pid = &pid; request.args.child_tid = &tid;
    ptest_assert_msg(sizeof(struct ptos_clone_args) == 32 && sizeof(void *) == 4 &&
#if defined(__mc68000__)
                     __alignof__(struct ptos_clone_args) == 2,
#else
                     __alignof__(struct ptos_clone_args) == 4,
#endif
                     "native public structure size/pointer width/alignment");
    good = request.args;
    phase1_expected_argument = 0xfedcba98UL;
    phase1_resources = 0; phase1_entered = 0;
    phase1_argument = 0; phase1_pid = 0; phase1_resource_error = 0;
#if defined(__x86_64__)
    clone_abi_argument = 0;
#endif
    memset(phase1_parent_path,0,sizeof(phase1_parent_path));
    memset(phase1_child_path,0,sizeof(phase1_child_path));
    /* Touch the retained stack before measuring failures: legitimate lazy
     * materialization is not rollback of a reservation. */
    for (size = 0; size < RUNNABLE_STACK_SIZE; size += 4096) stack[size] = 0;
    phase1_account("initial",-1);
    ptest_assert_msg(Prusage(usage) == 0,"Phase1 initial page accounting"); baseline = usage[4];
    for (size = 28; size <= 36; size++) {
        request.args = good;
        pid = tid = -99; phase1_entered = 0;
        if (size < 32) request.args.child_pid = (int32_t *)0xffff0000UL;
        result = pclone_trap(&request,size);
        ptest_assert_msg(result == tid && result > 0 && !phase1_entered &&
                         (size < 32 ? pid == -99 : pid > 0),
                         "28..31 prefix, 32 extension, unknown tail and no early dispatch");
        phase1_finish(result,size < 32 ? -1 : pid);
    }
    phase1_account("prefix",baseline);
    {
        static const unsigned long opaque[] = {0,0x80000000UL,0xffffffffUL};
        for (bit = 0; bit < sizeof(opaque)/sizeof(opaque[0]); bit++) {
            bad = good; bad.argument = (void *)opaque[bit];
            phase1_expected_argument = opaque[bit]; phase1_entered = 0;
            result = pclone_trap(&bad,32);
            phase1_finish(result,pid);
        }
        phase1_expected_argument = 0xfedcba98UL;
    }
    for (bit = 0; bit < 4; bit++) {
        bad = good; pid = tid = -99; phase1_entered = 0;
        bad.child_pid = (bit & 1) ? &pid : 0;
        bad.child_tid = (bit & 2) ? &tid : 0;
        result = pclone_trap(&bad,32);
        ptest_assert_msg(result > 0 && ((bit & 1) ? pid > 0 : pid == -99) &&
                         ((bit & 2) ? tid == result : tid == -99),
                         "all optional PID/TID output combinations");
        phase1_finish(result,(bit & 1) ? pid : -1);
    }
    bad = good; bad.flags |= CLONE_SAME_PROCESS;
    phase1_entered = 0;
    result = pclone_trap(&bad,32);
    ptest_assert_msg(result > 0 && tid == result && pid == Pgetpid(),
                     "same-PROCESS success returns TID and reports caller PID");
    if (result > 0) {
#if defined(TEST_WITH_PTHJOIN)
        int32_t status = -99;
        ptest_assert_msg(join_tid(result,&status) == 0 && status == 0,
                         "same-PROCESS void entry forces zero THREAD completion");
#else
        (void)Syield(); (void)Syield();
#endif
        ptest_assert_msg(phase1_entered == 1 && phase1_pid == Pgetpid() &&
                         Pwaitpid(-1,1,0) == -33L,
                         "THREAD return does not manufacture a PROCESS completion");
    }
    phase1_account("optional/same",baseline);
    /* The input is copied once: a legal output may alias the request itself. */
    bad = good; bad.child_tid = (int32_t *)&bad.flags; phase1_entered = 0;
    result = pclone_trap(&bad,32);
    ptest_assert_msg(result > 0 && bad.flags == (unsigned long)result,"snapshot survives input/output alias");
    phase1_finish(result,pid);
    phase1_account("input alias",baseline);
#define REJECT(error) phase1_reject(&bad,32,(error),&pid,&tid,stack)
    bad = good; bad.flags = 0; REJECT(-32L);
    bad = good; bad.flags = CLONE_SAME_PROCESS; REJECT(-25L);
    for (bit = 4; bit < 32; bit++) { bad = good; bad.flags |= 1UL << bit; REJECT(-25L); }
    for (bit = 2; bit <= 3; bit++) {
        bad = good; bad.flags |= CLONE_SAME_PROCESS | (1UL << bit); REJECT(-25L);
    }
    bad = good; bad.tls = (void *)0x80001000UL; REJECT(-25L);
    bad = good; bad.entry = (void (*)(void *))0x80001000UL; REJECT(-40L);
    bad = good; bad.stack_base = (void *)0x80001000UL; REJECT(-40L);
    bad = good; bad.stack_base = (void *)0xfffffff0UL; REJECT(-40L);
    bad = good; bad.stack_size = 0; REJECT(-25L);
    bad = good; bad.stack_base = stack + 1; REJECT(-25L);
    bad = good; bad.child_pid = (int32_t *)0x80001000UL; REJECT(-40L);
    bad = good; bad.child_tid = (int32_t *)0x80001000UL; REJECT(-40L);
    bad = good; bad.child_tid = (int32_t *)((unsigned char *)&tid + 1); REJECT(-25L);
    bad = good; bad.child_pid = bad.child_tid; REJECT(-25L);
    bad = good; bad.child_pid = (int32_t *)((unsigned char *)&tid + 2); REJECT(-25L);
    bad = good; bad.child_tid = (int32_t *)stack; REJECT(-25L);
    bad = good; bad.child_pid = (int32_t *)(stack + RUNNABLE_STACK_SIZE - 4); REJECT(-25L);
    phase1_account("before code-output",baseline);
    bad = good; bad.child_tid = (int32_t *)((unsigned long)phase1_entry & ~3UL);
#if defined(__mc68000__)
    REJECT(-25L); /* legacy image page is writable: explicit code-page overlap */
#else
    REJECT(-40L); /* read-only executable leaf fails writable-range validation */
#endif
    phase1_account("after code-output",baseline);
#undef REJECT
    phase1_reject(0,32,-40L,&pid,&tid,stack);
    phase1_reject((void *)0x80001000UL,32,-40L,&pid,&tid,stack);
    phase1_reject((void *)0xfffffff0UL,32,-40L,&pid,&tid,stack);
    phase1_reject(&good,27,-25L,&pid,&tid,stack);
    phase1_reject(&good,0xffffffffUL,-40L,&pid,&tid,stack);
#if defined(__x86_64__)
    pid = 0x12345678; tid = 0x23456789; phase1_entered = 0;
    ptest_assert_msg(phase1_wide_trap(0x100000000ULL | (unsigned long)&good,32) == -40L &&
                     phase1_wide_trap((unsigned long)&good,0x100000020ULL) == -25L &&
                     pid == 0x12345678 && tid == 0x23456789,
                     "actual x32 trap rejects high64 native address/size without low32 truncation");
    (void)Syield(); (void)Syield();
    ptest_assert_msg(!phase1_entered && Pwaitpid(-1,1,0) == -33L,"high64 rejection exposes no child");
#endif
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
    /* Existing disabled test-control namespace; ordinary calls below still
     * use the exact public 32-byte CLONE_VM ABI, not the private-VM escape. */
    for (bit = 1; bit <= 5; bit++) {
        ptest_assert_msg(pclone_trap(0,0xc8920400UL + bit) == 0,"arm clone acquisition failure injection");
        phase1_reject(&good,32,-39L,&pid,&tid,stack);
        (void)pclone_trap(0,0xc8920400UL);
        ptest_assert_msg(pclone_trap(0,0xc8920500UL + bit) == 0,"arm clone factory failure injection");
        phase1_reject(&good,32,-39L,&pid,&tid,stack);
        (void)pclone_trap(0,0xc8920500UL);
    }
    ptest_assert_msg(pclone_trap(0,0xc8920600UL) == 0,"arm final-publication abandonment");
    phase1_reject(&good,32,-326L,&pid,&tid,stack);
#endif
    phase1_account("rejections",baseline);
    /* Writable native outputs may straddle pages on the m68k two-byte ABI.
     * x32/A32 words stay aligned, with the two outputs on separate pages. */
    memory = Malloc(16384);
    ptest_assert_msg(memory > 0,"allocate cross-page ABI fixture");
    if (memory > 0) {
        unsigned long boundary = ((unsigned long)memory + 4095UL) & ~4095UL;
        struct ptos_clone_args *cross;
        int32_t *out_pid, *out_tid;
        boundary += 4096;
        cross = (struct ptos_clone_args *)(boundary - 16);
        *cross = good;
        out_pid = (int32_t *)(boundary + 4096 - 4);
#if defined(__mc68000__)
        out_tid = (int32_t *)(boundary + 4096 - 2);
        out_pid = (int32_t *)(boundary + 4096 - 8);
#else
        out_tid = (int32_t *)(boundary + 4096);
#endif
        cross->child_pid = out_pid; cross->child_tid = out_tid;
        phase1_entered = 0;
        result = pclone_trap(cross,32);
        ptest_assert_msg(result > 0 && result == *out_tid && *out_pid > 0,"cross-page input and native identity stores");
        phase1_finish(result,*out_pid);
        ptest_assert_msg(Mfree(memory) == 0,"cross-page fixture reclaimed");
    }
    phase1_entered = 0;
    result = pclone_trap(&good,32);
    phase1_finish(result,pid); /* retry after every failure */
    phase1_account("cross/retry",baseline);
    {
        static const char tail[] = "\017--abi-unmanaged";
        ptest_log_flush();
        ptest_assert_msg(Pexec(0,"runtests.tos",tail,0) == 0,
                         "nested synchronous launcher is not a managed THREAD");
    }
    phase1_account("nested",baseline);
    phase1_resource_matrix(good);
    phase1_account("resources",baseline);
#if defined(TEST_WITH_MMAP)
    phase1_mapping_matrix(good,stack);
    phase1_account("mappings",baseline);
#endif
    ptest_assert_msg(Prusage(usage) == 0 && usage[4] == baseline,"Phase1 failed/successful traps restore mapped-page baseline");
    phase1_contract_exercised = 1;
    conws("native Pclone ABI prefix/flags/pointers/atomic outputs: tested\r\n");
}
