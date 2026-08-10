# RSC Loader Split Design

## Context

`aes/gemrslib.c` currently holds two complete, mutually exclusive RSC loader
families behind eight `#if CONF_WITH_LEGACY_RSC_LOAD` regions:

- the m68k in-place loader (read the file, fix resource offsets in place),
  used by the constrained 192 KB and 256 KB ROMs, and
- the portable canonical loader (bounds-checked big-endian disk parser plus a
  native materializer, with `rs_loadmem()`), used everywhere else.

Roughly 1140 of the file's 1775 lines are loader-exclusive; only about 630
lines are genuinely shared.  The two families keep drifting apart, and review
feedback on this feature has repeatedly surfaced dual-implementation bugs
(INDIRECT CICON handling, `CONF_WITH_COLOUR_ICONS` nesting, a legacy-only
allocation leak).  Each was a case of two code paths living in one file.

This change splits the loaders into their own source files, selected by
`build.mk`, with no behaviour change.  It follows the project's stated
preference for "anything machine specific" (here: loader-specific) to live
behind a configuration option rather than as `#if` blocks in a shared file.

## Architecture

Three source files replace the one:

- `aes/gemrslib.c` retains the shared loader infrastructure and the public
  `rsrc_load()` API.  It no longer contains either `rs_readit()`.
- `aes/rsload_legacy.c` contains the m68k in-place loader and its fixups.
- `aes/rsload_portable.c` contains the disk parser, native materializer, and
  `rs_loadmem()`.

Exactly one of the two loader files is linked, so the two may each define
`rs_readit()` and `fix_objects()` with the same signature.

### File responsibilities

**`aes/gemrslib.c`** (shared):
- Globals: `rs_hdr`, `rs_global`, `tmprsfname`, `free_str`, `rs_fstr`.
- Shared helpers: `rs_obfix()`/`fix_chpos()`, `get_sub()`, `get_addr()`,
  `best_match()`, `expand_cicondata()`, `transform_cicon()`, `pack_planes()`,
  `pack_cicon()`, `transform_all_cicons()`, `get_ciconblkptr()`,
  `free_cicon_buffers()`.
- Public API: `rs_sglobe()`, `rs_free()`, `rs_gaddr()`, `rs_saddr()`,
  `rs_fixit()`, `rs_load()`, `rs_str()`.
- Removed from this file: `rs_readit()`, `fix_objects()`, all legacy fixups,
  the disk parser and materializer, `rs_loadmem()`, `rs_own_global`.

**`aes/rsload_legacy.c`** (m68k in-place loader, no `#if
CONF_WITH_LEGACY_RSC_LOAD` inside):
- `rs_readit()` (legacy form), `fix_long()`, `fix_trindex()`, `fix_nptrs()`,
  `fix_ptr()`, `fix_tedinfo()`, `fix_objects()` (legacy form), `fix_cicons()`,
  `fixup_colour_icons()`, `fixup_all_ciconblks()`.
- Keeps only `#if CONF_WITH_COLOUR_ICONS` guards, which are feature guards
  shared with the rest of the CICON machinery, not loader-dual guards.

**`aes/rsload_portable.c`** (portable canonical loader):
- All `DISK_*` record constants and `struct disk_rsc`, `struct
  native_rsc_layout`, `struct disk_cicon_info`.
- Every `disk_*()` accessor and validator, `align_long()`, `layout_add()`,
  `userblk_offset()`, `scan_disk_ciconblk()`, `scan_disk_cicons()`,
  `layout_ordinary()`, `disk_string()`, `disk_string_capacity()`,
  `copy_disk_words()`, the `native_*_ptr()` helpers, `materialize_cicons()`,
  `decode_object_spec()`, `materialize_rsc()`.
- `rs_readit()` (portable form), `transform_cicons()`, `fix_objects()`
  (portable form), `rs_loadmem()`, `rs_own_global`.

## Loader Selection

`build.mk` selects exactly one loader.  `obj/auto.conf` turns every enabled
option into a make variable set to `y`, so the existing `obj-$(CONF_...)`
idiom needs no new machinery.

