/* The ELF or PRG replacement image of the Pexec(200) acceptance test (#970). It runs
 * in the subject's PROCESS (exec200_check.h lists what it verifies).
 * Generation 1 replaces itself once more: after the runner has observed the
 * commit when the subject is an anchor (a nested subject's runner is blocked
 * in its Pexec(0)), and leaving a directory search open. Generation 2 checks
 * that search did not survive and replaces itself with EXEC200.TOS, which
 * completes the PROCESS. */
#include "exec200_check.h"
#include "exec200_vfork.h"
#include <mint/mintbind.h>
#if defined(__mc68000__) && defined(TEST_WITH_SHARED_VM_PEXEC) && !defined(EXEC200_PRG)
#include "../shared_vm/loaded_prg.h"
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
#include "../../include/mapping_lifetime_test.h"
#endif

/* An ELF process stages the same position-independent code as the PRG
 * loader fixture. Modes 4/104 borrow the TPA; 6/106 transfer ownership. */
static void exec200_elf_legacy(void)
{
    static const short stages[]={5,7}, modes[]={4,6,104,106};
    const char *who="legacy ELF caller";
    unsigned i,j;
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
    MAPPING_LIFETIME_SNAPSHOT before={ {0} },after={ {0} };
#endif

    exec200_verify_startup(who);
    for (i=0;i<sizeof(stages)/sizeof(stages[0]);i++) {
        for (j=0;j<sizeof(modes)/sizeof(modes[0]);j++) {
            short mode=modes[j];
            int transfer=mode==6 || mode==106;
            BASEPAGE *bp;
            char *env;
            long result, status, free_bp, free_env;

#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
            if (exec200_probe(&before,MLT_SNAPSHOT)) exec200_fail(who,"snapshot before staging",mode);
#endif
            bp=(BASEPAGE *)Pexec(stages[i],0,"","LEGACY=ELF\0");
            if ((long)bp<=0) exec200_fail(who,"stage basepage",stages[i]);
            env=bp->p_env;
            if (!loaded_prg_stage(bp)) exec200_fail(who,"stage TPA cannot hold probe",stages[i]);
            result=Pexec(mode,0,bp,0);
            status=result;
            if (mode>=100) {
                if (result<=0 || result==Pgetpid())
                    exec200_fail(who,"async launch PID",result);
                status=Pwaitpid(result,0,0);
                if ((status>>16)!=result || Pwaitpid(result,1,0)!=-33L)
                    exec200_fail(who,"exactly one async child completion",status);
                status&=65535;
            }
            if (status!=0x2b) {
                printf("ELF staged modes %d/%d\r\n",stages[i],mode);
                exec200_fail(who,"classic PRG entry",status);
            }
            if (!transfer && !loaded_prg_shared_witness(bp))
                exec200_fail(who,"shared-VM entry witness in caller's TPA",mode);
            free_bp=Mfree(bp);free_env=Mfree(env);
            if (free_bp!=(transfer?-36L:0) || free_env!=(transfer?-36L:0)) {
                printf("ELF staged modes %d/%d: Mfree TPA=%ld env=%ld\r\n",
                       stages[i],mode,free_bp,free_env);
                exec200_fail(who,"TPA and environment ownership",mode);
            }
#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
            if (exec200_probe(&after,MLT_SNAPSHOT) ||
                before.value[MLT_ALLOCS]!=after.value[MLT_ALLOCS] ||
                before.value[MLT_FREE_PAGES]!=after.value[MLT_FREE_PAGES] ||
                before.value[MLT_BAD_FREES]!=after.value[MLT_BAD_FREES])
                exec200_fail(who,"staged allocations retire before caller exit",mode);
#endif
        }
    }
}
#endif
#if defined(TEST_WITH_IRQ_RESTART) && defined(TEST_WITH_PRIVATE_CLONE_TEST)
#include "exec200_irq.h"

static void verify_new_irq(void *unused)
{
    (void)unused;
    exec200_irq_retain();
    Pterm(exec200_probe(0, IRT_FINISH) ? 87 : 0);
}

