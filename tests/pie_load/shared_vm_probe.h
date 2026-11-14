/* Private native regression mailbox shared by distinct PROCESSes. */
#ifndef SHARED_VM_PROBE_H
#define SHARED_VM_PROBE_H
#include <stdint.h>
struct shared_vm_probe {
    volatile uint32_t staged,environment,pid,child_pid,gate,done,error;
    volatile uint32_t started,grandchild,grand_environment;
    volatile uint32_t worker_stack,worker_tid,second_tid,worker_started,steps;
    volatile uint32_t controller_result;
    volatile uint32_t core_limit,data_limit,stack_limit,same_limit,resize_limit;
    volatile uint32_t late_runtime,descendant_pid,descendant_ready,descendant_gate,descendant_done;
    volatile uint32_t unrelated_runtime;
    volatile uint32_t descendant_mapping_prefixes,descendant_preparation_prefixes;
};
#endif
