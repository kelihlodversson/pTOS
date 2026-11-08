/* Generic PROCESS identity snapshots. No pointers or GUI client identities. */
#ifndef PROCESS_IDENTITY_H
#define PROCESS_IDENTITY_H
#include "portab.h"
#define PROCESS_IDENTITY_CURRENT 0UL
#define PROCESS_IDENTITY_NUMBER 1UL
#define PROCESS_IDENTITY_PID 2UL
typedef struct {
    ULONG number_low, number_high; /* Non-recycled PROCESS number; zero is invalid. */
    LONG pid;                     /* Recyclable MiNT PID, never an AES client ID. */
    ULONG selector;                /* Query key; output is CURRENT (zero). */
} __attribute__((aligned(4))) PROCESS_IDENTITY;
typedef char PROCESS_IDENTITY_LAYOUT[
    sizeof(PROCESS_IDENTITY) == 16 && __alignof__(PROCESS_IDENTITY) == 4 &&
    sizeof(((PROCESS_IDENTITY *)0)->number_low) == 4 &&
    sizeof(((PROCESS_IDENTITY *)0)->number_high) == 4 &&
    sizeof(((PROCESS_IDENTITY *)0)->pid) == 4 &&
    sizeof(((PROCESS_IDENTITY *)0)->selector) == 4 &&
    offsetof(PROCESS_IDENTITY,number_low) == 0 &&
    offsetof(PROCESS_IDENTITY,number_high) == 4 &&
    offsetof(PROCESS_IDENTITY,pid) == 8 &&
    offsetof(PROCESS_IDENTITY,selector) == 12 ? 1 : -1];
#endif
