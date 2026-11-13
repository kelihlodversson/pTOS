# External libaes compatibility and cutover

This is the current **x32 `CONF_WITH_LIBAES`** source API inventory for #661.
Native applications use GEMlib's pTOS `gem.h` backend and `libaes.so`, without
CUIS setup or a userspace AES server. The native `mt_*` interface takes
`AESGLOBAL *`. Initialize submodules before building; pTOS builds the static
`obj/gemlib/libgem.a` bindings automatically. External clients link that archive
before `-laes`. `include/gem.h` forwards to GEMlib; `include/libaes.h` declares
only the implementation API. The traditional trap backend remains separate. The earlier typed x32 kernel AES service and its
client ABI have been retired; classic kernel AES remains for single-address-
space systems.

`available` means the named classic operation is implemented, within the
documented resource/queue limits. `partial` identifies selectors or extensions
that are still missing. `unavailable` means there is no library entry point:
an application using it cannot currently link; this is **not** a runtime stub
returning GEM failure. Every available/partial operation below has an `mt_*` export in libaes
and a classic binding in GEMlib. libaes exports only `mt_*` functions. `helper` rows have no `mt_*` form.

The check in `tools/check-libaes-compatibility.py` compares this inventory with
the export map, public declarations and legacy AES opcode inventory. It also
checks that the evidence paths exist. This catches missing inventory entries,
not semantic correctness; the referenced tests and behavioral documentation
are the evidence for behavior. `doc/cuis.txt` gives detailed limits and modes.
The [shell audit](libaes-shell-compatibility.md) classifies every shell mode,
execution selector, extended launch flag and tail encoding, with explicit
follow-ups #873–#877 for deferred behavior. Remaining non-shell extensions
are scoped in #884 (events, replay and application control), #885 (menus,
objects and windowed forms), and #886 (window and clipboard extensions).
Extended resource formats remain tracked in #836 and #849.

## Source API and extension matrix

