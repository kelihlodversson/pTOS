/* Shared staged-source fixture for the full suite and small companion. */
/* The same staged image is checked by its loader PROCESS, a CLONE_VM child,
 * and an orphan after that image owner's retirement. No PID-based policy. */
static volatile unsigned long phase1_policy_data[1024] __attribute__((aligned(4096))) = {1};
static volatile unsigned long phase1_policy_bss[1024] __attribute__((aligned(4096)));
static unsigned char *phase1_policy_stack;
static void *phase1_policy_hole;
static int phase1_policy_writable_text;
struct phase1_policy_report { long magic, creator, worker, gate, done, error; };
#define PHASE1_POLICY_ELF "C:\\PCLPOL.TMP"
#define PHASE1_POLICY_PRG "C:\\PRGPOL.TMP"
static const char *phase1_policy_file = PHASE1_POLICY_ELF;

static char phase1_policy_cwd[128];
static volatile int phase1_policy_cwd_error;
static void phase1_policy_cwd_leaf(void *argument)
{
    char path[128], cwd[128];
    if (Dgetpath(path,3) || strcmp(path,phase1_policy_cwd) ||
        Dgetcwd(cwd,3,sizeof(cwd)) || strcmp(path,cwd) || Dsetpath("C:\\"))
        phase1_policy_cwd_error=1;
    phase1_entry(argument);
}

static long phase1_policy_checks(void)
{
    struct ptos_clone_args args = {0};
    int32_t pid, tid;
    long result, status, error = 0;
    unsigned i;
    unsigned char sentinel[16], *frame = phase1_policy_stack + RUNNABLE_STACK_SIZE - 16;
    void *bad[3];
    char cwd[128], path[128];
    if (Dgetpath(path,3) || strcmp(path,phase1_policy_cwd) ||
        Dgetcwd(cwd,3,sizeof(cwd)) || strcmp(path,cwd)) error |= 2048;
    args.flags = CLONE_VM | CLONE_COPY_CWD; args.stack_base = phase1_policy_stack;
    args.stack_size = RUNNABLE_STACK_SIZE; args.child_pid = &pid; args.child_tid = &tid;
    bad[0] = (void *)&phase1_policy_data[1023]; bad[1] = (void *)&phase1_policy_bss[1023]; bad[2] = phase1_policy_hole;
    for (i = 0; i < 3; i++) {
        pid = 0x12345678; tid = 0x23456789;
        memset(sentinel,0x96,sizeof(sentinel)); memcpy(frame,sentinel,sizeof(sentinel));
        args.entry = (void (*)(void *))bad[i];
        result = pclone_trap(&args,32);
        if (result != -40L) error |= 1L << i;
        if (pid != 0x12345678 || tid != 0x23456789 || memcmp(frame,sentinel,sizeof(sentinel))) error |= 8;
        if (result > 0) return error | 16; /* Never execute an unexpected data entry. */
        (void)Syield(); (void)Syield();
        if (Pwaitpid(-1,1,0) != -33L) error |= 32;
    }
    /* Also execute the writable-PRG / readonly-ELF error contract from the
     * actual staged image, including after its original PROCESS is gone. */
    {
        unsigned char before[4];
        void *output = (void *)((unsigned long)phase1_entry & ~3UL);
        memcpy(before,output,sizeof(before));
        memcpy(frame,sentinel,sizeof(sentinel));
        args.entry=phase1_entry; args.child_tid=output;
        pid=0x12345678; tid=0x23456789;
        result=pclone_trap(&args,32);
        if(result != (phase1_policy_writable_text ? -25L : -40L) ||
           pid!=0x12345678 || tid!=0x23456789 ||
           memcmp(before,output,sizeof(before)) || memcmp(frame,sentinel,sizeof(sentinel))) error|=1024;
        if(result>0) return error;
        (void)Syield(); (void)Syield();
        if(Pwaitpid(-1,1,0)!=-33L) error|=32;
    }
    args.entry = phase1_policy_cwd_leaf; args.argument = 0; args.child_tid=&tid; phase1_entered = 0;
    phase1_policy_cwd_error=0;
    pid = tid = -99;
    result = pclone_trap(&args,32);
    if (result <= 0 || result != tid || pid <= 0 || pid == Pgetpid()) return error | 64;
    status = Pwaitpid(pid,0,0);
    if ((status >> 16) != pid || (status & 65535) || phase1_entered != 1 ||
        phase1_argument || phase1_pid != pid || Pwaitpid(pid,1,0) != -33L) error |= 128;
    if (phase1_policy_cwd_error || Dgetpath(path,3) || strcmp(path,phase1_policy_cwd)) error |= 4096;
    return error;
}