static void verify_irq_replacement(void)
{
    long memory,tid;
    int32_t pid=0;
    if (exec200_probe(0, IRT_QUERY) != IRT_EMPTY) exec200_fail("image", "stale retained IRQ", 0);
    memory=Malloc(EXEC200_IRQ_STACK);
    if (memory<=0) exec200_fail("image", "new IRQ stack", memory);
    tid=exec200_irq_spawn(verify_new_irq,(void *)memory,&pid);
    if (tid<=0 || pid<=0) exec200_fail("image", "new IRQ process", tid);
    exec200_irq_reap(pid,(void *)memory);
}
#endif

#ifndef EXEC200_IMAGE_NAME
#define EXEC200_IMAGE_NAME "EXEC200B.TOS"
#endif

int main(void)
{
    EXEC200_TAIL t;
    long result;
#if defined(__mc68000__) && defined(TEST_WITH_SHARED_VM_PEXEC) && !defined(EXEC200_PRG)
    if (_base->p_cmdlin[0]==1 && _base->p_cmdlin[1]=='L') {
        exec200_elf_legacy();
        return 0;
    }
#endif
#if defined(EXEC200_VFORK_CASE) && !defined(EXEC200_PRG)
    if (_base->p_cmdlin[0]==1 && _base->p_cmdlin[1]=='V') {
        exec200_vfork_source("EXEC200P.TOS");
        return 0;
    }
#endif
    exec200_verify("image", &t);
#ifdef EXEC200_VFORK_CASE
    if (t.nested==EXEC200_VFORK_CASE) {
        unsigned long start=(unsigned long)Ssystem(S_GETLVAL,0x4baL,0L);
        if (t.generation!=1) exec200_fail("Pvfork image","first committed generation",t.generation);
        while (Fsfirst(EXEC200_VFORK_GATE,0)) {
            if ((unsigned long)Ssystem(S_GETLVAL,0x4baL,0L)-start>=2000UL)
                exec200_fail("Pvfork image","parent did not resume at commit",0);
            (void)Syield();
        }
        Pterm(76);
    }
#endif
#ifdef __mc68000__
#if defined(TEST_IS_PRG) && defined(EXEC200_PRG)
    if (t.generation == 1) (void)Cconws("native Pexec(200) PRG to PRG: PASS\r\n");
#else
#ifdef TEST_IS_PRG
    if (t.generation == 1) (void)Cconws("native Pexec(200) PRG to ELF: PASS\r\n");
#endif
    if (t.generation == 2) (void)Cconws("native Pexec(200) ELF to ELF: PASS\r\n");
#endif
#endif
    if (t.generation == 1) {
#if defined(TEST_WITH_IRQ_RESTART) && defined(TEST_WITH_PRIVATE_CLONE_TEST)
        if (t.nested == 2) {
            unsigned attempt;
            for (attempt=0;attempt<1024 && Fsfirst(EXEC200_IRQ_GATE,0);attempt++) (void)Syield();
            if (attempt==1024) exec200_fail("image", "retained IRQ commit gate", 0);
            verify_irq_replacement();
            (void)Cconws("native retained IRQ Pexec(200) old-owner retirement and fresh-image restart: PASS\r\n");
        }
#endif
        while (!t.nested && Fsfirst("GO970.TMP", 0)) (void)Syield();
        /* Leave a multi-match search open on the default DTA. */
        if (Fsfirst("*.TOS", 0)) exec200_fail("image", "open search", 0);
        result = exec200_next(EXEC200_IMAGE_NAME, 0, &t, 0);   /* inherits this environment */
        exec200_fail("image", "second Pexec(200) returned", result);
    }
    if (t.generation != 2) exec200_fail("image", "unexpected generation", (long)t.generation);
    /* The replaced program's directory search must not survive: it was
     * left open on this same default DTA, address and PROCESS. */
    if (Fsnext() == 0) exec200_fail("image", "search survived the exec", 0);
    result = exec200_next("EXEC200.TOS", 'F', &t, 0);
    exec200_fail("image", "Pexec(200) of the subject returned", result);
    return 86;
}