Entry points | Status | Semantics and outstanding extensions | Evidence
--- | --- | --- | ---
`appl_init`, `appl_exit`, `appl_yield` | available | Per-process globals and lifecycle; cooperative execution. Preemption/Pfork are #713. | `tests/cuis/application.c`, `tests/cuis/gem.c`
`appl_find`, `appl_read`, `appl_write` | available | GEM IDs/names, MiNT ID conversion, variable-length bounded streams, blocking reads and APR_NOWAIT. | `tests/cuis/application.c`, `tests/cuis/streams.c`
`appl_getinfo` | partial | AES_PROCESS ID conversion/appl_search/rsrc_rcfix, AES_OBJECT 3D/objc_sysvar, AES_PCGEM shell defaults, AES_MESSAGE AP_TERM/CH_EXIT and AES_INQUIRE APR_NOWAIT/shell buffers. Other queries return zero with zero outputs; AES_SHELL remains unadvertised. | `lib/aes/application.c`, `tests/cuis/gem_launch.c`
`appl_tplay`, `appl_trecord` | unavailable | Trusted input recording/replay needs an explicit ownership and injection policy. | `include/cuis.h`
`appl_search` | available | Standard APP_FIRST/NEXT/DESK enumeration with eight-character names, GEM IDs and application/accessory/current-desktop types; negative-mode extended descriptions are not advertised. | `lib/aes/application.c`, `tests/cuis/application.c`
`appl_control` | partial | APC_TOP raises a client preserving per-client window order; APC_INFO reports its menu/background. Visibility hiding, foreign menu pointers and other extended control modes return GEM failure. | `lib/aes/application.c`, `tests/cuis/application.c`
`evnt_keybd`, `evnt_button`, `evnt_mouse`, `evnt_mesag`, `evnt_timer`, `evnt_dclick` | available | Classic input/message/time waits, click intervals and captured releases. | `tests/cuis/input_events.c`, `tests/cuis/gem_events.c`
`evnt_multi` | partial | Six classic GEM masks, simultaneous results and one timer deadline. Extended wheel/rectangle masks are not implemented. | `tests/cuis/input_events.c`, `tests/cuis/application.c`
`menu_bar`, `menu_icheck`, `menu_ienable`, `menu_tnormal`, `menu_text` | available | Owned standard menu bars, native painting, title tracking and MN_SELECTED. | `tests/cuis/menu_bar.c`, `tests/cuis/menu_items.c`
`menu_popup` | partial | Standalone classic popup tracking and cancellation. No scrolling or attached/nested submenus. | `tests/cuis/popup_track.c`, `tests/cuis/popup_menu.c`
`menu_register` | available | Six session-owned Desk entries, standard AC_OPEN menu IDs and REG_NEWNAME lookup labels. Accessories remain resident across ordinary application exits. Shutdown sends one AC_CLOSE per Desk entry after resetting accessory windows; complete shutdown also sends AP_TERM. | `lib/aes/application.c`, `lib/aes/menubar.c`, `tests/cuis/application.c`, `tests/cuis/session.c`
`menu_attach`, `menu_istart`, `menu_settings` | unavailable | AES 3.30 attached-submenu/start-item/settings extensions. Extended menu capabilities are not advertised. | `lib/aes/popup.c`
`objc_add`, `objc_delete`, `objc_order`, `objc_find`, `objc_offset` | available | Caller-local tree editing/traversal assumes valid indices and acyclic parent/sibling links; malformed links can loop or fault. Offset accumulation and find origins use WORD coordinates and can wrap outside their range. These calls do not inherit the drawing preflight's topology or LONG geometry checks. | `tests/cuis/objects.c`
`objc_draw`, `objc_change` | partial | Shared native painter, classic text/boxes/images/icons, USERDEF, colour icons and 3D flags. Extended object types/GDOS object fonts need separate coverage. | `tests/cuis/object_draw.c`, `tests/cuis/colour_objects.c`, `tests/cuis/three_d.c`
`objc_edit` | available | Standard text/template/validation editing and caret control. | `tests/cuis/gem_forms.c`
`objc_sysvar` | partial | Native MultiTOS 3D colour/offset selectors; extended selectors are rejected. | `tests/cuis/three_d.c`
`form_do`, `form_button`, `form_keybd`, `form_dial`, `form_alert`, `form_error`, `form_center`, `form_center_grect` | available | Classic modal forms, radio/exit/watch semantics, native alerts, exact screen restoration and centering; no windowed-form extensions. | `tests/cuis/gem_forms.c`, `tests/cuis/gem_buttons.c`
`graf_handle`, `graf_mkstate`, `graf_mouse` | available | Workstation metrics, input snapshots, native cursor forms and counted visibility with focus/grab ownership. | `tests/cuis/graphics.c`, `tests/cuis/pointer_style.c`
`graf_watchbox`, `graf_rubberbox`, `graf_dragbox`, `graf_slidebox` | available | Classic watch/drag/rubber/slide interactions and capture semantics. | `tests/cuis/gem_tracking.c`, `tests/cuis/gem_buttons.c`
`graf_mbox`, `graf_growbox`, `graf_shrinkbox`, `graf_growbox_grect`, `graf_shrinkbox_grect` | available | Classic native box animations, including GRECT convenience bindings. | `tests/cuis/box_animations.c`
`wind_create`, `wind_open`, `wind_close`, `wind_delete`, `wind_calc`, `wind_find`, `wind_update`, `wind_new` | partial | Classic decorated windows, sliders, resizing, redraw regions and nested update/mouse locks. Unsupported creation flags are rejected; HOTCLOSE calculation alone does not imply interactive HOTCLOSE support. | `tests/cuis/application.c`, `tests/cuis/window_api.c`, `tests/cuis/gem_windows.c`
`wind_get`, `wind_set`, `wind_get_int`, `wind_get_ptr`, `wind_get_grect`, `wind_set_int`, `wind_set_ptr`, `wind_set_ptr_int`, `wind_set_str`, `wind_set_grect` | partial | Standard implemented fields, window-zero desktop/background, global stack queries and pointer-safe convenience forms. Unsupported field selectors return GEM failure. Not a claim of all MagiC/XaAES window fields. | `tests/cuis/window_api.c`, `tests/cuis/window_desktop_api.c`
`wind_create_grect`, `wind_open_grect`, `wind_calc_grect` | available | GRECT convenience forms use the corresponding bounded classic operations. | `tests/cuis/window_api.c`
`rsrc_load`, `rsrc_free`, `rsrc_gaddr`, `rsrc_saddr`, `rsrc_obfix`, `rsrc_rcfix` | partial | Classic disk/native resources, colour icons, caller-owned rcfix and GEM globals. RSX and additional extended resource formats remain separate work. | `tests/cuis/resources.c`, `tests/cuis/gem_resources.c`
`fsel_input`, `fsel_exinput` | available | Shared native file selector, caller DTA restoration, wildcard/path handling and extended title. No fslx selector API. | `tests/cuis/gem_selector.c`
`shel_read`, `shel_find`, `shel_envrn`, `shel_get`, `shel_put`, `shel_rdef`, `shel_wdef` | available | Standard startup/tail, search/environment queries, persistent desktop buffers and default desktop paths. | `tests/cuis/shell.c`, `tests/cuis/shell_buffer.c`, `tests/cuis/shell_metadata.c`
`shel_write` | partial | AES4 mode 0 GEM or classic full-screen TOS/TTP launch with CH_EXIT; mode 1 explicit GEM/TOS immediate launch (SHW_IMMED), GEM/TOS parallel launch (SHW_PARALLEL), and deferred GEM SHW_CHAIN. Immediate launches send CH_EXIT; parallel launches leave child reaping to the caller and send no CH_EXIT. CL_PARSE prepares ARGV, including long tails and empty arguments; SW_ENVIRON supplies a private launch environment; SW_DEFDIR selects the child directory before execution. Immediate launches default to the executable directory; parallel/deferred launches otherwise inherit caller paths. Existing .ACC startup/launch (not forced SWM_LAUNCHACC), NM_APTERM consent, partial/complete shutdown, AP_TFAIL refusal and owner cancellation are supported. Live resolution changes return failure before disruption. Explicit accessory mode, classification environment overrides, text-mode chain handoff, broadcast/shared environment, other extended command flags and vendor-specific execution modes are deferred in #873–#877. See the detailed shell audit. Resident EmuDesk launches children without chain loading. | `tests/cuis/gem_launch.c`, `tests/cuis/gem_argv.c`, `tests/cuis/gem_chain.c`, `tests/cuis/gem_explicit.c`, `tests/cuis/gem_lifecycle.c`, `tests/x32_desktop/run.py`
`scrp_read`, `scrp_write` | available | Shared standard scrap-directory state; this does not imply extended clipboard ownership/data-transfer protocols. | `tests/cuis/shell_metadata.c`
`scrp_clear` | unavailable | PC-GEM scrap clearing is not exported. | `lib/aes/desktop.c`
`inside`, `rc_constrain`, `rc_equal`, `rc_intersect`, `rc_union` | helper | Existing caller-local rectangle helpers, declared in rectfunc.h. | `tests/cuis/program.c`

