/* pTOS GEMDOS THREAD lifecycle ABI; this is not the POSIX pthread library. */
#ifndef PTOS_THREAD_CALLS_H
#define PTOS_THREAD_CALLS_H
#define GEMDOS_PTHJOIN 0x191
#define GEMDOS_PTERMGRP 0x192 /* WORD status; successful managed call never returns */
/* long Pthjoin(long tid, int32_t *status): zero success; optional native
 * 32-bit output contains the raw zero-extended 16-bit THREAD exit status.
 * Same-PROCESS managed TIDs only. Unknown/reaped/unpublished: ESRCH;
 * self/cyclic join: EDEADLK; competing claim: EACCDN; bad output: EIMBA.
 * No public detach API or asynchronous signal interruption is implemented. */
#endif
