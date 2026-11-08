/* Real capture producer, modeled CPU register/ownership queries. No IRQ execution. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "../bdos/arch/x86_64/interrupt_user.c"
#define CHECK(x) do { if (!(x)) abort(); } while (0)
PD *run;
static KTHREAD_CONTEXT execution;
static BOOL owned=TRUE;
static UQUAD root=0x1000, top;
KTHREAD_CONTEXT *ksched_interrupt_target(PD *pd, struct kaspace **space, ULONG *tid)
{ (void)pd; *space=(struct kaspace *)1; *tid=17; return owned ? &execution : NULL; }
ASPACE *kaspace_backend(const KASPACE *space) { (void)space; return (ASPACE *)1; }
ASPACE_ROOT aspace_root(const ASPACE *space) { (void)space; return 0x1000; }
UQUAD x86_64_read_cr3(void) { return root; }
UQUAD x86_64_get_kernel_stack(void) { return top-8; }
UQUAD x86_64_get_irq_stack(void) { return top; }
int main(void)
{
    X86_USER_IRQ_FRAME f={0}, before;
    X86_USER_IRQ_CAPTURE *c=&execution.interrupted_user;
    UWORD i;
    top=(uintptr_t)&f+sizeof(f);
    execution.identity=123; execution.owner_cpu=0;
    f.cs=X86_64_USER_CODE_SEL; f.ss=X86_64_USER_DATA_SEL;
    f.rip=0x400000; f.rsp=0x700000; f.rflags=0x202; f.vector=32;
    for(i=0;i<512;i++) f.fx[i]=(UBYTE)i;
    f.rax=0x1234; before=f;
    x86_64_capture_user_irq(&f);
    CHECK(c->valid && c->sequence==1 && c->execution==123 && c->tid==17 && !c->cpu);
    CHECK(!memcmp(&c->image,&f,sizeof(f)) && !memcmp(&f,&before,sizeof(f)));
    CHECK(c->binding.frame_address==(uintptr_t)&f && c->binding.cr3==0x1000);
    f.rax=0x5678; CHECK(c->image.rax==0x1234); /* copied, never borrowed */
    root=0x2000; x86_64_capture_user_irq(&f); CHECK(!c->valid && c->sequence==1);
    root=0x1000; top-=16; x86_64_capture_user_irq(&f); CHECK(!c->valid && c->sequence==1);
    top+=16; x86_64_capture_user_irq(&f); CHECK(c->valid && c->sequence==2 && c->image.rax==0x5678);
    c->sequence=~(UQUAD)0; x86_64_capture_user_irq(&f); CHECK(!c->valid && c->sequence==~(UQUAD)0);
    c->sequence=7; owned=FALSE; x86_64_capture_user_irq(&f); CHECK(!c->valid && c->sequence==7);
    owned=TRUE; f.cs=8; x86_64_capture_user_irq(&f); CHECK(!c->valid && c->sequence==7);
    puts("x32 capture producer: copied frame, unchanged IRQ source, binding rejection and sequence exhaustion: PASS (CPU queries modeled)");
    return 0;
}
