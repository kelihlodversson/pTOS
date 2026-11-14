extern int startup_log;
extern int leaf(void);
int dynvalue = 5;
int dynbss;
int answer(void) { return leaf() + dynvalue + dynbss; }
__attribute__((constructor)) static void init(void)
{
    startup_log = startup_log * 10 + 2;
    dynvalue = 9;
}
__attribute__((destructor)) static void fini(void)
{
    startup_log = startup_log * 10 + 5;
    dynvalue = -7;
}
#ifdef LDSO_TEST_DSO_PEER
#include "../../lib/ldso/runtime.h"
#include "../pie_load/shared_vm_probe.h"
/* Entry and writable state live in the actual file-backed DSO. The generic
 * CLONE_VM owner must keep them and their executable dependencies alive after
 * the interpreted creator has stopped, including when fini never executes. */
void dso_shared_peer(void *argument)
{
    struct shared_vm_probe *probe=argument;
    unsigned spin;
    if(dynvalue!=(int32_t)probe->stack_limit || dynbss!=3 ||
        answer()!=14+(int32_t)probe->stack_limit) probe->error=81;
    probe->core_limit=(uintptr_t)&answer;probe->data_limit=(uintptr_t)&dynvalue;
    probe->steps=1;probe->worker_started=1;
    for(spin=0;spin<2048 && !probe->gate;spin++)
        (void)dl_syscall(0xff,0,0,0,0,0,0);
    if(!probe->gate) {probe->error=82;return;}
    if(dynvalue!=(probe->controller_result?(int32_t)probe->stack_limit:-7) || dynbss!=3 ||
        answer()!=(probe->controller_result?14+(int32_t)probe->stack_limit:7)) probe->error=83;
    if(!probe->grandchild || *(volatile uint32_t *)(uintptr_t)probe->grandchild!=0x644894UL)
        probe->error=84;
    dynbss=17;probe->steps=2;probe->done=1;
}
#endif
