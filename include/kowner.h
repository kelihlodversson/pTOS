/* Single-CPU owner metadata locks. Kernel continuations never switch with an
 * owner held. SMP must supply inter-CPU acquisition before enabling CPUs. */
#ifndef KOWNER_H
#define KOWNER_H
#include "portab.h"

typedef enum { KOWNER_PROCESS=10, KOWNER_MAPPING=20,
               KOWNER_ADMISSION=30, KOWNER_RUNQUEUE=40 } KOWNER_RANK;
typedef struct {
    UQUAD clock,ticket;
    UWORD depth,rank;
} KOWNER_CPU;
typedef struct {
    KOWNER_CPU *cpu;
    uintptr_t flags;
    UQUAD ticket,parent;
    UWORD depth,rank,previous;
} KOWNER_GUARD;

#if CONF_WITH_MULTITHREADING
/* CPU selection and local IRQ mechanics remain in the existing backend. */
KOWNER_CPU *kowner_cpu(void);
uintptr_t kowner_irq_save(void);
void kowner_irq_restore(uintptr_t flags);
void kowner_enter(KOWNER_GUARD *guard,KOWNER_RANK rank);
void kowner_leave(const KOWNER_GUARD *guard);
BOOL kowner_locked(void);
BOOL kowner_holds(KOWNER_RANK rank);
void kowner_assert_unlocked(void);
#else
static inline void kowner_enter(KOWNER_GUARD *guard,KOWNER_RANK rank)
{ (void)guard;(void)rank; }
static inline void kowner_leave(const KOWNER_GUARD *guard) { (void)guard; }
static inline BOOL kowner_locked(void) { return FALSE; }
static inline BOOL kowner_holds(KOWNER_RANK rank) { (void)rank;return FALSE; }
static inline void kowner_assert_unlocked(void) { }
#endif
#endif