```make
# aes/build.mk
obj-$(CONF_WITH_LEGACY_RSC_LOAD) += rsload_legacy.o
obj-$(CONF_WITH_PORTABLE_RSC_LOAD) += rsload_portable.o
```

`CONF_WITH_PORTABLE_RSC_LOAD` is a new invisible derived symbol that is the
logical inverse of the legacy option:

```kconfig
config CONF_WITH_PORTABLE_RSC_LOAD
	bool
	default y
	depends on !CONF_WITH_LEGACY_RSC_LOAD
```

On ARM the legacy option cannot be selected (it depends on `ARCH_M68K`), so
the portable loader is selected there and everywhere the legacy option is off.
The two symbols can never both be `y`.

## Cross-File Interface

The only shared-to-loader calls are `rs_load() -> rs_readit()` and
`rs_fixit() -> fix_objects()`.  The loaders call back into four shared
helpers and the two globals.  A private header `aes/rsload.h` declares the
boundary:

```c
extern RSHDR *rs_hdr;
extern AESGLOBAL *rs_global;

WORD rs_readit(AESGLOBAL *pglobal, UWORD fd);
void fix_objects(void);

void *get_addr(UWORD rstype, UWORD rsindex);
void *get_sub(UWORD rsindex, UWORD offset, UWORD rsize);
CICONBLK **get_ciconblkptr(RSHDR *hdr);
void transform_all_cicons(LONG num_cicons, CICONBLK **ciconblkptr);
```

- `rsload.h` includes `config.h`, `portab.h`, `rsdefs.h`, and the public
  `gemrslib.h` for the types it declares, so every includer gets them.
- `get_addr()`, `get_sub()`, `get_ciconblkptr()`, and
  `transform_all_cicons()` lose `static` and are declared here; the
  `-Wmissing-prototypes` flag makes the declarations mandatory.
- `rs_obfix()` stays public in `gemrslib.h` as today.
- `rs_hdr` and `rs_global` stay defined (non-`static`) in `gemrslib.c` and are
  exported.  `rs_sglobe()` (shared) writes them; the legacy loader also writes
  them directly.
- `rs_loadmem()` keeps its `#if !CONF_WITH_LEGACY_RSC_LOAD` declaration in the
  public `gemrslib.h`; its definition moves to `rsload_portable.c`.  This stays
  consistent because the portable loader is exactly the `!LEGACY` build.

All three files include `rsload.h`.  Because `rsload.h` includes the public
`gemrslib.h`, the loader files also see the prototypes of `rs_obfix()` and the
public API they do not define.

## Behaviour And Verification

This is a pure refactor: no logic changes, no new options for users, no size
budget change.

- Verify by building the full configuration matrix and comparing image sizes
  against the recorded pre-split values:
  - `atari192` (legacy, no colour icons): `ptos192us.img` with 2068 bytes free.
  - `atari256` (legacy + colour icons): `ptos256us.img` with 7255 bytes free.
  - `atari512` (portable + colour icons): `ptos512k.img` with 16461 bytes free.
  - `atari512` with `CONF_WITH_VDI_CICON_TEST=y`: exercises `rs_loadmem()`
    from its new file.
  - `rpi1` (portable + truecolor backend): exercises `pack_planes()`/`pack_cicon()`.
- `make gitready` must pass.
- The French 256 KB variant must still fit (Release archives CI job).

## Constraints

- Exactly one loader links on every configuration; no duplicate-symbol
  conflicts.
- Source basenames remain unique across the tree (`gemrslib.c`,
  `rsload_legacy.c`, `rsload_portable.c`).
- C90 declarations-before-statements and 4-space indentation throughout.
- No change to the public `gemrslib.h` interface other than the existing
  `rs_loadmem()` guard, which is unchanged.
- The derived `CONF_WITH_PORTABLE_RSC_LOAD` symbol is invisible: it has no
  prompt, no help text, and no user-facing effect.
