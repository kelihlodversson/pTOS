/* Private GEMDOS Pclone test tags. Never a Pexec(200) or public clone ABI. */
#ifndef EXEC_HANDOFF_TEST_H
#define EXEC_HANDOFF_TEST_H
#include <stdint.h>
#define EXEC_HANDOFF_RUN   0xc9700200UL /* CLONE_ARGS, zero flags/outputs/TLS */
#define EXEC_HANDOFF_QUERY 0xc9700300UL
#define EXEC_HANDOFF_FAILURES 10
typedef struct {
    uint32_t process[2], execution[2], root[2];
    uint32_t pid, tid, handoffs;
} EXEC_HANDOFF_TEST_STATE;
#endif
