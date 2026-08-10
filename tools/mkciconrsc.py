#!/usr/bin/env python3
#
# mkciconrsc.py - generate the truecolor colour-icon test resource
#
# Copyright (C) 2026 The pTOS development team.
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.
#
"""Generate desk/cicontest_rsc.c, a canonical big-endian RSC as C bytes.

Writes a standard Atari disk image to stdout.  The resource is a single
tree with a root G_IBOX and one 4-plane 32x32 colour icon whose planes split
the icon into four quadrants, one direct palette index per quadrant
(codes 1/2/4/8).

The layout is the canonical disk walk for a NEW_FORMAT_RSC resource:

    RSHDR (36 bytes, rsh_rssize = offset of the extension array)
    OBJECTs (2 * 24 bytes)
    trindex (one LONG, offset of the tree root)
    CICONBLK ptr table ([ciconblk_offset, -1L])
    CICONBLK block (contiguous, 968 bytes, see below)
    extension array ([total_len, ptr_table_offset, 0L])

The CICONBLK block is laid out for the canonical disk parser, which computes
every next-section position arithmetically:

    CICONBLK(38) + mono pdata(64 w) + mono pmask(64 w) + text(12)
    + CICON(22) + col_data(64*4 w) + col_mask(64 w)

Run as:  python3 tools/mkciconrsc.py > desk/cicontest_rsc.c
"""

import sys

