# Shared open-file descriptions (#1002)

This audit starts from pTOS `20db343d71d1b9bae51947f921ca70154407ddce`. The
existing PROCESS descriptor namespace is the starting point for the work.
A binding grants one close right and identifies an SFT slot and incarnation.
Bindings that share that SFT owner also share the backend's open position.
The owner and dispatch below the binding are the migration boundary.

## Integration sequence

1. Record current descriptor admission, cursor placement, dispatch and final
   release for FAT, PFS and BIOS devices. Identify the FreeMiNT adapter boundary.
2. Introduce a typed backend interface on the existing SFT owner and preserve
   its exact incarnation and reference accounting throughout the transition.
3. Migrate file I/O and standard-stream dispatch in increments, covering FAT
   and virtio-9p and comparing managed and legacy caller behavior.
4. Coordinate the `FILEPTR`/`DEVDRV` adapter with #978 and define the retention
   boundary required by #929. Validate the result on native MMU and small-ROM
   configurations before removing transitional paths.

Keep `OPNFILES`, the GEMDOS handle ABI and the FAT `OFD` size constraint.
The descriptions and their backend references will continue to use the
existing PROCESS admission and retirement mechanisms. Directory iteration
and DTA/search ownership remain under their current contracts.

## Descriptor admission and SFT ownership

The system has 75 SFT slots and six standard handles. A managed PROCESS owns
a table of generations and local numbers. `process_files_resolve()` validates
a live binding, checks the SFT incarnation and requires an accessible
ordinary reference. Each positive binding has one independently disposable
reference. GEMDOS `Fdup` accepts a standard handle on this baseline. With
managed descriptors it can allocate a new local number without occupying
another SFT slot. `Fforce` takes a checked reference before closing the
destination and revalidates the empty destination generation across callbacks.
`PD_UFT` is a mirror for managed standards and the routing table for legacy
standards. Legacy positive indexes refer to the system table. `f_own` is a
slot and retirement marker. The checked legacy import also proves that an
explicit reference remains after subtracting aliases and nonordinary
references. [process_files.c](../bdos/process_files.c) · [fshand.c](../bdos/fshand.c) · [fsclose.c](../bdos/fsclose.c)

`fs_sft_claim()` assigns a nonrecycled incarnation in USER_ASPACE builds.
The live count `f_use` includes ordinary, staged and residual close rights.
Snapshot preparation adds a checked staged right for each inherited binding.
Publication reclassifies each staged right as ordinary without changing the
total. Abort releases admission before disposing the acquired prefix.
The PROCESS epoch, generation, operation count and source/target identities
remain the authority for this transaction. Shared-VM PROCESS clones and
forked processes own their new bindings. Threads in one PROCESS use the
same table. The file snapshot, DTA/search snapshot, and CWD inheritance have
distinct policies. Same-PROCESS Pexec(200) keeps file bindings.
[process_files.h](../bdos/process_files.h) · [process_files.c](../bdos/process_files.c)

In USER_ASPACE builds, `fsclose.c` owns the exact ordinary, staged and
cleanup close protocol, including trusted boot callers. An admitted close
pins the slot's incarnation with `f_close_busy`, captures the enclosing disk
error scope and distinguishes a retained error from consumption. Bounded
disposal can transfer an independently owned reference to the existing
residual cleanup ticket without increasing `f_use`. A managed binding is
removed before the backend runs. A retained close restores only its captured
empty generation, or transfers the old right if another operation replaced
it. A consuming release may report an error. The backend must return
normally after consumption, and a nonlocal exit at that point is a
fail-stop. Shutdown retires managed tables first, then sweeps exact legacy
and residual owners. [fsclose.c](../bdos/fsclose.c) · [process_files.c](../bdos/process_files.c) · [proc.c](../bdos/proc.c) · [xshutdown.c](../bdos/xshutdown.c)

`CONF_WITH_PROCESS_FILES` is derived from native multithreading. Its separate
test option enables private test controls. USER_ASPACE can still be built
with legacy handle routing. Non-MMU builds use the direct close path without
incarnation, staged and residual fields. The 192 KiB profile defaults to
traditional FAT deletion, while extended unlink lifetimes and backing pins
have separate gates. PFS is optional on x32. FAT keeps the full-width
directory pointer in its cookie independently of the iterator slot.
FAT works with and without PFS enabled. [bdos/Kconfig](../bdos/Kconfig) · [fs/Kconfig](../fs/Kconfig)

## Baseline open descriptions and dispatch