Native `aes_*` transport/session/window/message helpers are library-private
and declared only in `lib/aes/native_bindings.h`. Applications use the standard
GEM exports and `gem.h`/GEMlib; `aes_global`, `gl_apid` and `gl_ap_version` are
caller-owned GEMlib binding state, not libaes exports or CUIS session handles. Raw ownership/fault/scheduling
probes use `tests/cuis/native_probe.h` with direct syscalls, while ordinary GEM
clients import only standard bindings. The API inventory rejects private exports.

## Typed global state and binding ownership (#953)

`include/aesglobal.h` is the canonical public definition, independent of kernel
headers. Native `aes_global` is an `AESGLOBAL` object; pass `&aes_global` or the
address of another `AESGLOBAL` to `mt_*`. Use named fields instead of word
indices, pointer-word copies, or casts between global representations.
`appl_init()` and successful `appl_exit()` update GEMlib's `gl_apid` and
`gl_ap_version`. Direct `mt_*` calls operate only on the supplied public global;
they do not discover or synchronize GEMlib's default state or another array.
Binding data and classic wrappers are linked into the caller, including callers
that supply some bindings themselves, such as EmuDesk.
`ap_private` belongs to the caller, including across `mt_appl_init` and
`mt_appl_exit`. Resource selectors `ap_ptree`, `ap_rscmem` and `ap_rsclen` are
authoritative for the supplied global. Resource ownership records retain image
metadata, without a shadow `AESGLOBAL`. EmuDesk passes its own typed global to
libaes directly.

m68k keeps the classic big-endian, 15-WORD/30-byte layout and two-byte alignment.
ARM and x32 use their native byte order, pointer representation and alignment:
32 bytes for 32-bit pointers; the 64-bit kernel/host representation is 56 bytes.
The header asserts each member offset, field width, total size and alignment.
This changes the private native libaes ABI: rebuild clients with the library.
A 15-WORD buffer is only the m68k binding representation.

Classic wrappers, binding globals and public rectangle helpers belong to GEMlib.
Internal rectangle routines remain private to libaes.
`tools/check-libaes-elf.py` checks actual ELF exports/imports and the archive,
including caller-owned x32 global storage. `make libaes-elf-check` runs it.
Kernel AES retains its historical area-D use of `ap_3resv`.
No dependency on #903, new CUIS identity API, or 68k AESPB emulation is introduced.

## Explicit application context lifetime (#953)

For userspace libaes, `mt_appl_init` allocates an opaque `AES_APP_CONTEXT` when
the supplied global's `ap_3resv` is null. It publishes the pointer only after
session setup succeeds and rolls back normal allocation/registration failures.
A non-null slot reuses exactly that context without opening another session,
allocating another context, or finding a context by PID/TID. Reinitialization
preserves that global's resource selectors; no other global's public fields
are copied or synchronized. `ap_private` remains untouched.

