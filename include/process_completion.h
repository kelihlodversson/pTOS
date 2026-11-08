/* Non-consuming PROCESS completion data. Contains no pointers or wait rights. */
#ifndef PROCESS_COMPLETION_H
#define PROCESS_COMPLETION_H
#include "portab.h"
#define PROCESS_UNSTARTED 0UL
#define PROCESS_RUNNING 1UL
#define PROCESS_COMPLETED 2UL
#define PROCESS_ABANDONED 3UL
#define PROCESS_COMPLETION_NONE 0UL
#define PROCESS_COMPLETION_NORMAL 1UL
#define PROCESS_COMPLETION_SIGNAL 2UL
typedef struct {
    ULONG number_low, number_high;
    LONG pid;
    ULONG state, kind;
    UWORD status; /* Full normal exit status, including 0xffff. */
    WORD signal; /* Explicit signal; never inferred from status bits. */
    ULONG order_low, order_high; /* Zero until delivery-ready; immutable terminal order. */
} __attribute__((aligned(4))) PROCESS_COMPLETION;
typedef char PROCESS_COMPLETION_LAYOUT[
    sizeof(PROCESS_COMPLETION)==32 && __alignof__(PROCESS_COMPLETION)==4 &&
    offsetof(PROCESS_COMPLETION,number_low)==0 && offsetof(PROCESS_COMPLETION,number_high)==4 &&
    offsetof(PROCESS_COMPLETION,pid)==8 && offsetof(PROCESS_COMPLETION,state)==12 &&
    offsetof(PROCESS_COMPLETION,kind)==16 && offsetof(PROCESS_COMPLETION,status)==20 &&
    offsetof(PROCESS_COMPLETION,signal)==22 &&
    offsetof(PROCESS_COMPLETION,order_low)==24 && offsetof(PROCESS_COMPLETION,order_high)==28 ? 1 : -1];
#endif
