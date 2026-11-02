# External libaes compatibility and cutover

This is the current **x32 `CONF_WITH_LIBAES`** source API inventory for #661.
Applications use normal GEMlib headers and `libaes.so`, without CUIS setup or
a userspace AES server. The older typed kernel AES service documented in
`aes-x32.txt` is a separate, transitional implementation. Its availability
does not establish that a feature works through external libaes.

`available` means the named classic operation is implemented, within the
documented resource/queue limits. `partial` identifies selectors or extensions
that are still missing. `unavailable` means there is no library entry point:
an application using it cannot currently link; this is **not** a runtime stub
returning GEM failure. Every available/partial GEM binding below also exports
its standard `mt_*` form. `helper` rows have no `mt_*` form.

The check in `tools/check-libaes-compatibility.py` compares this inventory with
the export map, public declarations and legacy AES opcode inventory. It also
checks that the evidence paths exist. This catches missing inventory entries,
not semantic correctness; the referenced tests and behavioral documentation
are the evidence for behavior. `doc/cuis.txt` gives detailed limits and modes.

## Source API and extension matrix

Entry points | Status | Semantics and outstanding extensions | Evidence
--- | --- | --- | ---
`appl_init`, `appl_exit`, `appl_yield` | available | Per-process globals and lifecycle; cooperative execution. Preemption/Pfork are #713. | `tests/cuis/application.c`, `tests/cuis/gem.c`
`appl_find`, `appl_read`, `appl_write` | available | GEM IDs/names, MiNT ID conversion, variable-length bounded streams, blocking reads and APR_NOWAIT. | `tests/cuis/application.c`, `tests/cuis/streams.c`
`appl_getinfo` | partial | AES_PROCESS ID conversion/rsrc_rcfix, AES_OBJECT 3D/objc_sysvar, AES_PCGEM shell defaults, AES_MESSAGE CH_EXIT and AES_INQUIRE APR_NOWAIT/shell buffers. Other queries return zero with zero outputs; AES_SHELL remains unadvertised. | `lib/aes/application.c`, `tests/cuis/gem_launch.c`
`appl_tplay`, `appl_trecord` | unavailable | Trusted input recording/replay needs an explicit ownership and injection policy. | `include/cuis.h`
`appl_search`, `appl_control` | unavailable | MultiAES/MagiC application enumeration and control require their standard discovery/visibility semantics. | `lib/aes/application.c`
`evnt_keybd`, `evnt_button`, `evnt_mouse`, `evnt_mesag`, `evnt_timer`, `evnt_dclick` | available | Classic input/message/time waits, click intervals and captured releases. | `tests/cuis/input_events.c`, `tests/cuis/gem_events.c`
`evnt_multi` | partial | Six classic GEM masks, simultaneous results and one timer deadline. Extended wheel/rectangle masks are not implemented. | `tests/cuis/input_events.c`, `tests/cuis/application.c`
`menu_bar`, `menu_icheck`, `menu_ienable`, `menu_tnormal`, `menu_text` | available | Owned standard menu bars, native painting, title tracking and MN_SELECTED. | `tests/cuis/menu_bar.c`, `tests/cuis/menu_items.c`
`menu_popup` | partial | Standalone classic popup tracking and cancellation. No scrolling or attached/nested submenus. | `tests/cuis/popup_track.c`, `tests/cuis/popup_menu.c`
`menu_register` | available | Six session-owned Desk entries, standard AC_OPEN menu IDs and REG_NEWNAME lookup labels. Accessories remain resident across ordinary application exits; global shutdown/reset AC_CLOSE policy is deferred with those operations. | `lib/aes/application.c`, `lib/aes/menubar.c`, `tests/cuis/application.c`, `tests/cuis/session.c`
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
`shel_write` | partial | AES4 mode 0 GEM or classic full-screen TOS/TTP launch with CH_EXIT; mode 1 GEM parallel (SHW_PARALLEL) and deferred SHW_CHAIN. CL_PARSE prepares ARGV, including long tails and empty arguments; SW_ENVIRON supplies a private launch environment. Accessories, mode 1 text execution, other extended command flags, single-task execution, shutdown and resolution changes remain missing. Resident EmuDesk launches children without chain loading. | `tests/cuis/gem_launch.c`, `tests/cuis/gem_argv.c`, `tests/cuis/gem_chain.c`, `tests/x32_desktop/run.py`
`scrp_read`, `scrp_write` | available | Shared standard scrap-directory state; this does not imply extended clipboard ownership/data-transfer protocols. | `tests/cuis/shell_metadata.c`
`scrp_clear` | unavailable | PC-GEM scrap clearing is not exported. | `lib/aes/desktop.c`
`inside`, `rc_constrain`, `rc_equal`, `rc_intersect`, `rc_union` | helper | Existing caller-local rectangle helpers, declared in rectfunc.h. | `tests/cuis/program.c`

