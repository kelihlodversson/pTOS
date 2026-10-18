# x32 loaded AES accessories in ring 3 (#389)

Status: proposed design; implementation has not started.
Epic: #548. Implementation branch: feature/389-x32-accessory-execution.
Draft PR: #395, targeting epic/548-vdi-aes-ring3.

## Intent and acceptance

A loaded x32 .ACC must execute as a private ring-3 process, initialize through
libcmini accessory startup, call GEMDOS and AES, wait for an AES event, and
resume without corrupting the desktop or another accessory. Exit, a user
fault, and desktop restart must release its resources from a safe kernel
context. Ordinary Pexec and the ARM/m68k launch paths must keep working.

This is the accessory lifecycle part of #548. #407 describes the same launch
mechanism and should be implemented with #389 rather than with a second launcher.
The work uses the existing C/assembly OS and its tracked process-memory APIs.
No native LP64 userspace, new scheduler policy, demand-paging redesign, SMP,
or VDI callback implementation is included.

## Existing contracts and gaps

- doc/process-entry.txt already defines ENTRY_PROGRAM=0 and ENTRY_ACCESSORY=1.
  RDI is the basepage; RSI is the explicit entry type. At x32 entry RSP+8 is
  divisible by 16. libcmini must preserve that convention and select _acc_main.
- load_one_acc() loads through pgmld() and calls pstart(gotopgm, ...).
  gotopgm() in aes/arch/x86_64/gemasm.S deliberately panics today.
- Ordinary program launch prepares a KPROC-owned address space, user stack,
  and kernel stack. gouser() saves a launch continuation and restores the
  launcher's CR3, syscall stack, saved user RSP and GS state on termination.
- AES dsptch()/switchto() currently switch kernel coroutine stacks only.
  They do not select GEMDOS run, CR3 or per-process syscall/termination state.
- Ring-3 trap class 2 is rejected in bios/arch/x86_64/trap.c. Removing that
  check without opcode-aware parameter validation is unsafe.
- PR #395 had no product-code diff when resumed. Its old branch was merged
  with the epic baseline without rewriting history.

## Proposed lifecycle

### Load and prepare

Use the normal ELF/GEMDOS loader and KPROC process preparation facilities.
Keep a kernel-owned association between an AESPD and the loaded accessory's
PD; never recover ownership or launch state from user-writable basepage fields.
The association tracks preparing, runnable, waiting and dead lifecycle states.

Before publishing a runnable accessory, prepare its image, basepage,
environment, private user stack and kernel stack using the tracked virtual
regions and backing-page ownership APIs. ABI-visible addresses are low virtual
addresses; physical backing need not be low. Retain segment permissions and
private writable state. A failed load/preparation must close handles, unwind
inherited references and release every allocation without leaving an AES task
runnable. The builder chooses the precise helper names and metadata layout.

### First entry and scheduling

Replace the panic only once preparation and a saved termination continuation
exist. Enter the validated image entry with the recorded basepage in RDI,
ENTRY_ACCESSORY in RSI and the prepared user RSP. Reuse the ordinary launch
machinery through a shared helper where practical; preserve ENTRY_PROGRAM=0
for Pexec. Do not infer the type from p_parent or an accessory filename.

A suspended accessory keeps its kernel syscall frames and termination
continuation on storage owned by that accessory. A single global continuation
must not accidentally resume another task. At each AES task switch save and
restore the GEMDOS process identity, CR3, syscall kernel-stack selection,
saved user RSP and syscall/GS state along with the coroutine stack. Internal
kernel AES tasks use their own recorded kernel context. The dispatcher must
execute on supervisor mappings available across these address spaces.

The scheduler must switch identity and mappings before it resumes a task's
frames. Event waits may suspend a validated AES syscall and later finish it;
copyout belongs to the original caller. IRQ handlers continue to queue AES
work; they never invoke user accessory code directly.

### Exit, faults and restart

Pterm and fatal user faults return to the owning accessory's saved AES
continuation. Unlink the accessory from ready/wait/event structures, cancel
its outstanding work, close process-owned handles and destroy tracked process
resources exactly once. Release its active kernel stack only after switching
to a live kernel/scheduler stack. Desktop restart drains accessory contexts
before clearing AES global storage or rebuilding process stacks. Restart and
exit must not longjmp into a freed or overwritten continuation.

## Dependency boundaries

The launch preparation and entry probes can be developed before safe GEM
syscalls, but the #389 acceptance test cannot pass until #404 supplies the
validated AES boundary. That boundary must cover every pointer reached by the
probe's AES calls, including arrays, strings and event-message buffers, and
return a defined error for invalid input. This design does not authorize
turning off the blanket rejection for unvalidated opcodes.

#427 establishes the native VDI control layout, #403 splits VDI client and
hardware services, and #432 supplies safe user callback delivery. Accessory
calls needing those facilities wait for them. Keep the initial accessory
probe free of user callback registrations. Coordinate the libcmini #15 startup
consumer and verify its current implementation before using it as evidence.
The builder may use a small assembly entry probe initially so entry register
checks do not depend on C startup; full acceptance still requires libcmini.

## Verification plan

1. Establish a pc-x86_64 baseline build and the existing QEMU/OVMF process
   regression result before changing the launch or scheduler paths.
2. Use a loaded x32 .ACC entry probe to capture RDI, RSI and RSP before C
   startup. Check basepage identity, ENTRY_ACCESSORY and stack alignment,
   then a GEMDOS call and termination through the AES continuation.
3. Once the safe AES boundary is available, test libcmini initialization,
   appl_init, event wait/delivery, yield/resume, and exit with two accessories
   and the desktop active. Each process must retain its own writable state.
4. Exercise bad blocks/pointers, a user fault, failed preparation, repeated
   exit and repeated desktop restart. Check tracked resource counts against
   baseline after cleanup; no kernel fault or stale runnable task is allowed.
5. Repeat the ordinary x32 Pexec regression. Build representative ARM and
   m68k configurations for shared-code changes. Run make gitready before
   committing implementation. Add the loaded-accessory QEMU/OVMF case to CI.

A build alone does not establish ring-3 execution or scheduler correctness.
No implementation or runtime success is claimed by this design commit.