ICON_W = 32
ICON_H = 32
NUM_PLANES = 4
MONO_WORDS = (ICON_W // 16) * ICON_H        # words in one 32x32 bitmap
TEXT_SIZE = 12                              # bytes of icon text

# disk record sizes (bytes) fixed by rsdefs.h / obdefs.h and the standard
# Atari RSC format -- change these only together with the portable decoder.
RSHDR_SIZE = 18 * 2                         # 10 UWORDs + 7 WORDs + 1 UWORD
ICONBLK_SIZE = 3 * 4 + 11 * 2               # monoblk: 3 ptrs + 11 WORDs
CICONBLK_SIZE = ICONBLK_SIZE + 4            # + mainlist LONG
CICON_SIZE = 2 + 5 * 4                      # WORD + 5 disk LONGs
OBJECT_SIZE = 6 * 2 + 4 + 4 * 2             # 6 WORDs + ob_spec + 4 WORDs

# section sizes, in bytes
OBJECTS_SIZE = 2 * OBJECT_SIZE
TRINDEX_SIZE = 4
PTRTABLE_SIZE = 2 * 4
BLOCK_SIZE = (CICONBLK_SIZE
              + MONO_WORDS * 2              # mono pdata
               + MONO_WORDS * 2              # mono pmask
               + TEXT_SIZE
               + CICON_SIZE
               + MONO_WORDS * 2 * NUM_PLANES # col_data, plane-major
              + MONO_WORDS * 2)             # col_mask
EXTARRAY_SIZE = 3 * 4

# The canonical disk walk relies on these sizes.  Keep assertions so changes
# to the record layout fail loudly instead of emitting a corrupt file.
assert CICONBLK_SIZE == 38, "CICONBLK must be 38 bytes"
assert CICON_SIZE == 22, "CICON must be 22 bytes"
assert OBJECT_SIZE == 24, "OBJECT must be 24 bytes"
assert MONO_WORDS == 64, "mono_words must be 64 for a 32x32 icon"
assert BLOCK_SIZE == 968, "CICONBLK block must be 968 bytes"

# section offsets, from the start of the RSC image
OBJECT_OFFSET = RSHDR_SIZE
TRINDEX_OFFSET = OBJECT_OFFSET + OBJECTS_SIZE
PTRTABLE_OFFSET = TRINDEX_OFFSET + TRINDEX_SIZE
BLOCK_OFFSET = PTRTABLE_OFFSET + PTRTABLE_SIZE
RSSIZE = BLOCK_OFFSET + BLOCK_SIZE          # == offset of extarray
TOTAL = RSSIZE + EXTARRAY_SIZE              # == extarray[0]

assert TOTAL == 1076, "total RSC must be 1076 bytes"
assert RSSIZE == 1064, "rsh_rssize must be 1064 bytes"

NEW_FORMAT_RSC = 0x0004
G_IBOX = 25
G_CICON = 33
ICON_TEXT = b"CICON test"


class Blob(object):
    """Accumulates 16/32-bit fields in the requested byte order."""

    def __init__(self, byteorder):
        self.buf = bytearray()
        self.order = byteorder

    def word(self, value):
        self.buf += (value & 0xffff).to_bytes(2, self.order)

    def long(self, value):
        self.buf += (value & 0xffffffff).to_bytes(4, self.order)

    def raw(self, data):
        self.buf += data


def plane_owns(plane, in_top, left):
    """True if plane p is set for a pixel in the given half/quarter.

    Plane 0 = top-left quadrant, 1 = top-right, 2 = bottom-left,
    3 = bottom-right; a quadrant's colour code is the OR of the plane
    bits there (pack_planes in aes/gemrslib.c:396 maps plane p to bit
    1<<p of the colour code).
    """
    return (plane in (0, 1)) == in_top and (plane in (0, 2)) == left


def plane_words(plane):
    """The 64 words of one 32x32 colour plane (2 words per row, MSB-first)."""
    words = []
    for y in range(ICON_H):
        in_top = y < ICON_H // 2
        word0 = 0xffff if plane_owns(plane, in_top, True) else 0x0000
        word1 = 0xffff if plane_owns(plane, in_top, False) else 0x0000
        words.append(word0)
        words.append(word1)
    return words


def build_rsc():
    """Assemble the canonical big-endian RSC image, asserting as we go."""
    blob = Blob("big")

    # RSHDR (36 bytes); every offset below is asserted against the
    # running byte position
    blob.word(NEW_FORMAT_RSC)               # rsh_vrsn
    assert len(blob.buf) == 2
    blob.word(OBJECT_OFFSET)                # rsh_object
    assert len(blob.buf) == 4
    for _ in range(7):                      # rsh_tedinfo..rsh_frimg
        blob.word(0)
    assert len(blob.buf) == 18
    blob.word(TRINDEX_OFFSET)               # rsh_trindex
    assert len(blob.buf) == 20
    blob.word(2)                            # rsh_nobs
    blob.word(1)                            # rsh_ntree
    for _ in range(5):                      # rsh_nted..rsh_nimages
        blob.word(0)
    assert len(blob.buf) == 34
    blob.word(RSSIZE)                       # rsh_rssize
    assert len(blob.buf) == RSHDR_SIZE

    # OBJECT[0]: ROOT, an invisible box not drawn
    blob.word(0)                            # ob_next (no sibling)
    blob.word(1)                            # ob_head -> OBJECT[1]
    blob.word(1)                            # ob_tail -> OBJECT[1]
    blob.word(G_IBOX)                       # ob_type
    blob.word(0)                            # ob_flags
    blob.word(0)                            # ob_state
    blob.long(0)                            # ob_spec
    blob.word(0)                            # ob_x
    blob.word(0)                            # ob_y
    blob.word(10)                           # ob_w (char units)
    blob.word(6)                            # ob_h (char units)

    # OBJECT[1]: the colour icon
    blob.word(0)                            # ob_next
    blob.word(-1)                           # ob_head
    blob.word(-1)                           # ob_tail
    blob.word(G_CICON)                      # ob_type
    blob.word(0)                            # ob_flags
    blob.word(0)                            # ob_state
    blob.long(0)                            # ob_spec.index: CICONBLK ptr table index
    blob.word(2)                            # ob_x (char units)
    blob.word(1)                            # ob_y
    blob.word(4)                            # ob_w
    blob.word(4)                            # ob_h
    assert len(blob.buf) == OBJECT_OFFSET + OBJECTS_SIZE

    # trindex: one LONG, offset of the tree root (OBJECT[0])
    blob.long(OBJECT_OFFSET)
    assert len(blob.buf) == TRINDEX_OFFSET + TRINDEX_SIZE

    # CICONBLK ptr table: [block offset, -1L]
    blob.long(BLOCK_OFFSET)
    blob.long(0xffffffff)
    assert len(blob.buf) == PTRTABLE_OFFSET + PTRTABLE_SIZE

    block_start = len(blob.buf)
    assert block_start == BLOCK_OFFSET

    # CICONBLK struct (38 bytes): monoblk (34) + mainlist LONG
    blob.long(0)                            # ib_pmask (fixed up)
    blob.long(0)                            # ib_pdata (fixed up)
    blob.long(0)                            # ib_ptext (fixed up)
    blob.word(0x0000)                       # ib_char (fg=0, bg=0, ch=0)
    blob.word(0)                            # ib_xchar
    blob.word(0)                            # ib_ychar
    blob.word(0)                            # ib_xicon
    blob.word(0)                            # ib_yicon
    blob.word(ICON_W)                       # ib_wicon
    blob.word(ICON_H)                       # ib_hicon
    blob.word(0)                            # ib_xtext
    blob.word(ICON_H)                       # ib_ytext
    blob.word(ICON_W)                       # ib_wtext
    blob.word(8)                            # ib_htext
    blob.long(1)                            # mainlist: one colour icon
    assert len(blob.buf) - block_start == CICONBLK_SIZE

    # mono pdata: a filled 32x32 square
    for _ in range(MONO_WORDS):
        blob.word(0xffff)
    assert len(blob.buf) - block_start == CICONBLK_SIZE + MONO_WORDS * 2

    # mono pmask: filled
    for _ in range(MONO_WORDS):
        blob.word(0xffff)
    assert len(blob.buf) - block_start == CICONBLK_SIZE + MONO_WORDS * 4

    # icon text (12 bytes, NUL padded)
    assert len(ICON_TEXT) <= TEXT_SIZE
    blob.raw(ICON_TEXT + b"\0" * (TEXT_SIZE - len(ICON_TEXT)))
    assert len(blob.buf) - block_start == CICONBLK_SIZE + MONO_WORDS * 4 + TEXT_SIZE

    # Disk CICON (22 bytes): num_planes + 5 LONG markers.  The pointers are
    # disk offsets to be decoded by the portable loader, so they are zero.
    blob.word(NUM_PLANES)                   # num_planes
    blob.long(0)                            # col_data marker
    blob.long(0)                            # col_mask marker
    blob.long(0)                            # sel_data marker
    blob.long(0)                            # sel_mask marker
    blob.long(0)                            # next_res marker
    assert len(blob.buf) - block_start == (CICONBLK_SIZE + MONO_WORDS * 4
                                            + TEXT_SIZE + CICON_SIZE)

    # col_data: plane-major, one full 32x32 bitmap per plane
    for plane in range(NUM_PLANES):
        for word in plane_words(plane):
            blob.word(word)
    assert len(blob.buf) - block_start == (CICONBLK_SIZE + MONO_WORDS * 4
                                            + TEXT_SIZE + CICON_SIZE
                                            + MONO_WORDS * 2 * NUM_PLANES)

    # col_mask: filled
    for _ in range(MONO_WORDS):
        blob.word(0xffff)
    assert len(blob.buf) == BLOCK_OFFSET + BLOCK_SIZE
    assert len(blob.buf) == RSSIZE

    # extension array: [total length, ptr table offset, 0L]
    blob.long(TOTAL)
    blob.long(PTRTABLE_OFFSET)
    blob.long(0)
    assert len(blob.buf) == TOTAL

    return blob.buf


def parser_block_walk():
    """Replay the canonical CICONBLK disk walk."""
    pos = BLOCK_OFFSET                      # CICONBLK *p = cicondata
    pos += CICONBLK_SIZE                    # q = (WORD *)(p+1)
    pos += MONO_WORDS * 2                   # mono pdata; q += mono_words
    pos += MONO_WORDS * 2                   # mono pmask; q += mono_words
    pos += TEXT_SIZE                        # icon text; q += 12/2 words
    pos += CICON_SIZE                       # disk CICON
    pos += MONO_WORDS * 2 * NUM_PLANES      # col_data; q += mono_words*planes
    pos += MONO_WORDS * 2                   # col_mask; q += mono_words
    return pos                              # == end of CICONBLK block


def validate(buf):
    """Read the emitted image back and check every field the parser uses."""
    u16 = lambda off: int.from_bytes(buf[off:off + 2], "big")
    u32 = lambda off: int.from_bytes(buf[off:off + 4], "big")

    assert u16(0) == NEW_FORMAT_RSC
    assert u16(2) == OBJECT_OFFSET
    assert u16(4) == 0                      # rsh_tedinfo..rsh_frimg all zero
    assert u16(6) == 0
    assert u16(8) == 0
    assert u16(10) == 0
    assert u16(12) == 0
    assert u16(14) == 0
    assert u16(16) == 0
    assert u16(18) == TRINDEX_OFFSET
    assert u16(20) == 2                     # rsh_nobs
    assert u16(22) == 1                     # rsh_ntree
    assert u16(24) == 0 and u16(26) == 0 and u16(28) == 0
    assert u16(30) == 0 and u16(32) == 0
    assert u16(34) == RSSIZE                # rsh_rssize: offset of extarray

    # trindex: one LONG, the offset of the tree root
    assert u32(TRINDEX_OFFSET) == OBJECT_OFFSET

    # CICONBLK ptr table: [block offset, -1L]
    assert u32(PTRTABLE_OFFSET) == BLOCK_OFFSET
    assert u32(PTRTABLE_OFFSET + 4) == 0xffffffff

    # the fields fixup_all_ciconblks() reads from the CICONBLK struct
    assert u16(BLOCK_OFFSET + 22) == ICON_W     # ib_wicon
    assert u16(BLOCK_OFFSET + 24) == ICON_H     # ib_hicon
    assert u32(BLOCK_OFFSET + 34) == 1          # mainlist

    # fix_objects() reads ob_type and ob_spec.index from each OBJECT
    assert u16(OBJECT_OFFSET + 6) == G_IBOX              # OBJECT[0].ob_type
    assert u16(OBJECT_OFFSET + OBJECT_SIZE + 6) == G_CICON  # OBJECT[1].ob_type
    assert u32(OBJECT_OFFSET + OBJECT_SIZE + 12) == 0     # OBJECT[1].ob_spec.index

    # The portable decoder reads disk CICON num_planes and marker fields.
    # CICON struct offset = BLOCK_OFFSET + 38 + 64*2 + 64*2 + 12 (same as
    # parser_block_walk()); its 22-byte disk layout is:
    #   0 num_planes, 2 col_data, 6 col_mask, 10 sel_data,
    #   14 sel_mask, 18 next_res
    cicon_off = BLOCK_OFFSET + CICONBLK_SIZE + MONO_WORDS * 2 * 2 + TEXT_SIZE
    assert cicon_off == BLOCK_OFFSET + 38 + 64 * 2 + 64 * 2 + 12
    assert cicon_off == BLOCK_OFFSET + 306
    assert u16(cicon_off) == NUM_PLANES               # num_planes
    assert u32(cicon_off + 10) == 0                   # sel_data (no variant)
    assert u32(cicon_off + 18) == 0                   # next_res (no more CICONs)

    # One plane owns each quadrant: TL=1, TR=2, BL=4, BR=8.
    plane_data_off = cicon_off + CICON_SIZE
    assert u16(plane_data_off) == 0xffff              # plane 0, top-left
    assert u16(plane_data_off + 2) == 0               # plane 0, top-right
    assert u16(plane_data_off + 64) == 0              # plane 0, bottom-left
    assert u16(plane_data_off + 128) == 0             # plane 1, top-left
    assert u16(plane_data_off + 130) == 0xffff        # plane 1, top-right
    assert u16(plane_data_off + 320) == 0xffff        # plane 2, bottom-left
    assert u16(plane_data_off + 450) == 0xffff        # plane 3, bottom-right

    # the CICONBLK block is exactly as long as the parser's walk of it
    assert parser_block_walk() == BLOCK_OFFSET + BLOCK_SIZE
    assert parser_block_walk() == RSSIZE

    # extension array
    assert u32(RSSIZE) == TOTAL             # extarray[0]: true length
    assert u32(RSSIZE + 4) == PTRTABLE_OFFSET
    assert u32(RSSIZE + 8) == 0             # terminator


def byte_lines(buf):
    """Emit `buf` as C initialiser rows (no array wrapper)."""
    lines = []
    for i in range(0, len(buf), 12):
        chunk = ", ".join("0x%02x" % b for b in buf[i:i + 12])
        lines.append("    %s," % chunk)
    return lines


def emit_c(buf):
    lines = [
        "/*",
        " * cicontest_rsc.c - truecolor colour-icon test resource",
        " *",
        " * Automatically generated by tools/mkciconrsc.py - do not edit.",
        " * Regenerate with:  python3 tools/mkciconrsc.py > desk/cicontest_rsc.c",
        " *",
        " * A 'new format' RSC (rsh_vrsn = NEW_FORMAT_RSC) describing a single",
        " * tree with one 4-plane colour icon.  The colour codes are direct",
        " * palette indices (slots 1/2/4/8), one per quadrant of the 32x32 icon:",
        " *",
        " *   slot 1  slot 2",
        " *   slot 4  slot 8",
        " *",
        " * The background colour (code 0) is a Task 6 calibration item: it is",
        " * rendered from the palette and should match the desktop background.",
        " *",
        " * This is one canonical big-endian Atari disk image.  Its CICONBLK block",
        " * is exactly contiguous for the portable decoder's arithmetic disk walk.",
        " */",
        "",
        "#include \"portab.h\"",
        "",
        "const LONG cicontest_rsc_size = %dL;" % TOTAL,
        "",
        "const UBYTE cicontest_rsc[] = {",
    ]
    lines += byte_lines(buf)
    lines += [
        "};",
        "",
    ]
    return "\n".join(lines)


def main():
    buf = build_rsc()
    validate(buf)
    sys.stdout.write(emit_c(buf))


if __name__ == "__main__":
    main()