The `aes_*` transport/session/window/message exports in `exports.map` remain
migration-test interfaces. They must become private as their tests move to
standard GEM entry points. `aes_global`, `gl_apid` and `gl_ap_version` are
standard binding state, not CUIS session handles.

## Boot/configuration audit

These are transitional configurations, not the desired final constraints.
Kconfig still permits kernel AES with CUIS and with separate address spaces.
The resident desktop currently relies on options nested below CONF_WITH_AES;
setting that symbol to n is not yet a complete external-desktop cutover.

Configuration | Current path | Validation
--- | --- | ---
x32, AES=n, LIBAES=n | Native CLI; no GUI library | `configs/pc-x86_64_defconfig`, x86 boot CI
x32, AES=n, ELF_SHARED=y, LIBAES=y | Ordinary shared GEM applications over CUIS; no desktop required | `tests/cuis/gem.c`, combined/headless shared-library boot CI
x32, AES=y, LIBAES=n, EXTERNAL_DESK=y | Transitional kernel AES and typed ring-3 desktop | `tests/x32_desktop/run.py`, feature-disabled desktop CI
x32, AES=y, LIBAES=y, EXTERNAL_DESK=y | BIOS launches resident libaes EmuDesk directly; kernel AES is still linked but not its shell | `tests/x32_desktop/run.py`, RCS x32 GUI CI
x32, AES=y, LIBAES=y, EXTERNAL_DESK=n | Built-in desktop retains legacy AES integration | Transitional link validation; not external-desktop acceptance evidence
ARM/m68k, USER_ASPACE=n | Existing kernel AES/trap/desktop paths | Native builds and ARM/m68k regression CI
ARM/m68k, USER_ASPACE=y | libaes unavailable; no completed external GUI path | Architecture follow-ups; not evidence for removing kernel AES yet

RCS integration is merged in kelihlodversson/rcs#40, with libcmini#24 fixing
x32 cookie lookup. Its GUI test uses normal GEMlib headers and the shared CRT,
loads its resources, compares a save with ARM golden output, edits and reloads
the result, and returns cleanly to the resident desktop. These tests establish
that application path. The desktop tests also cover a native accessory started
once with EmultiDesk, Desk-menu AC_OPEN, close/reopen, window survival across
GEM/console children and termination, plus classic console launch/return,
parameters and normal/fault cleanup.

## Remaining cutover gates, in implementation order

1. Complete remaining text execution modes and applicable extended command
   flags. Classic AES4 console launch/return is implemented in #859; CL_PARSE
   and per-launch SW_ENVIRON are implemented in #861. Preserve the resident desktop;
   use standard GEM shell modes and process lifetime/CH_EXIT semantics.
2. Accessory startup, independent process/window lifetime, menu_register,
   AC_OPEN and exit cleanup are implemented in #863. Complete AC_CLOSE reset
   delivery together with the actual shutdown/resolution policy in gate 4;
   ordinary multitasking application exits must not close accessories.
3. Resolve the remaining API/extension rows above: implement the applicable
   subset and record explicit follow-ups for deliberately deferred extensions.
   Keep appl_getinfo honest. Replay/injection needs a policy before enabling it.
4. Define shutdown/resolution-change behavior and desktop menu availability.
5. Move external desktop/resources/options out of kernel-AES dependencies and
   migrate remaining native test clients away from the typed legacy service.
   Hide the transitional aes_* library exports behind the library boundary.
6. Test the resulting external GUI and CLI/fallback configurations, then make
   CUIS/kernel AES mutually exclusive and kernel AES unavailable with
   USER_ASPACE on every architecture. Architectures without external libaes
   must retain a usable non-GUI configuration. Verify rejected combinations.
7. Remove the now-unreachable legacy AES service and AESPB/object validation/
   snapshot machinery, preserving native CUIS ownership/buffer checks. Run
   the full desktop, standard GEM/RCS, malformed-input and release matrix.

No API inventory check proves these gates complete. The final configuration
and validation removal must wait for the corresponding behavioral tests.
