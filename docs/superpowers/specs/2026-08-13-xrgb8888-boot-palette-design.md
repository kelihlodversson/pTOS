# XRGB8888 Boot Palette Design

## Problem

`default_prgb_palette[]` stores colours as `0x00BBGGRR`, matching the
existing Raspberry Pi palette representation.  A little-endian XRGB8888
framebuffer instead needs the numeric pixel value `0xFFRRGGBB`, so its bytes
in memory are B, G, R, X.  Directly OR-ing the PRGB value with `0xff000000UL`
swaps red and blue in the initial 32bpp palette.

## Design

Add a local conversion helper in `vdi/vdi_backend_truecolor.c` that extracts
the red, green, and blue bytes from a PRGB value and packs them as
`0xFFRRGGBB`.  `vdi_truecolor_init_palette()` uses this helper only when
`linea_vars.v_planes == 32`.

The RGB565 conversion stays unchanged.  The 32bpp `vs_color()` / `vq_color()`
conversion was corrected alongside the boot palette to the `0xFFRRGGBB`
convention: `vdi_truecolor_set_color()` packs the VDI-scale r/g/b components
as `0xFFRRGGBB` (the little-endian XRGB8888 pixel value, memory bytes
B,G,R,X) and `vdi_truecolor_get_color()` unpacks the same layout, matching
what `vdi_truecolor_init_palette()` now seeds.

## Verification

Build `virt-arm-tc32_defconfig`, boot it under QEMU, and run `make gitready`.
