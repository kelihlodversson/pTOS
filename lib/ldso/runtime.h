/* Internal freestanding interpreter interface (#660). */
#ifndef LDSO_RUNTIME_H
#define LDSO_RUNTIME_H
#include "image.h"
#include "graph.h"
#include "relocate.h"
#include "startup.h"
int64_t dl_syscall(uint32_t fn, uint64_t a, uint64_t b, uint64_t c,
                    uint64_t d, uint64_t e, uint64_t f);
int dl_bootstrap(const uint32_t *stack);
uint32_t dl_runtime(const uint32_t *stack);
void dl_runtime_fini(void);
#endif
