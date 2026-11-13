/* Private native regression mailbox shared by distinct PROCESSes. */
#ifndef SHARED_VM_PROBE_H
#define SHARED_VM_PROBE_H
#include <stdint.h>
struct shared_vm_probe {
    volatile uint32_t staged,environment,pid,child_pid,gate,done,error;
};
#endif
