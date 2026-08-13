# Task 12 Report: Frename / xrename

## Status: DONE

## Changes

- Moved `unpackit`, `is_subdir`, and `update_fcb` from `bdos/fsdir.c` into
  `fs/fatfs_pfs.c` as static FAT-core helpers; `packit`, `getdnd`, and
  `freednd` remain exported BDOS helpers.
- Replaced the old path-rebuilding `fat_rename` adapter with the legacy
  rename core, taking resolved directory cookies and final path components.
  It does not call `xrename()` or `ixcreat()`.
- The existence check scans `newdir` for `newname` with the same effective
  attributes as `ixsfirst(path, FA_SUBDIR, NULL)`, avoiding a reconstructed
  path while preserving the `EACCDN` result when a destination exists.
- Cross-directory creation calls `fat_create()` while both DNDs are locked,
  then obtains the native handle from `newfc.index`.
- Added non-static `fat_rename_path()` for the non-pluggable build. It retains
  the two legacy `findit()` calls and invokes the common core with cookies.
- Converted `xrename()` into the configuration dispatch shim: ON calls
  `pfs_do_rename()` and OFF calls `fat_rename_path()`.

## Verification

- `make distclean && make atari512_defconfig && make` completed successfully.
- Separate OFF `m68k-atari-mintelf-nm -g obj/fsdir.o obj/fatfs_pfs.o` check:
  `fsdir.o` imports `_fat_rename_path`; `fatfs_pfs.o` exports it; neither
  object references `_pfs_do_rename`.
- `make distclean && make virt-arm_defconfig && make` completed successfully.
- Separate ON `arm-none-eabi-nm -g obj/fsdir.o obj/fatfs_pfs.o obj/pfs.o`
  check: `fsdir.o` imports `_pfs_do_rename`; `pfs.o` exports it;
  `fatfs_pfs.o` exports `_fat_pfs_ops` and has no `_fat_rename_path` symbol.
- `make gitready` and `git diff --check` passed.

The target builds retain unrelated existing compiler warnings in AES/VDI code
and the existing non-empty DATA-segment warning for virt-arm.

## Review Fix Round 1

- Protected the source DND while the OFF wrapper resolves the destination
  path, restoring only a lock acquired by this call.
- Protected the source DND while the common core scans the destination for an
  existing entry, again preserving a pre-existing `DND_LOCKED` bit.
- `make distclean && make atari512_defconfig && make` completed successfully.
- `make distclean && make virt-arm_defconfig && make` completed successfully.
- `make gitready` and `git diff --check` passed.
