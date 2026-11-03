# x32 libaes shell compatibility audit

This is the shell portion of the #661 initial x32 cutover contract, audited in
#871 after merged #870. It records supported behavior and explicit follow-ups;
it does **not** claim complete AES4, MagiC, Geneva, N.AES or XaAES compatibility.
The remaining API families and final Kconfig/legacy-service removal keep their
independent gates in [the compatibility matrix](libaes-compatibility.md).

Supported launches use standard GEMlib signatures and `SHELW`, with no native
CUIS setup. Public selector constants follow GEMlib numeric assignments;
declaring a constant does not advertise that its operation succeeds. `appl_getinfo(AES_SHELL)`
currently returns zero with all four outputs zero. Programs must not infer
support from the version number or a constant's presence. `AES_MESSAGE` reports
AP_TERM and CH_EXIT, without claiming live resolution switching.

References: [tos.hyp shel_write](https://freemint.github.io/tos.hyp/en/shel_write.html)
and [upstream GEMlib selectors/layouts](https://github.com/freemint/gemlib/blob/master/mt_gem.h).
Numeric coincidences between reference AES profiles do not establish identical
semantics: mode 0's `CL_PARSE=1` is distinct from mode 1's `SHW_CHAIN=1`.

## Mode and execution selectors

Mode / selector | Initial x32 behavior | Remaining scope
--- | --- | ---
0, `CL_NORMAL` or `CL_PARSE` | Infer kind from PRG/APP/GTP/ACC versus TOS/TTP, ignoring `wisgr`. Launch concurrently and send one CH_EXIT. Default to the executable directory. | GEMEXT/TOSEXT/ACCEXT overrides and forced identity: [#873](https://github.com/kelihlodversson/pTOS/issues/873).
1, `SHW_IMMED=0`, GEMAPP/TOSAPP | Explicit kind overrides suffix; observed concurrent launch with CH_EXIT. Console runs with an exclusive display/input lease; other applications remain resident. Default to executable directory. | No single-task peer freeze is implied.
1, `SHW_PARALLEL=100`, GEMAPP/TOSAPP | Inherit caller paths unless SW_DEFDIR is set. No AES child watch/CH_EXIT or automatic reap; ordinary Pwait/Pwaitpid applies. Console lease restores on normal/fault exit. | Maintain this behavior independently of deferred/single-task extensions.
1, `SHW_CHAIN=1`, GEMAPP | Prepare a detached successor; return 1 when prepared. It starts after process exit and child cleanup, not at appl_exit. Inherit the captured paths unless SW_DEFDIR is set. Failed replacement preserves the previous successor. | Explicit TOSAPP chain returns 0 before reading command/tail; [#873](https://github.com/kelihlodversson/pTOS/issues/873).
1, `SHW_DOS=2` | Return 0 before reading command/tail. There is no DOS backend. | Historical PC-GEM profile: [#876](https://github.com/kelihlodversson/pTOS/issues/876).
1, `SHW_SINGLE=101` | Return 0 before reading command/tail. Do not pretend that a parallel launch implements freeze/thaw. | MagiC single-mode profile: [#876](https://github.com/kelihlodversson/pTOS/issues/876), scheduling work #713.
3, `SWM_LAUNCHACC` | Return 0 before reading command/tail. Existing .ACC startup/launch is not equivalent to forcing accessory identity for an arbitrary filename. | [#873](https://github.com/kelihlodversson/pTOS/issues/873): set CRT `_app` and AES identity before constructors/preinitializers.
4, `SWM_SHUTDOWN` | SD_ABORT/SD_PARTIAL/SD_COMPLETE implement consent, refusal, initiator cancellation, ordered AC_CLOSE, AP_TERM and completion. Resident desktop powers off after authenticated completion. | MyAES reboot/options and N.AES diagnostic output are not implemented: [#876](https://github.com/kelihlodversson/pTOS/issues/876). The optional `ignorant` buffer is not populated.
5, `SWM_REZCHANGE` | Return 0 before reading command or starting shutdown. Desktop live-switch command stays disabled. | A real live x32 display backend is required: [#877](https://github.com/kelihlodversson/pTOS/issues/877).
7, `SWM_BROADCAST` | Return 0 before reading message buffer. | Standard broadcast and peer/queue semantics: [#874](https://github.com/kelihlodversson/pTOS/issues/874).
8, `SWM_ENVIRON` | Return 0 before reading/modifying any buffer, for SIZE/CHANGE/COPY. `shel_envrn` instead queries the captured process startup environment. | Shared shell environment, its lifetime and default-launch semantics: [#874](https://github.com/kelihlodversson/pTOS/issues/874).
9, `SWM_NEWMSG` | NM_APTERM consent only. Reject unknown bits, including NM_INHIBIT_HIDE, before reading command/tail. | Actual hide/inhibit-hide control: [#876](https://github.com/kelihlodversson/pTOS/issues/876).
10, `SWM_AESMSG` | Read one standard message; AP_TFAIL refuses the current lifecycle request through native provenance checks. Other message kinds return 0. | AP_AESTERM and Geneva dialog dispatch: [#876](https://github.com/kelihlodversson/pTOS/issues/876).
20/21/22, MagiC threads | Return 0 before reading THREADINFO, parameter, exit code or thread ID. | Native thread model and THR_EXIT, with #713: [#876](https://github.com/kelihlodversson/pTOS/issues/876).
224/225/227, Geneva launches | Return 0 before interpreting SHWRCMD. | Profile-specific structure and XSHD_FLAGS semantics: [#876](https://github.com/kelihlodversson/pTOS/issues/876).
Reserved 2/6 and other unknown low-byte modes | Return 0 before reading overloaded arguments. | No implicit alias to a supported mode.

Normal successful launches return a positive GEM/native process ID; GEM
failure is zero. Deferred GEM preparation returns 1 rather than a child ID.
Lifecycle returns 1 on acceptance and 0 on failure. Mode 8's standard size and
uncopied-byte return conventions are deferred together with that service;
its present zero return must not be taken as proof that copying succeeded.

## Extended command flags and command tails

Flag | Initial x32 behavior | Follow-up
--- | --- | ---
SW_DEFDIR (0x0400) | Read SHELW.newcmd and defdir. Non-NULL directory is resolved in the caller's context; NULL selects the executable directory. Set only the child's default drive/path before execution. | Implemented in #870.
SW_ENVIRON (0x0800) | Read SHELW.newcmd and env. Copy supplied environment privately for the launch; it does not change another process or global shell state. NULL retains existing default inheritance/ARGV behavior. | Implemented in #861.
SW_DEFDIR plus SW_ENVIRON | Both fields apply; each unselected SHELW field remains unread. Failure releases temporary launch storage and reservations. | Covered by native directory/environment tests.
SW_PSETLIMIT (0x0100), SW_PRENICE (0x0200), SW_UID (0x1000), SW_GID (0x2000) | Return 0 before reading SHELW or tail, including combinations with implemented flags. Never accept and ignore child policy. | Child-only policy and rollback: [#875](https://github.com/kelihlodversson/pTOS/issues/875). MagiC 6's conflicting bit-12 layout needs explicit profile handling in #876.
Reserved high bits, including 0x4000/XSHD_FLAGS (0x8000) | Return 0 before reading an extended structure. Extensions on lifecycle modes also fail. | Geneva bit 15 belongs to its separate profile, #876.

`SW_DEFDIR` and `SW_ENVIRON` are accepted only on implemented mode 0/1
launches. All supplied paths and environments remain subject to the documented
bounds in `doc/cuis.txt`; invalid caller memory can fault that process.
Rejecting an unsupported selector before dereference does not establish that
all pointers passed to supported calls are valid.

Tail form | Initial x32 behavior | Follow-up
--- | --- | ---
Length byte 0..126, CL_NORMAL | Copy exactly the supplied tail and zero unused bytes; do not modify caller storage. | Existing classic launch tests.
Length byte 0..126, mode 0 CL_PARSE | Parse quoting/empty arguments into a private ARGV environment. | #861.
Length byte 127, CL_NORMAL | Require explicit ARGV in the selected environment and copy it unchanged, including argv[0]. | #861.
Length byte 127, mode 0 CL_PARSE | Bounded long NUL-terminated command text is parsed into ARGV. | #861.
Length byte 254/255 | Return 0 before reading command/extended structure or text after the marker. | MagiC encoded-ARGV/long-tail forms: [#873](https://github.com/kelihlodversson/pTOS/issues/873).
Other length bytes 128..253 | Return 0 before reading command. | No valid classic length encoding is inferred.

## Acceptance boundary and next cutover work

The supported contract is the resident multitasking EmultiDesk, standard GEM
applications and RCS, native accessories started as .ACC images, the launch
forms above, and the implemented shutdown lifecycle. Ordinary application
launches never chain-load the desktop. The five linked issues retain the
missing compatibility behavior as explicit work after the initial cutover;
they do not turn unimplemented standard modes into supported ones. Existing
boot-time mode selection remains separate from live resolution switching.

The shell-scope audit can be closed when this classification and its rejection
regressions are merged. This does not close the non-shell API-gap audit or the
remaining cutover gates: external desktop/resource Kconfig decoupling, legacy
client migration and private exports, AES/CUIS exclusion and address-space
constraints, obsolete kernel-AES removal, and the final acceptance matrix.

Evidence: `tests/cuis/shell.c` covers all unsupported low-byte modes and high
flag bits with poisoned overloaded pointers, invalid execution selectors,
254/255 and other invalid tails, and absence of launch/search/allocation side
effects. `tests/cuis/gem_explicit.c`, `gem_argv.c`, `gem_chain.c` and
`gem_lifecycle.c` exercise the supported forms through ordinary GEM bindings;
`tests/x32_desktop/run.py` covers the resident desktop/accessory/console paths.
