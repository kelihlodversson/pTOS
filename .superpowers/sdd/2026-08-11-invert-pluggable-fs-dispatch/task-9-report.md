# Task 9 Report: Dsetpath / xchdir

## Status: DONE

## Changes

- Moved the static `contains_wildcard_characters()` helper from
  `bdos/fsdir.c` to `fs/fatfs_pfs.c` under the OFF configuration gate.
- Added non-static `fat_chdir_path()` under `#if !CONF_WITH_PLUGGABLE_FS`.
  It preserves the legacy wildcard rejection, drive selection,
  `findit(p, &s, 1)` resolution, and `dirtbl[]` use-count replacement.
- Converted `xchdir()` to the pluggable-FS dispatch shim: ON calls
  `pfs_do_chdir()` and OFF calls `fat_chdir_path()`.
- `pfs_do_chdir()` and its pluggable-FS cwd bookkeeping were not changed.

## Temporary xsfirst Bridge

`xsfirst()` remains in `bdos/fsdir.c` until Task 11, but it previously used
the helper moved by this task. It now has an inline C90 wildcard scan with the
same behavior: return `result` on an error or when neither `'*'` nor `'?'` is
present; otherwise return `E_OK`. No helper reference remains in `fsdir.c`.
Task 11 will remove `xsfirst()` and add its OFF wrapper, allowing its wrapper
to use the static helper in `fatfs_pfs.c`.

## Verification

- `make distclean && make atari512_defconfig && make` completed successfully.
- Separate OFF `m68k-atari-mintelf-nm -g obj/fsdir.o obj/fatfs_pfs.o` check:
  `fsdir.o` references `_fat_chdir_path`; `fatfs_pfs.o` exports it; neither
  object references `_pfs_do_chdir`.
- `make distclean && make virt-arm_defconfig && make` completed successfully.
- Separate ON `arm-none-eabi-nm -g obj/fsdir.o obj/fatfs_pfs.o` check:
  `fsdir.o` references `_pfs_do_chdir`; `fatfs_pfs.o` exports `_fat_pfs_ops`
  and has no `_fat_chdir_path` symbol.
- `make gitready` passed.

The target builds retain unrelated existing compiler warnings in AES/VDI code.

## Fix Round 1

- Restored `xsfirst()`'s original short-circuit behavior by returning
  immediately after a negative `ixsfirst()` result, before the temporary
  inline wildcard scan can read `name`.
- `make distclean && make atari512_defconfig && make` completed successfully.
- `make distclean && make virt-arm_defconfig && make` completed successfully.
- `make gitready` passed.
