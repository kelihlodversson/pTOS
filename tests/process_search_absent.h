/* Descriptor-only fixtures have no trusted searches. Search ownership is
 * exercised separately with the production KPROC cache and PFS table. */
LONG kproc_search_snapshot_prepare(KPROC *source,KPROC *target,UQUAD ticket,
                                  KPROC_SEARCH_SNAPSHOT **out)
{ (void)source;(void)target;(void)ticket;(void)out;return E_OK; }
BOOL kproc_search_snapshot_valid(const KPROC_SEARCH_SNAPSHOT *snapshot)
{ return snapshot==NULL; }
void kproc_search_snapshot_commit(KPROC_SEARCH_SNAPSHOT *snapshot)
{ (void)snapshot; }
void kproc_search_snapshot_end(KPROC_SEARCH_SNAPSHOT **snapshot)
{ (void)snapshot; }
void kproc_search_retire(KPROC *process) { (void)process; }
void kproc_search_snapshot_invalidate(KPROC_SEARCH_SNAPSHOT *snapshot,WORD drive)
{ (void)snapshot;(void)drive; }
