/* Production wait selection/claim code. Reuse explicit host CPU endpoints. */
#define main result_wait_fixture_main
#include "sched_result_wait.c"
#undef main
int main(void)
{
    KERNEL_TASK first,second;
    CHILD_WAIT own={&process_pd,41,9,17},foreign={&process_pd,-1,9,18};
    memset(&first,0,sizeof(first));memset(&second,0,sizeof(second));
    scheduler.tasks=&first;first.next_task=&second;
    first.parent=second.parent=&process_pd;first.pid=41;second.pid=42;
    first.asynchronous=second.asynchronous=TRUE;
    first.state=TASK_ZOMBIE;second.state=TASK_BLOCKED;
    first.synchronous_wait_process=9;first.synchronous_waiter=17;
    assert(process_wait_target(&first,&own) && process_wait_claim_available(&first,&own));
    assert(!process_wait_target(&second,&own));
    assert(!process_wait_claim_available(&first,&foreign));
    /* A claimed zombie cannot make another wait-any busy-spin while its
     * independently eligible child is still blocked. */
    assert(!child_ready(&foreign));
    second.state=TASK_ZOMBIE;assert(child_ready(&foreign));
    foreign.pid=41;assert(child_ready(&foreign)); /* no eligible child => error retry */
    process_wait_release_claim(&foreign);
    assert(first.synchronous_wait_process==9 && first.synchronous_waiter==17);
    own.owner=10;process_wait_release_claim(&own);
    assert(first.synchronous_wait_process==9);
    own.owner=9;process_wait_release_claim(&own);
    assert(!first.synchronous_wait_process && !first.synchronous_waiter && irq_flags==0x200);
    assert(process_wait_claim_available(&first,&foreign));
    first.state=TASK_RETIRING;assert(!process_wait_target(&first,&own));
    first.state=TASK_RESERVED;assert(!process_wait_target(&first,&own));
    first.state=TASK_UNUSED;assert(!process_wait_target(&first,&own));
    puts("Production PROCESS wait claims: identity, unrelated-child readiness, retirement and balanced error/cancel release: PASS (host CPU model)");
    return 0;
}
