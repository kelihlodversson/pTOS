/* Private GEMDOS Pclone test tags. Never a Pexec(200) or public clone ABI. */
#ifndef EXEC_HANDOFF_TEST_H
#define EXEC_HANDOFF_TEST_H
#include <stdint.h>
#define EXEC_HANDOFF_RUN   0xc9700200UL /* CLONE_ARGS, zero flags/outputs/TLS */
#define EXEC_HANDOFF_QUERY 0xc9700300UL
#define EXEC_HANDOFF_FAILURES 11
typedef struct {
    uint32_t process[2], execution[2], root[2];
    uint32_t pid, tid, handoffs;
} EXEC_HANDOFF_TEST_STATE;
/* Pexec(200) test probes (CONF_WITH_EXEC_REPLACEMENT_TEST), same transport.
 * ARM + (kind << 8) + stage arms one injected failure for the next
 * Pexec(200): kind 1 image bundle (1..8), 2 commit (1..10), 3 receipt (1..4);
 * ARM alone disarms all. QUERY fills EXEC_REPLACE_TEST_STATE for the caller.
 * WAIT + baseline, with the child PID as the address, blocks until that
 * PROCESS's committed-exec generation differs from baseline or it is
 * terminal; returns generation | terminal << 8 | ready evaluations << 16. */
#define EXEC_REPLACE_ARM   0xc9700400UL
#define EXEC_REPLACE_QUERY 0xc9700800UL
#define EXEC_REPLACE_WAIT  0xc9700900UL
typedef struct {
    uint32_t process[2], root[2], generation[2];
    uint32_t pid, tid;
} EXEC_REPLACE_TEST_STATE;
#endif