To share explicitly, initialize one global, assign its live `ap_3resv` to a
second zero-initialized global, then call `mt_appl_init` on the second global.
`mt_appl_exit` closes the session, cleans up, frees the context once and clears
the supplied global's slot. All other aliases become invalid; the caller must
stop using them. A failed close retains the context for retry. If freeing the
context fails after close, retrying exit retries the free without closing or
cleaning up twice. No other operations are valid after that partial exit.

Except for initialization with a null slot, passing an uninitialized, invalid,
mismatched, closed or freed context is undefined behavior. There is no magic,
generation counter, registry, reference counting, TLS or stale-pointer check.
Kernel process exit remains authoritative cleanup for faults, `_Exit` and
omitted `appl_exit`; libaes has no cached-session destructor.

Application session, enumeration cursor and shutdown/cohort state now live in
the context. Windows, resources, menus, graphics, shell state and temporary
helper pointers still need migration in subsequent #953 slices. This is not
yet a reentrant implementation or independent application isolation. CUIS
still returns one session per process: two fresh globals allocate distinct
userspace contexts, but currently receive the same native session. Ending it
through either context invalidates the other context's kernel session. Tests
of independent enumeration/cohort state use distinct mocked session handles;
production independent sessions require the separate CUIS identity work
coordinated with #896/#907/#915.

## Boot/configuration audit

Kconfig excludes kernel AES whenever CUIS or per-process address spaces are
selected. The external x32 desktop uses shared libaes and native VDI. ARM and
m68k retain the traditional kernel AES on single-address-space configurations;
backends without external libaes must retain a usable non-GUI path.

Configuration | Current path | Validation
--- | --- | ---
x32, AES=n, LIBAES=n | Native CLI; no GUI library | `configs/pc-x86_64_defconfig`, x86 boot CI
x32, AES=n, LIBAES=y, EXTERNAL_DESK=n | Shared GEM applications over CUIS | `tests/cuis/gem.c`, combined boot CI
x32, AES=n, LIBAES=y, EXTERNAL_DESK=y | Resident shared desktop, no kernel AES | `configs/pc-x86_64-libaes-desktop_defconfig`, `tests/x32_desktop/run.py`
ARM/m68k, USER_ASPACE=n | Traditional kernel AES/trap/desktop | Native builds and ARM/m68k regression CI
ARM/m68k, USER_ASPACE=y | Kernel AES unavailable; external libaes is not yet ported | Architecture follow-ups and CLI configuration checks

RCS integration is merged in kelihlodversson/rcs#40, with libcmini#24 fixing
x32 cookie lookup. Its GUI test uses normal GEMlib headers and the shared CRT,
loads its resources, compares a save with ARM golden output, edits and reloads
the result, and returns cleanly to the resident desktop. These tests establish
that application path. The desktop tests also cover a native accessory started
once with EmultiDesk, Desk-menu AC_OPEN, close/reopen, window survival across
GEM/console children and termination, plus classic console launch/return,
parameters and normal/fault cleanup.

## Remaining cutover gates, in implementation order

1. Merge the shell-scope audit #871. The initial baseline includes classic
   console launch/return (#859), CL_PARSE/SW_ENVIRON (#861), explicit
   immediate/parallel modes (#867) and launch directories/SW_DEFDIR (#869).
   The [selector audit](libaes-shell-compatibility.md) records the missing
   launch forms (#873), shared services (#874), child policy (#875), vendor
   profiles (#876) and live display backend (#877) as explicit follow-ups.
   This scopes shell work for the initial cutover without advertising those
   missing operations. Ordinary desktop launches remain concurrent.
2. Accessory startup, independent process/window lifetime, menu_register,
   AC_OPEN and exit cleanup are implemented in #863. AC_CLOSE resets accompany
   the shutdown protocol; ordinary multitasking application exits do not close
   accessories.
3. Resolve the remaining API/extension rows above: implement the applicable
   subset and record explicit follow-ups for deliberately deferred extensions.
   Keep appl_getinfo honest. Replay/injection needs a policy before enabling it.
4. Shutdown preparation, consent/refusal, cancellation and desktop poweroff
   are implemented. Live resolution switching requires a platform backend and
   remains disabled without terminating clients.
5. External desktop/resources/options are independent of kernel AES (#878).
   Transitional libaes helpers are private (#880). The typed x32 kernel AES
   service, its clients and its snapshot validation are retired in #882.
6. Kconfig enforces CUIS/kernel-AES exclusion and disallows kernel AES with
   USER_ASPACE. Preserve usable non-GUI configurations for backends without
   external libaes.
7. Complete the full desktop, standard GEM/RCS, malformed-input, fault/cleanup,
   CLI/fallback, architecture and release acceptance matrix, and integrate
   current master before the epic is ready for review.

No API inventory check proves these gates complete. The final configuration
and validation removal must wait for the corresponding behavioral tests.