| Backend | Open and cursor | Duplication and release |
| --- | --- | --- |
| FAT | `opnfil()` creates one OFD per open. The OFD holds mode, byte offset and cluster/record cursor. Separate opens share the DFD for file length, attributes, cluster chain and unlink state. | Managed aliases share one SFT slot and one OFD. Legacy `Fforce` shares a slot and increments its count. Legacy `Fdup` of a standard file creates another slot pointing to the same OFD. Final slot deletion checks for any other slot using the OFD before putting the handle group's OFD reference. |
| PFS file | `pfs_do_open/create` separates the path/directory cookie from the open cookie returned by the provider. On success the SFT takes that file cookie by value and owns `pos`, initialized to zero. | A managed duplicate or legacy `Fforce` shares the same slot and cursor. Legacy `Fdup` rejects a standard PFS file with `EIHNDL`. The core does not copy the cookie to a second live slot. Alias close can call the driver's optional flush hook. The final use invokes the consuming release. |
| BIOS character device | Special names `CON:`, `AUX:`, `PRN:` and the default standards route to BIOS values. Managed bindings can store a BIOS value directly with incarnation zero. Legacy UFT entries can hold it directly, and legacy `Fdup` represents it as a tagged `f_ofd` in a new slot. | There is no per-open cursor or filesystem cookie. Closing a direct device consumes no filesystem reference. Closing a tagged slot releases its usage count. Closing a standard alias follows the default-handle restoration policy. |

The FAT provider under PFS sets `native_handles`. Its open/create calls
`opnfil()` and returns the resulting GEMDOS handle in `index`. PFS returns it
without another SFT allocation. This can be a PROCESS-local number. Treating
it as a raw SFT number or wrapping it in a second cookie would break ownership.
The standard PFS allocation path transfers a real cookie only on success.
The caller closes and releases it if SFT or PROCESS admission fails.
Comments in the legacy duplication and PFS close paths refer to copied PFS
cookies. The current code shares a slot or rejects the duplicate.
[fsopnclo.c](../bdos/fsopnclo.c) · [pfs.c](../fs/pfs.c) · [pfs.h](../fs/pfs.h) · [fatfs_pfs.c](../fs/fatfs_pfs.c)

FAT calls `ixclose(CL_KEEP)` during alias close for dirty-state handling.
Slot retirement and OFD release are separate, and an OFD held by a mapping
can finish later. PFS similarly separates the optional flush callback from
the final consuming callback. USER_ASPACE can report a final `release_file`
error after consumption. The legacy path uses the void `release` interface.
These distinctions must survive the move to typed operations.
[fsopnclo.c](../bdos/fsopnclo.c) · [fsclose.c](../bdos/fsclose.c) · [pfs.h](../fs/pfs.h)

GEMDOS dispatch in `osif_impl()` interprets direct and slot-tagged BIOS
values and provides the character-device I/O rules. Device code `-1` is
console, `-2` aux and `-3` printer. A negative signed pointer can also be a
valid higher-half OFD, so the existing tag test recognizes only those three
values. A redirected Ccon/Caux call uses file I/O or EOF testing downstream.
Managed entry passes the local handle to downstream file I/O even after
classifying it at the trap boundary. `syshnd()`, `getofd()` and
`getpfsslot()` then resolve the handle for the operation. A common typed
resolver can remove this representation split. It must keep the character
conversion, I/O counts and error behavior at the entry points that own them.
[bdosmain.c](../bdos/bdosmain.c) · [console.c](../bdos/console.c) · [fsmain.c](../bdos/fsmain.c) · [fsio.c](../bdos/fsio.c)

FAT I/O and seek mutate the OFD and update the DFD on writes that extend a
file. The FAT write path keeps the legacy TOS behavior for an existing file
opened read-only. PFS read/write passes the cookie's offset to the backend and
advances it by the positive count the callback returns. PFS seek bounds the
result using `fstat` and a signed 32-bit position. virtio-9p obtains an
independent fid on each open, performs positioned transfers in chunks and
clunks the fid on final release. It has no flush hook. As implemented, a
negative result after earlier successful chunks is returned as an error and
the PFS cursor does not advance. A dispatch refactor must preserve existing
partial-I/O and error conventions or change them in a separately tested
increment. [fsio.c](../bdos/fsio.c) · [pfs.c](../fs/pfs.c) · [virtio_9p_xfs.c](../fs/virtio_9p_xfs.c)

## Mapping pins, revocation and execution boundaries

Native file-backed mappings currently retain a FAT OFD through `FILE_BACKING`.
They can outlive the last SFT slot. Positioned paging saves and restores the
ordinary OFD cursor, including its record, cluster and byte fields, across
disk errors. Current backings can be reused by DFD identity and carry an
independent size snapshot. Closing a descriptor must preserve that distinction. PFS mapping
is currently rejected. FAT media invalidation marks the old object lost,
detaches its media/dir pointers, revokes the exact SFT references and removes
affected bindings before reuse. A surviving backing pin may release the old
object but cannot reclaim clusters on the replacement medium.
[fileback.c](../bdos/fileback.c) · [fsopnclo.c](../bdos/fsopnclo.c) · [fsclose.c](../bdos/fsclose.c)

Qualified native profiles use USER timer preemption by default. Kernel
interruptions defer scheduling. Admission publication and count changes use
the accepted single-CPU execution model. Kernel callbacks may reenter close,
retire a PROCESS or replace a descriptor. A filesystem operation must retain
its exact owner across any such gap and keep callbacks, I/O, allocation and
final destruction outside a leaf critical section. The GEMDOS entry restores
its enclosing disk-error state before the cancellation point on exit.
Future SMP will need explicit exclusion for the combined lookup, reference
acquisition and publication. It can use the same owner and lifetime states.
[bdos/Kconfig](../bdos/Kconfig) · [bdosmain.c](../bdos/bdosmain.c) · [process_files.c](../bdos/process_files.c) · [kowner.h](../include/kowner.h)

