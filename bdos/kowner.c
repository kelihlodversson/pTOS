/* The domain locks protect existing owner ledgers; this owns no objects and
 * is not another admission, PROCESS or mapping registry. All sections are
 * bounded metadata only. Local IRQ exclusion is the single-CPU backend,
 * while explicit ownership/rank/LIFO checks enforce the owner contract. */
#include "emutos.h"
#if CONF_WITH_NATIVE_SCHED
#include "kowner.h"
#include "biosext.h"

typedef struct { KOWNER_CPU *cpu;UWORD depth; } DOMAIN_LOCK;
static DOMAIN_LOCK domains[4];
static DOMAIN_LOCK *domain(UWORD rank)
{
    if(rank<10 || rank>40 || rank%10)panic("owner: invalid rank");
    return &domains[rank/10-1];
}
KOWNER_GUARD kowner_enter(KOWNER_RANK rank)
{
    KOWNER_GUARD guard;
    DOMAIN_LOCK *lock;
    guard.flags=kowner_irq_save();
    guard.cpu=kowner_cpu();
    lock=domain((UWORD)rank);
    if(!guard.cpu || (guard.cpu->depth && rank<guard.cpu->rank))
        panic("owner: lock order inversion");
    if(lock->depth && lock->cpu!=guard.cpu)
        panic("owner: remote CPU without synchronization backend");
    if(guard.cpu->clock==~(UQUAD)0 || guard.cpu->depth==0xffffU || lock->depth==0xffffU)
        panic("owner: scope identity exhausted");
    guard.parent=guard.cpu->ticket;guard.previous=guard.cpu->rank;
    guard.ticket=++guard.cpu->clock;guard.depth=++guard.cpu->depth;
    guard.rank=(UWORD)rank;
    guard.cpu->ticket=guard.ticket;guard.cpu->rank=guard.rank;
    lock->cpu=guard.cpu;lock->depth++;
    return guard;
}
void kowner_leave(KOWNER_GUARD guard)
{
    DOMAIN_LOCK *lock=domain(guard.rank);
    if(guard.cpu!=kowner_cpu() || !guard.cpu->depth ||
       guard.cpu->ticket!=guard.ticket || guard.cpu->depth!=guard.depth ||
       guard.cpu->rank!=guard.rank || lock->cpu!=guard.cpu || !lock->depth)
        panic("owner: stale or non-LIFO release");
    if(!--lock->depth)lock->cpu=NULL;
    guard.cpu->depth--;guard.cpu->ticket=guard.parent;guard.cpu->rank=guard.previous;
    kowner_irq_restore(guard.flags);
}
BOOL kowner_locked(void) { return kowner_cpu()->depth!=0; }
BOOL kowner_holds(KOWNER_RANK rank)
{ DOMAIN_LOCK *lock=domain((UWORD)rank);return lock->depth && lock->cpu==kowner_cpu(); }
void kowner_assert_unlocked(void)
{ if(kowner_locked())panic("owner: callback, destruction or handoff under owner lock"); }
#endif
