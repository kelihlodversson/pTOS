# Task 9 Report: virt-arm XRGB8888 Truecolor Test Hook

## Implementation

- Added `CONF_WITH_VDI_TRUECOLOR32_TEST`, a default-off test-only Kconfig
  option depending on `MACHINE_VIRT_ARM` and
  `CONF_WITH_VDI_BACKEND_TRUECOLOR32`.
- Added `bios/machine/virt-arm/virt_screen.c`, deliberately avoiding the
  globally-colliding `screen.c` basename. It allocates a 640x480, 2560-byte
  pitch guest-RAM framebuffer with `balloc_stram()`, stores it in `v_bas_ad`,
  reports its physical address with `virt_to_phys()`, and returns a packed,
  truecolor XRGB8888 32bpp descriptor.
- Wired address setup, descriptor reporting, and the test object entirely
  behind `CONF_WITH_VDI_TRUECOLOR32_TEST`.
- Added `CONF_WITH_VDI_TRUECOLOR32_TEST` to `EXTENDED_PALETTE`, preventing
  `MAP_COL[]`/`REV_MAP_COL[]` out-of-bounds writes when `v_planes` is 32.
  The extended requested-color entries remain zero-initialized on virt-arm;
  `vq_color(pen > 15, 0)` therefore reads harmless BSS zeros in this test-only
  configuration. `MAP_COL[1]` remains 255 for the truecolor backend.
- The early serial-console clear is planar-only. The test configuration skips
  it, because its packed framebuffer would otherwise overflow the legacy
  planar temporary array before VDI takes over.
- Generated the minimal Kconfig defconfig with `make savedefconfig` after
  enabling `CONF_WITH_VDI_BACKEND_TRUECOLOR`,
  `CONF_WITH_VDI_BACKEND_TRUECOLOR32`, and
  `CONF_WITH_VDI_TRUECOLOR32_TEST`; the checked-in defconfig preserves the
  parent configuration comments and settings and adds the Issue #91 note.

## Verification

### XRGB8888 test image

Command:

```
make virt-arm-tc32_defconfig && make
timeout 5 qemu-system-arm -M virt,highmem=off -cpu cortex-a7 -m 128 \
  -kernel virt-arm.elf -d guest_errors,unimp -D /tmp/qemu.log \
  -display none -serial stdio
```

- Build completed successfully and produced `virt-arm.elf`.
- QEMU returned `124`, confirming it survived the complete timeout window.
- Serial output included:
  - `virt-arm tc32: 640x480 XRGB8888 framebuffer at phys 0x47dd4000 (1228800 bytes)`
  - `VDI video mode = 640x480 32-bit`
  - `AES: EMUDESK: appl_init()`
  - `AES: EMUDESK: evnt_multi()`
- `/tmp/qemu.log` was empty: no guest errors or unimplemented-device entries.
- Unlike the m68k target, virt-arm has no `_detect_fpu` probe and therefore no
  corresponding benign illegal-instruction entry.

### Stock virt-arm regression

Command:

```
make clean
make virt-arm_defconfig && make
timeout 5 qemu-system-arm -M virt,highmem=off -cpu cortex-a7 -m 128 \
  -kernel virt-arm.elf -d guest_errors,unimp -D /tmp/qemu-stock.log \
  -display none -serial stdio
```

- Clean stock build completed successfully.
- `obj/virt_screen.o` was absent after the clean stock build.
- QEMU returned `124`, `/tmp/qemu-stock.log` was empty, and serial output
  retained the planar `VDI video mode = 320x200 4-bit` plus both AES desktop
  milestones.

### Formatting

- `make gitready` passed.