static void phase1_policy_child(void *argument)
{
    if (!argument) {
        phase1_resource_error = (int)phase1_policy_checks();
    } else {
        struct phase1_policy_report report;
        long file = Fopen(phase1_policy_file,2), error,received=0;
        unsigned i;
        if (file < 0) return;
        memset(&report,0,sizeof(report));
        for (i = 0; i < 128; i++) {
            if (Fseek(0,file,0) < 0) { received=-1;break; }
            received=Fread(file,sizeof(report),&report);
            if(received<0) break;
            /* Publication may follow an early reader. Never run the policy
             * checks or overwrite the source's header until its explicit gate. */
            if(received==sizeof(report) && report.magic==0x50434c4eL &&
                report.worker==Pgetpid() && report.gate) break;
            (void)Syield();
        }
        if(received!=sizeof(report) || report.magic!=0x50434c4eL ||
            report.worker!=Pgetpid() || !report.gate) {
            printf("orphan report gate timeout or I/O failure: bytes=%ld polls=%u\r\n",received,i);
            (void)Fclose(file);return;
        }
        error = phase1_policy_checks();
        if (!report.gate || report.magic != 0x50434c4eL || report.worker != Pgetpid()) error |= 256;
        report.done = 1; report.error = error;
        if (Fseek(0,file,0) >= 0) (void)Fwrite(file,sizeof(report),&report);
        (void)Fclose(file);
    }
}

static void phase1_run_staged(void)
{
    struct ptos_clone_args args = {0};
    struct phase1_policy_report report;
    int32_t pid = -1, tid = -1;
    long stack = Malloc(RUNNABLE_STACK_SIZE), result, status, file;
    volatile unsigned char touch;
    ptest_begin("Pclone from staged executable text");
    ptest_assert_msg(Dsetpath("C:\\TESTS") == 0 &&
                     Dgetpath(phase1_policy_cwd,3) == 0 &&
                     (!strcmp(phase1_policy_cwd,"TESTS") || !strcmp(phase1_policy_cwd,"\\TESTS")),
                     "staged owner selects inheritable non-root CWD");
    phase1_policy_stack = (unsigned char *)Malloc(RUNNABLE_STACK_SIZE);
    /* Loader-owned slack is resident data, but outside executable segments. */
    if (_base->p_tbase == (void *)(_base + 1)) {
        /* Classic p_hitpa is intentionally stale after CRT Mshrink. Use a
         * resident TPA stack page, not the retired original allocation end. */
        phase1_policy_hole = (void *)((unsigned long)&touch & ~4095UL);
        phase1_policy_file = PHASE1_POLICY_PRG;
        phase1_policy_writable_text = 1;
    } else {
        phase1_policy_hole = (void *)(((unsigned long)_base->p_bbase + _base->p_blen + 8191UL) & ~4095UL);
        phase1_policy_file = PHASE1_POLICY_ELF;
        phase1_policy_writable_text = 0;
    }
    touch = *(volatile unsigned char *)phase1_policy_hole;
    (void)touch;
    ptest_assert_msg(stack > 0 && phase1_policy_stack,"staged-image entry stacks");
    if (stack > 0 && phase1_policy_stack) {
        ptest_assert_msg(phase1_policy_checks() == 0,"staged owner enforces text/data/BSS/image-hole policy");
        args.flags = CLONE_VM | CLONE_COPY_CWD; args.entry = phase1_policy_child;
        args.stack_base = (void *)stack; args.stack_size = RUNNABLE_STACK_SIZE;
        args.child_pid = &pid; args.child_tid = &tid;
        phase1_resources = 0; phase1_resource_error = 0;
        result = pclone_trap(&args,32);
        ptest_assert_msg(result > 0 && result == tid,"staged CLONE_VM PROCESS child admitted");
        if (result > 0) {
            status = Pwaitpid(pid,0,0);
            ptest_assert_msg((status >> 16) == pid && !(status & 65535) && !phase1_resource_error,
                             "nested shared child enforces original staged image policy");
        }
        /* Do not free the orphan's active stack or its nested-child stack.
         * Their containing KASPACE owns all pages until its final reference. */
        args.argument = (void *)1;
        result = pclone_trap(&args,32);
        ptest_assert_msg(result > 0 && result == tid,"orphan staged-policy child admitted");
        ptest_assert_msg(Dsetpath("C:\\") == 0,
                         "parent changes CWD before orphan continues independently");
        file = Fopen(phase1_policy_file,2);
        memset(&report,0,sizeof(report)); report.magic=0x50434c4eL;
        report.creator=Pgetpid(); report.worker=pid;
        ptest_assert_msg(file >= 6 && Fwrite(file,sizeof(report),&report) == sizeof(report) && Fclose(file)==0,
                         "publish orphan identity before staged owner exits");
    }
    ptest_pass();
}
