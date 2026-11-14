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
#include "../../include/mman.h"
#include "../../include/shared_pexec_test.h"
#include "../../include/mapping_lifetime_test.h"
#include "../pie_load/shared_vm_probe.h"
static int64_t loaded_descendant_publish(uintptr_t image,struct shared_vm_probe *probe)
{
    static MAPPING_LIFETIME_SNAPSHOT before,after;
    unsigned phase,prefix,i;
    int64_t result,steps;
    for(phase=0;phase<2;phase++) for(prefix=1;prefix<256;prefix++) {
        if(dl_syscall(0x190,(uintptr_t)&before,MLT_SNAPSHOT,0,0,0,0)) return -1;
        if(!phase) {
            /* The first acquisition after mapping admission is raw[0].
             * Force it to fail so a complete mapping prefix stays unpublished. */
            if(dl_syscall(0x190,0,0xc9670200UL+prefix,0,0,0,0) ||
               dl_syscall(0x190,0,0xc8940903UL,0,0,0,0)) return -1;
        } else if(dl_syscall(0x190,0,0xc8940900UL+prefix,0,0,0,0)) return -1;
        result=dl_syscall(0x4b,104,0,image,0,0,0);
        steps=dl_syscall(0x190,0,0xc8940a00UL,0,0,0,0);
        (void)dl_syscall(0x190,0,0xc9670200UL,0,0,0,0);
        (void)dl_syscall(0x190,0,0xc8940900UL,0,0,0,0);
        if(phase && result>0) {
            probe->descendant_preparation_prefixes=prefix-1;
            return prefix>16 && steps==prefix-1?result:-1;
        }
        if(result!=-39 || dl_syscall(0x190,(uintptr_t)&after,MLT_SNAPSHOT,0,0,0,0)) return -1;
        for(i=0;i<MLT_FIELDS;i++) if(before.value[i]!=after.value[i]) {
            probe->error=1100+i;return -1;
        }
        if(!phase && steps==3) {probe->descendant_mapping_prefixes=prefix-1;break;}
    }
    return -1;
}
/* Entry and writable state live in the actual file-backed DSO. The generic
 * CLONE_VM owner must keep them and their executable dependencies alive after
 * the interpreted creator has stopped, including when fini never executes. */
void dso_shared_peer(void *argument)
{
    struct shared_vm_probe *probe=argument;
    unsigned spin,i;
    char tail[12];
    uintptr_t address=(uintptr_t)probe;
    int64_t image,pid;
    volatile unsigned char *late;
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
    late=(volatile unsigned char *)(uintptr_t)probe->late_runtime;
    if(!late || late[0]!=0x7f || late[1]!='E' || late[2]!='L' || late[3]!='F') {
        probe->error=93;return;
    }
    late[0]=0x53;if(late[0]!=0x53) probe->error=94; /* actual retained file COW */
    dynbss=17;probe->steps=2;
    /* A loaded child acquires existing source-owned runtime dependencies and
     * special ancestor receipts, then outlives this intermediate PROCESS. */
    tail[0]=10;tail[1]='h';tail[2]=' ';
    for(i=0;i<8;i++) tail[3+i]="0123456789abcdef"[(address>>(28-4*i))&15];
    tail[11]=0;
    /* Generic compatibility metadata is supervisor-only: provide an actual
     * user-readable environment, as other generic-source launch fixtures do. */
    image=dl_syscall(0x4b,3,(uintptr_t)"C:\\PIEPROBE.TOS",(uintptr_t)tail,
                     (uintptr_t)"CHECK=descendant\0\0",0,0);
    if(image<=0) {probe->environment=(uint32_t)image;probe->error=95;return;}
    probe->staged=(uint32_t)image;
    pid=loaded_descendant_publish((uintptr_t)image,probe);
    if(pid<=0) {probe->environment=(uint32_t)pid;probe->error=96;return;}
    probe->descendant_pid=(uint32_t)pid;
    for(spin=0;spin<512 && !probe->descendant_ready;spin++)
        (void)dl_syscall(0xff,0,0,0,0,0,0);
    if(!probe->descendant_ready) probe->error=97;
    image=dl_syscall(GEMDOS_MMAP,0,4096,PROT_READ|PROT_WRITE,
                    MAP_PRIVATE|MAP_ANONYMOUS|MAP_PTOS_RUNTIME,(uint64_t)-1,0);
    if(image<=0) {probe->error=99;return;}
    probe->unrelated_runtime=(uint32_t)image;
    *(volatile uint32_t *)(uintptr_t)image=0x967894UL;
    probe->done=1;
}
#endif
