PROCESS search snapshots (#984)
===============================

The existing PROCESS file snapshot owns admission, epoch validation, staging,
seal and rollback for both descriptors and trusted searches. STANDARD handoff
(Pexec) leaves searches fresh. ALL handoff stages each trusted DTA and each
active search before any child publication. Unsupported providers return EINVFN;
unexplained associations, stale owners and in-flight operations fail admission.
Public fork remains gated.

The DTA key is the original logical user address inherited by #1000, including
its original mapping/backing. Kernel construction PD allocations provide no DTA
or search authority. Fsfirst/Fsnext stage the DTA in kernel memory across provider
calls and hold a revocable KASPACE lease: a mapping mutation prevents copyout.
Trusted headers are captured before any eviction callback. Eviction, exhaustion,
failed continuation, media retirement and PROCESS exit consume the matching
search owner; retained PROCESS identity alone never retains its heavy PD state.

Legacy FAT headers contain logical cluster/offset/pattern state. The existing
DND cookie-reference count now also protects their directory cache lifetime;
preparation acquires a separate child hold. PFS searches own PFS_ITERATOR values
in the existing search table. A staged slot is reserved under the existing
PROCESS snapshot ticket and cannot serve Fsnext before commit.

Provider contract (reusable by future Dopendir/Dreaddir, #198):

* clone_iterator preserves its source and acquires a fresh directory reference
  and independent position owner. It returns an empty output on failure, and
  must unwind acquisitions before any nonlocal error. Opaque cookies/pool slots
  are never copied as ownership.
* iterator_valid is a pure query: no allocation, I/O, callbacks or reentry. It is
  permitted during the allocation-free publication step.
* close_iterator consumes exactly once, including when it reports an error; it
  cannot nonlocally escape. The owner is detached before invoking it.

FAT cloning allocates a fresh directory cookie and pool slot, copying only the
logical DTA header. 9P cloning duplicates the directory fid, allocates a fresh
pool slot and copies only the logical wire offset. Failed fid cleanup uses the
existing transport quarantine.

Provider calls retain busy slots through retirement. A guarded FAT disk error
unwinds the busy operation before osif handles E_CHNG and destroys the directory
tree. Direct FAT media retirement from inside an active provider stack violates
that contract and is rejected before tree destruction. Normal media retirement
closes published/staged FAT searches and invalidates their trusted witnesses and
PROCESS epochs before the DND tree is freed. No additional registry is involved.

Validation and review
---------------------

`tools/test-process-search.py` exercises the production PROCESS transaction,
KPROC trusted cache and PFS owners with a counted provider. It covers exact
header inheritance/anti-forgery, independent continuation, both cleanup orders,
metadata/provider failure, full-table partial rollback/retry, cache eviction,
unsupported/stale associations, reentrant abort/retirement/close, close errors,
and published/staged media invalidation. Its second fixture executes production
Fsfirst staging and KASPACE leases with a model MMU, including cross-page output,
same-address remapping, permission revocation, checked backing failure and disk
unwind. Both fixtures assert final resource conservation.

The native PROCESS-files regression lane runs private-root snapshots through a
private test transport. It covers default basepage, stack and cross-page heap
DTAs, parent/child COW byte isolation, both continuation orders, all existing
constructor failure prefixes with active searches, and a source that exits
before its private descendant resumes. FAT and standard-PFS FAT run on x32,
ARM MMU and m68k MMU where supported; standard-PFS lanes also cover the 9P Q:
provider. Public Pfork/Pvfork are not enabled by this transport.

Independent kernel/scheduler, filesystem, MMU and resource-ownership reviews
found and resolved callback-related DTA/iterator lifetime issues. The review
closures and exact CI head are recorded in the PR. CI runs normal, fortified and
ASan/UBSan/leak checks, native regressions, ROM builds and compatibility jobs.
