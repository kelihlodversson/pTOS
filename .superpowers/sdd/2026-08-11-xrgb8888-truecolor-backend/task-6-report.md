# Task 6 Report: Packed Raster Strides

## Changes

- Added `packed_ppb` in `setup_info()`, derived locally from
  `(UWORD)(linea_vars.v_planes / 8)`.
- Replaced all seven specified packed-truecolor source/destination step and
  row-stride literals with `packed_ppb`.
- Updated the two affected comments to describe the packed pixel size.
- Left planar setup and stride arithmetic unchanged.

## Verification

- `make rpi2_defconfig && make`: passed. The normal pre-existing compiler
  warnings were emitted; `vdi/vdi_raster.c` compiled cleanly.
- `timeout 5 qemu-system-arm -M raspi2b -bios kernel7.img -d guest_errors -serial stdio`:
  reached USB initialization but required more than five seconds for the USB
  scan.
- `timeout 15 qemu-system-arm -M raspi2b -bios kernel7.img -d guest_errors -serial stdio`:
  reached `VDI video mode = 1280x720 16-bit` and remained alive until the
  expected timeout. No guest errors were reported; QEMU printed its existing
  `bcm2835_systmr_write: read-only ofs 0x4` diagnostic at startup.
- `kernel7.img`: 440537 bytes. RGB565 derives `packed_ppb == 2`, preserving
  the prior byte stride values; any image difference is expected codegen.
- `make atari512-dispatch_defconfig && make`: passed. The planar dispatch
  configuration compiled `vdi/vdi_raster.c` cleanly.
- `make gitready`: passed.

## Files

- Modified: `vdi/vdi_raster.c`
- Added: `.superpowers/sdd/2026-08-11-xrgb8888-truecolor-backend/task-6-report.md`

## Concerns

- No remaining concerns. The raspi2 desktop reached its 16-bit VDI mode after
  the known USB-scan delay. This task does not execute an XRGB8888 mode; it
  makes packed raster setup use that mode's descriptor depth when introduced.
