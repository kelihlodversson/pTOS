# Task 11 Report: Fsfirst / Fsnext

## Status: DONE

## Changes

- Added non-static `LONG fat_sfirst_path(char *name, int att)` and
  `LONG fat_snext_path(void)` under `#if !CONF_WITH_PLUGGABLE_FS` in
  `fs/fatfs_pfs.c`, with matching declarations in `fs/fatfs.h`.
- `fat_sfirst_path()` retains the legacy DTA initialization, `ixsfirst()`
  call, and exact error-before-wildcard short-circuit.
- `fat_snext_path()` retains the legacy uninitialized-DTA and exhausted-search
  handling, then calls `ixsnext()` and `makbuf()`.
- Converted `xsfirst()` and `xsnext()` to compile-time dispatch shims: ON
  calls `pfs_do_sfirst()` / `pfs_do_snext()` and OFF calls the FAT wrappers.
- Removed Task 9's temporary inline wildcard scan with the old `xsfirst()`
  body. `ixsfirst()`, `ixsnext()`, and `makbuf()` are unchanged.

## Verification

- `make distclean && make atari512_defconfig && make` completed successfully.
- Separate OFF `nm` check: `fsdir.o` defines `_xsfirst`/`_xsnext` and
  `fatfs_pfs.o` exports `_fat_sfirst_path`/`_fat_snext_path`; no PFS search
  dispatch symbol is present.
- `make distclean && make virt-arm_defconfig && make` completed successfully.
- Separate ON `nm` check: `fsdir.o` references `_pfs_do_sfirst` and
  `_pfs_do_snext`, which `pfs.o` exports; `fatfs_pfs.o` has no FAT search
  wrapper symbols and exports `_fat_pfs_ops`.
- The target builds retain unrelated existing compiler warnings in AES/VDI
  code and the existing non-empty DATA-segment warning for virt-arm.