## FreeMiNT adapter boundary

The upstream `fcookie` identifies filesystem, device and node. `FILEPTR`
adds links, shared flags and position, device-private data and the
`DEVDRV` pointer. `do_open()` resolves a cookie, obtains the device and its
private value via `getdev`, initializes the FILEPTR and calls device open.
`do_dup()` and `Fforce` share the FILEPTR and increment its link count.
The descriptor's close-on-exec flag lives in the descriptor table.
[FreeMiNT file.h](https://github.com/freemint/freemint/blob/2afc6cd66501d6f1a959f5709626c3ffaae1ff56/sys/mint/file.h) · [FreeMiNT fsops.h](https://github.com/freemint/freemint/blob/2afc6cd66501d6f1a959f5709626c3ffaae1ff56/sys/mint/fsops.h) · [FreeMiNT k_fds.c](https://github.com/freemint/freemint/blob/2afc6cd66501d6f1a959f5709626c3ffaae1ff56/sys/k_fds.c)

Upstream decrements `links` and calls the driver's `close(FILEPTR *, pid)`
for each admitted close. Driver callbacks can use the remaining link count.
The cookie and FILEPTR are released at the final link boundary. Upstream
also handles per-process unselect and TTY hangup state in this path. The
adapter needs to distinguish a link-release notification from final resource
disposal and keep any driver-retained operation accounted for in the pTOS
owner. A driver's FILEPTR link field must have defined synchronization and
overflow rules with that owner. Each #978 operation needs verified layout,
width and `_cdecl` conventions on x32, ARM and m68k. DEVDRV's optional tail
requires its published size check. `ioctl` and readiness operations can
borrow a retained description. The adapter preserves existing GEMDOS
entry layouts through explicit conversions for the driver-visible types.
[FreeMiNT fsops.h](https://github.com/freemint/freemint/blob/2afc6cd66501d6f1a959f5709626c3ffaae1ff56/sys/mint/fsops.h) · [FreeMiNT k_fds.c](https://github.com/freemint/freemint/blob/2afc6cd66501d6f1a959f5709626c3ffaae1ff56/sys/k_fds.c)

## Implementation

`fs_handle_lookup()` resolves a legacy number or a managed PROCESS binding
to an SFT borrower or a BIOS device value. Legacy BIOS aliases have a
numeric device field in the SFT. Device I/O, FAT and PFS use this lookup
for read, write, seek and standard redirection. Close still consumes the
admitted reference through the existing backend and cleanup protocol.
Managed duplication and inheritance share the exact slot. The small-ROM
path retains direct FAT, including the legacy multi-slot FAT duplicate.

PFS file operations retain the admitted SFT incarnation before invoking a
driver and keep it through the cursor update. A nested operation on the same
PFS cursor returns `EACCDN`. A callback can consume the last descriptor
without freeing the cookie. Dropping a temporary pin skips the flush while
other owners exist and uses bounded final disposal otherwise. The disk-error
guard unwinds the pin before restoring the original error continuation.
The scheduler defers a pending group exit until the busy flag and file pin
are released. The enclosing GEMDOS boundary delivers it after cleanup.
An open or create that fails slot or binding admission releases the
unpublished cookie even if the driver's flush raises a disk error. Its
result remains the original admission error.

`Fcntl(F_GETFL)` reports the open access mode through the native USER_ASPACE
GEMDOS path. The mode comes from the FAT OFD, the PFS slot, or the BIOS
device contract. The provider-specific ioctl entry takes a kernel-owned
buffer with a defined size and runs under the same pin as regular PFS I/O.
New user-visible ioctl commands require explicit ABI and buffer validation.
The retained readiness query validates the SFT incarnation after a binding
closes. FAT regular files provide a level check. PFS providers can supply a
nonblocking query. Providers without the method return `EINVFN`. Wake
registration and BIOS/device polling remain part of #929.

FAT mapping pins still use the OFD backing lifecycle, including media
revocation and release after an SFT slot disappears. PFS mapping remains
unsupported. #978 will use the provider entry points to adapt a real
FreeMiNT driver and establish the driver-visible layout and callback ABI.

## Verification boundary

Use `tools/test-fs-close.py` and `tools/test-process-files.py` as hosted
controls for exact references, rollback, stale identity, reentrant close,
media revocation and shutdown. Native controls require x32 FAT, ARM FAT/PFS,
m68k MMU FAT/PFS, and representative non-MMU and small ROMs. Compare
default-on preemption with cooperative execution and keep the existing
public clone, fork and Pexec regressions. Record the real driver and readiness
evidence with the #978/#929 adapter work.

At the starting source the hosted close/reference and PROCESS file suites
passed. The PROCESS file suite also passed with GCC 14 fortification and
with ASan/UBSan. LeakSanitizer could not run in the workspace sandbox, so
the successful sanitizer pass used `detect_leaks=0`. The existing tests
run production ownership code with modeled disk and execution state.
They establish a migration baseline for the later native acceptance runs.
