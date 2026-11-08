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
} PROCESS_IDENTITY;
#endif
