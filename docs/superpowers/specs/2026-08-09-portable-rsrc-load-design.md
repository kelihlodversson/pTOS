# Portable rsrc_load() Design

## Goal

Make one canonical standard-Atari RSC file load correctly on m68k and ARM.
The loader must no longer overlay disk bytes with native C structures or use
the target compiler's structure layout to traverse the file.

This fixes issue #150 and unblocks issue #107's embedded CICON test RSC.

## Problem

`aes/gemrslib.c` currently treats an RSC image as native `RSHDR`, `OBJECT`,
`ICONBLK`, `CICONBLK`, `CICON`, `TEDINFO`, and `BITBLK` structures.  It reads
fields directly, advances records with `sizeof()`, and replaces disk offsets
with native pointers in place.

Those structures have different layouts on the supported targets:

| Structure | ARM | m68k (`-mshort`) |
| --- | --- | --- |
| `ICONBLK` | 36 | 34 |
| `CICONBLK` / `mainlist` offset | 40 / 36 | 38 / 34 |
| `CICON` / `col_data` offset | 24 / 4 | 22 / 2 |

Consequently, the current raw CICON test image crashes on ARM and is shifted
by one bitmap word on m68k.  A resource file must not encode either layout.

## Canonical Disk Format

The loader accepts the standard Atari big-endian RSC wire format.  Disk
records use fixed field sizes and offsets, independent of the target ABI:

- `RSHDR`: 36 bytes, eighteen 16-bit fields.
- `OBJECT`: 24 bytes, six 16-bit fields, one 32-bit `ob_spec`, then four
  16-bit fields.
- `TEDINFO`: three 32-bit offsets, then eight 16-bit fields.
- `ICONBLK`: three 32-bit offsets and eleven 16-bit fields (34 bytes).
- `BITBLK`: one 32-bit offset and five 16-bit fields.
- `CICONBLK`: disk `ICONBLK` plus a 32-bit CICON count (38 bytes).
- `CICON`: one 16-bit plane count followed by five 32-bit fields (22 bytes).

All offsets, extension-array entries, image words, and bitmap words are read
as big-endian values.  `tools/draft.c` and `tools/erd.c` already implement
this wire format with explicit big-endian readers; the runtime loader follows
the same convention.

## Decode Boundary

`rs_readit()` and `rs_loadmem()` first obtain an immutable raw RSC byte image.
A new internal decoder reads that image with bounded `read_be_u16()` and
`read_be_u32()` helpers and materialises a native mutable resource image.

The decoder performs two passes:

1. Validate header offsets/counts and calculate native allocation sizes using
   the fixed disk record sizes, never `sizeof()` over raw disk bytes.
2. Allocate one owned native image containing `RSHDR`, record arrays, pointer
   tables, strings, bitmap/image data, and variable CICON data. Resolve disk
   offsets to native pointers while materialising. Copy big-endian bitmap/image
   words as native `WORD`s.

After materialisation, `rs_hdr` and `AESGLOBAL.ap_rscmem` refer only to native
memory. Existing native APIs (`rs_gaddr()`, `rs_saddr()`, `rs_fixit()`,
`OBJECT *`, `ICONBLK *`, and `CICONBLK *`) remain unchanged.  Native fixup
code may continue to use `sizeof()` because it no longer traverses disk bytes.

The raw image is scratch input and is released after decoding. `rs_free()`
releases the one materialised image and CICON conversion buffers as it does
now.

## CICON Handling

The decoder treats `CICONBLK.mainlist` as the disk CICON count, not a native
pointer. It walks the variable data with the fixed 38-byte CICONBLK and
22-byte CICON disk records, creates native CICONBLK/CICON objects, resolves
their data/mask pointers, and constructs native `next_res` links.

The existing truecolor conversion in `transform_all_cicons()` then receives
native CICONs. No target-specific RSC array or compiler-layout assumption is
needed.

## Embedded Test Resource

`tools/mkciconrsc.py` emits one canonical big-endian `cicontest_rsc[]` array.
`desk/cicontest_rsc.c` contains no `__BYTE_ORDER__` split. The same bytes are
passed to `rs_loadmem()` on ARM and m68k.

## Error Handling

Malformed or truncated resources fail decode without publishing a partially
materialised resource. Bounds checks cover every table offset, fixed record,
variable CICON block, extension-array entry, and bitmap/image data span.
`rs_readit()` and `rs_loadmem()` preserve their existing failure contracts.

## Verification

- Load the canonical generated CICON RSC on ARM and m68k.
- rpi2 QEMU reaches the desktop and screendump shows four CICON quadrants.
- Atari STE Hatari reaches the desktop and renders the planar CICON path.
- Build default `atari512`, `rpi1`, `rpi2`, `virt-arm`, and `virt-m68k`.
- Confirm no new AES depth checks and run `make gitready`.

## Non-goals

- Changing built-in `desk_rsc.c`; it is already generated C with native
  target layout, not a disk RSC image.
- Changing the VDI CICON rendering path or desktop test-hook interface.
- Supporting non-Atari/little-endian RSC file variants; the portable on-disk
  contract is canonical standard Atari big-endian RSC.
