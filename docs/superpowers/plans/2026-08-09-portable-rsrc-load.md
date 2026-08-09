# Portable rsrc_load() Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Load one canonical big-endian standard Atari RSC image correctly on ARM and m68k, without making its bytes depend on target compiler structure padding, pointer alignment, or native byte order.

**Architecture:** Decode immutable disk bytes into one owned native pseudo-RSC image. The pseudo-image begins with a native `RSHDR`; its offsets point at aligned native record arrays and pointer tables, while a copied disk-data tail supplies strings and decoded bitmap data. Existing consumers retain `AESGLOBAL.ap_rscmem`, `ap_ptree`, `rs_gaddr()`, `rs_saddr()`, `rs_fixit()`, and `deskapp.c`'s `hdr + hdr->rsh_iconblk` contract. CICONs are parsed from their fixed 38/22-byte disk records into native CICONBLK/CICON structures before the existing `transform_all_cicons()` truecolor/planar conversion.

**Tech Stack:** Freestanding C90 (`-std=gnu90`), `portab.h` types, `include/endian.h` big-endian conversion helpers, GNU make cross-builds, QEMU raspi2b, Hatari STE, Python 3 generator assertions.

## Global Constraints

- The accepted external format is canonical standard Atari **big-endian** RSC: no target-specific byte arrays, no `__BYTE_ORDER__` selection, no native C struct overlay of disk bytes.
- Fixed disk record sizes: RSHDR 36, OBJECT 24, TEDINFO 28, ICONBLK 34, BITBLK 14, CICONBLK 38, CICON 22 bytes.
- Decode every disk scalar/offset with explicit BE readers; never cast raw bytes to `RSHDR *`, `LONG *`, `OBJECT *`, `CICONBLK *`, or `CICON *`.
- The decoded native allocation starts at `ap_rscmem`; native header offsets must address contiguous native arrays so `deskapp.c:setup_mono_icons()` keeps working unchanged.
- `ap_rsclen` and every native header offset are UWORD. Reject a decoded native image larger than 65535 bytes rather than truncating it.
- Preserve the public AES resource API and its lifetime model: `rs_loadmem()` copies caller bytes; `rs_free()` frees one materialized allocation plus separately allocated CICON conversion buffers.
- C90: declarations at the start of a block, `/* */` comments, 4 spaces, no hard tabs. `int` is 16-bit on m68k: use WORD/UWORD/LONG/ULONG/BOOL and cast arithmetic deliberately.
- Never edit generated configuration files (`obj/autoconf.h`, `obj/auto.conf`) or shipped defconfigs. `CONF_WITH_VDI_CICON_TEST` remains default n.
- `make gitready` before every commit. Stage only intended source/docs files.

---

### Task 1: Define and validate the canonical disk-RSC reader

**Files:**
- Modify: `aes/gemrslib.c` — include `endian.h`; add disk record offsets/sizes, bounded BE readers, and a validated decoded-header descriptor before resource materialization
- Modify: `tools/mkciconrsc.py` — encode one canonical BE RSC and assert every fixed wire offset/size
- Modify (generated): `desk/cicontest_rsc.c` — one canonical `const UBYTE cicontest_rsc[]`, no `__BYTE_ORDER__` branch

**Interfaces:**
- Consumes: `be2cpu16()` / `be2cpu32()` from `include/endian.h`; disk bytes from `rs_readit()`/`rs_loadmem()`; `NEW_FORMAT_RSC` from `rsdefs.h`.
- Produces: private `struct disk_rsc` containing `const UBYTE *base`, `LONG size`, decoded native `RSHDR hdr`, and true disk size; private `disk_uword()`, `disk_word()`, `disk_ulong()`, `disk_range()` helpers returning BOOL.

- [ ] **Step 1: Add exact disk-schema constants in `aes/gemrslib.c`**

Add `#include "endian.h"` with the other shared headers. Place these after the existing RSC type constants; they describe bytes on disk only and are not C structure sizes:

```c
#define DISK_RSHDR_SIZE       36
#define DISK_OBJECT_SIZE      24
#define DISK_TEDINFO_SIZE     28
#define DISK_ICONBLK_SIZE     34
#define DISK_BITBLK_SIZE      14
#define DISK_CICONBLK_SIZE    38
#define DISK_CICON_SIZE       22

#define D_RSH_VRSN            0
#define D_RSH_OBJECT          2
#define D_RSH_TEDINFO         4
#define D_RSH_ICONBLK         6
#define D_RSH_BITBLK          8
#define D_RSH_FRSTR          10
#define D_RSH_STRING         12
#define D_RSH_IMDATA         14
#define D_RSH_FRIMG          16
#define D_RSH_TRINDEX        18
#define D_RSH_NOBS           20
#define D_RSH_NTREE          22
#define D_RSH_NTED           24
#define D_RSH_NIB            26
#define D_RSH_NBB            28
#define D_RSH_NSTRING        30
#define D_RSH_NIMAGES        32
#define D_RSH_RSSIZE         34
```

Add matching named field offsets for OBJECT, TEDINFO, ICONBLK, BITBLK, CICONBLK, and CICON; use the fixed table in `docs/superpowers/specs/2026-08-09-portable-rsrc-load-design.md` rather than `offsetof()`.

- [ ] **Step 2: Add bounded big-endian field readers**

Add private helpers that accept an offset and fail before touching out-of-range bytes:

```c
static BOOL disk_range(const struct disk_rsc *disk, LONG offset, LONG length)
{
    return (offset >= 0L) && (length >= 0L)
        && (offset <= disk->size) && (length <= disk->size-offset);
}

static UWORD disk_uword(const struct disk_rsc *disk, LONG offset)
{
    UWORD value;

    memcpy(&value, disk->base + offset, sizeof(value));
    return be2cpu16(value);
}

static ULONG disk_ulong(const struct disk_rsc *disk, LONG offset)
{
    ULONG value;

    memcpy(&value, disk->base + offset, sizeof(value));
    return be2cpu32(value);
}
```

Use `memcpy()` rather than dereferencing an unaligned `UWORD *`/`ULONG *`. Add `disk_word()` as `(WORD)disk_uword()`.

- [ ] **Step 3: Decode and validate the disk header**

Add `static BOOL disk_header(struct disk_rsc *disk, const UBYTE *base, LONG available)` that:

1. Rejects `available < DISK_RSHDR_SIZE`.
2. Reads all eighteen RSHDR fields through the helpers into `disk->hdr`.
3. Sets `disk->size` to `rsh_rssize` for an old-format resource.
4. For `NEW_FORMAT_RSC`, validates the 12-byte extension prefix at `rsh_rssize`, reads `extarray[0]` as the true size, and uses that as `disk->size`.
5. Rejects zero, negative, or `disk->size > available` values.
6. Validates every nonzero fixed-table offset/count span with `disk_range()`:
   `rsh_object + nobs*24`, `rsh_tedinfo + nted*28`, `rsh_iconblk + nib*34`,
   `rsh_bitblk + nbb*14`, `rsh_trindex + ntree*4`, `rsh_frstr + nstring*4`,
   and `rsh_frimg + nimages*4`.

Use LONG products such as `(LONG)hdr.rsh_nobs * DISK_OBJECT_SIZE`.

- [ ] **Step 4: Make the generator a canonical BE fixture**

In `tools/mkciconrsc.py` remove target layout/byte-order selection. Emit disk CICONBLK=38 and CICON=22 exactly:

```python
RSHDR_SIZE = 36
ICONBLK_SIZE = 34
CICONBLK_SIZE = 38
CICON_SIZE = 22
...
blob.word(NUM_PLANES)
blob.long(0)       # col_data marker, disk offset ignored by parser
blob.long(0)       # col_mask marker
blob.long(0)       # sel_data marker
blob.long(0)       # sel_mask marker
blob.long(0)       # next_res marker
```

Use only `Blob("big")`. Keep validation assertions for: `rsh_object=36`,
`rsh_trindex=84`, CICON pointer table offset 88, CICONBLK offset 96,
`rsh_rssize=1066`, `extarray[0]=1078`, CICON `num_planes` at block+306,
and the four plane quadrants. Emit exactly:

```c
const UBYTE cicontest_rsc[] = {
    /* canonical big-endian bytes */
};
```

Keep `#define CICONTEST_RSC_SIZE 1078`; remove the `__BYTE_ORDER__` preprocessor branch.

- [ ] **Step 5: Run fixture checks before wiring the decoder**

```sh
python3 tools/mkciconrsc.py > desk/cicontest_rsc.c
python3 tools/mkciconrsc.py > /tmp/cicontest_rsc.c
diff -u desk/cicontest_rsc.c /tmp/cicontest_rsc.c
make rpi2_defconfig && make
make atari512_defconfig && make
```

Expected: generator exits 0; generated file is byte-identical; both default builds succeed because the hook remains off.

- [ ] **Step 6: Commit the canonical fixture and reader scaffolding**

```sh
make gitready
git add aes/gemrslib.c tools/mkciconrsc.py desk/cicontest_rsc.c
git commit -m "aes: define canonical RSC disk reader (#150)"
```

---

### Task 2: Materialize ordinary RSC records into a native pseudo-image

**Files:**
- Modify: `aes/gemrslib.c` — replace raw-image `rs_parse()` fixup flow with native-layout allocation and materialization; keep public APIs unchanged
- Modify: `aes/gemrslib.h` — update `AESGLOBAL.ap_rscmem` comment to “native materialized resource image”
- Modify: `desk/deskapp.c` — only if the native pseudo-image contract cannot preserve `hdr + hdr->rsh_iconblk`; expected outcome is no code change

**Interfaces:**
- Consumes: Task 1 `disk_header()` and wire readers; `dos_alloc_anyram()`/`dos_free()`; existing native `RSHDR`, `OBJECT`, `TEDINFO`, `ICONBLK`, `BITBLK` definitions.
- Produces: private `materialize_rsc(AESGLOBAL *, const struct disk_rsc *)` returning BOOL; after success `ap_rscmem` is one allocated native image, `ap_ptree` is a native `OBJECT **`, and all non-CICON pointers are native pointers or the existing `(void *)-1L` sentinel.

- [ ] **Step 1: Define a native image layout descriptor**

Add a private `struct native_rsc_layout` with LONG offsets for header, object,
TEDINFO, ICONBLK, BITBLK, tree table, free-string table, free-image table,
extension table, CICON table/data (filled in Task 3), raw disk-data tail, and

Add:

```c
static LONG align_long(LONG value)
{
    return (value + 3L) & ~3L;
}
```

`layout_ordinary()` starts at `sizeof(RSHDR)`, aligns each native array to 4,
uses native `sizeof(OBJECT/TEDINFO/ICONBLK/BITBLK)`, reserves native pointer
tables with `sizeof(void *)`, and reserves `disk->size` raw-tail bytes. It
rejects a negative/overflowed result or a result above `0xffffL`.

- [ ] **Step 2: Allocate one image and create its native header**

`materialize_rsc()` allocates `layout.total`, clears it, and sets:

```c
hdr = (RSHDR *)image;
*hdr = disk->hdr;
hdr->rsh_object = layout.object;
hdr->rsh_tedinfo = layout.tedinfo;
hdr->rsh_iconblk = layout.iconblk;
hdr->rsh_bitblk = layout.bitblk;
hdr->rsh_trindex = layout.trindex;
hdr->rsh_frstr = layout.frstr;
hdr->rsh_frimg = layout.frimg;
hdr->rsh_string = layout.raw + disk->hdr.rsh_string;
hdr->rsh_imdata = layout.raw + disk->hdr.rsh_imdata;
```

Copy the disk bytes to `image + layout.raw` once. Validate every converted
header offset fits UWORD before assignment. Do not publish `pglobal` until all
materialization succeeds; free `image` on every failure path.

- [ ] **Step 3: Decode OBJECT, TEDINFO, ICONBLK, and BITBLK tables**

Before the table loops, implement these four private helpers: `native_disk_ptr()`
(valid disk offset to `image + layout.raw + offset`), `native_tedinfo_ptr()`,
`native_iconblk_ptr()`, and `native_bitblk_ptr()`. The three typed helpers
validate the offset is inside the matching fixed disk table, require an exact
record boundary, then return `image + layout.<section> + index*sizeof(native
record)`. Decode each disk record into the correspondingly indexed native
record.

For OBJECT:

```c
obj->ob_next = disk_word(disk, off + D_OBJ_NEXT);
obj->ob_head = disk_word(disk, off + D_OBJ_HEAD);
obj->ob_tail = disk_word(disk, off + D_OBJ_TAIL);
obj->ob_type = disk_uword(disk, off + D_OBJ_TYPE);
obj->ob_flags = disk_uword(disk, off + D_OBJ_FLAGS);
obj->ob_state = disk_uword(disk, off + D_OBJ_STATE);
obj->ob_x = disk_word(disk, off + D_OBJ_X);
obj->ob_y = disk_word(disk, off + D_OBJ_Y);
obj->ob_width = disk_word(disk, off + D_OBJ_WIDTH);
obj->ob_height = disk_word(disk, off + D_OBJ_HEIGHT);
```

Decode `ob_spec` as a BE ULONG. Resolve it by `ob_type & 0x00ff`, validating
that the disk offset is in the matching source table before computing the
native array index:

```c
switch (obj->ob_type & 0x00ff)
{
case G_TEXT:
case G_BOXTEXT:
case G_FTEXT:
case G_FBOXTEXT:
    obj->ob_spec.tedinfo = native_tedinfo_ptr(image, layout, disk, spec);
    break;
case G_IMAGE:
    obj->ob_spec.bitblk = native_bitblk_ptr(image, layout, disk, spec);
    break;
case G_ICON:
    obj->ob_spec.iconblk = native_iconblk_ptr(image, layout, disk, spec);
    break;
case G_STRING:
case G_BUTTON:
case G_TITLE:
    obj->ob_spec.free_string = native_disk_ptr(image, layout.raw, disk, spec);
    break;
case G_CICON:
    obj->ob_spec.index = (LONG)spec;     /* table index; Task 3 resolves it */
    break;
case G_BOX:
case G_IBOX:
case G_BOXCHAR:
    obj->ob_spec.index = (LONG)spec;     /* packed scalar, not an offset */
    break;
default:
    obj->ob_spec.index = (LONG)spec;     /* retain existing unsupported-type behaviour */
    break;
}
```

For `INDIRECT`, decode the pointed LONG into a native LONG slot in the raw
tail before assigning `ob_spec.indirect`; never leave a BE disk LONG behind.

Decode TEDINFO pointer fields to `raw + disk_offset`; preserve `-1L` as
`(BYTE *)-1L`; decode all eight scalar words. Decode ICONBLK and BITBLK pointer
fields likewise. For every pointed bitmap span, validate it and write each
disk BE word into the raw tail as a native WORD via `copy_disk_words()`.
For every non-sentinel text/string pointer, require a NUL byte before
`disk->size`; this keeps the later `strlen()` in `fix_tedinfo()` bounded.

- [ ] **Step 4: Materialize tree/free pointer tables and preserve API lookups**

Build native tables instead of mutating disk LONGs:

```c
trees = (OBJECT **)(image + layout.trindex);
for (i = 0; i < hdr->rsh_ntree; i++)
    trees[i] = (OBJECT *)(image + layout.object
        + disk_ulong(disk, disk->hdr.rsh_trindex + (LONG)i * 4));
```

Validate each tree offset is within the disk OBJECT table and is an exact
multiple of `DISK_OBJECT_SIZE`. Build `BYTE **` free-string and `void **`
free-image tables from their disk offset tables, pointing into the raw tail.

Set `pglobal->ap_ptree = trees`; native `get_addr()` continues to work because
the remapped header offsets point at native contiguous arrays and the table
slots are native pointers.

- [ ] **Step 5: Replace in-place ordinary fixups with native finalization**

Remove ordinary disk-overlay calls from `rs_parse()`:

- remove `fix_trindex()`, `fix_tedinfo()`, and all `fix_nptrs()` calls;
- remove `fix_long()`/`fix_ptr()` once no caller remains;
- change `fix_objects()` so it only applies `rs_obfix()` to each native object
  and resolves the already-materialized G_CICON table index in Task 3; it must
  not treat native pointer fields as offsets;
- update `rs_sglobe()`/`rs_parse()` to publish only a successfully materialized
  native image.

Retain `get_addr()` but simplify its pointer cases to return native fields and
native tables without calling `fix_long()`.

- [ ] **Step 6: Route file and memory wrappers through the decoder**

Change `rs_readit()` to read the disk header bytes into `UBYTE header[DISK_RSHDR_SIZE]`, decode it with `disk_header()`, allocate/read exactly `disk.size` disk bytes, call `materialize_rsc()`, then free the scratch disk allocation. On seek/read failure, free every temporary allocation before returning FALSE.

Change `rs_loadmem()` to call `disk_header()` directly on `rsmem`, then `materialize_rsc()`; it no longer copies input itself because materialization makes the owned copy. Preserve NULL `pglobal` handling and return `pglobal->ap_ptree[0]` only after `rs_fixit()` succeeds.

- [ ] **Step 7: Build ordinary-resource regressions**

```sh
make distclean
make atari512_defconfig && make
make rpi2_defconfig && make
timeout 30 qemu-system-arm -M raspi2b -bios kernel7.img \
  -d guest_errors -serial file:/tmp/rsrc-rpi2.log -display none
```

Expected: both builds succeed; default rpi2 reaches `AES: EMUDESK: evnt_multi()`; no new guest errors. This covers normal built-in native C resources and confirms no default configuration links the optional test hook.

- [ ] **Step 8: Commit the ordinary decoder**

```sh
make gitready
git add aes/gemrslib.c aes/gemrslib.h
git commit -m "aes: materialize canonical RSC records natively (#150)"
```

---

### Task 3: Decode CICON extensions and restore the test hook

**Files:**
- Modify: `aes/gemrslib.c` — scan fixed disk CICON records, allocate native CICONBLK/CICON structures in the pseudo-image, and invoke existing `transform_all_cicons()`
- Modify: `tools/mkciconrsc.py` — ensure the canonical fixture asserts 38/22-byte CICON disk records and canonical BE image words
- Modify (generated): `desk/cicontest_rsc.c`

**Interfaces:**
- Consumes: Task 2 native layout and raw-tail mapping; canonical new-format extension array (`extarray[0]` true disk length, `extarray[1]` CICON table offset, `-1L` CICON table sentinel).
- Produces: native extension array at `hdr + hdr->rsh_rssize`, native `CICONBLK **` table terminated by `(CICONBLK *)-1L`, native `CICONBLK.mainlist` chains, and correctly converted bitmap data for `transform_all_cicons()`.

- [ ] **Step 1: Add a disk CICON sizing pass**

Add `scan_disk_cicons()` called after `disk_header()`. It validates:

1. `rsh_rssize + 12` is in range for a new-format extension.
2. `extarray[0] == disk->size` and `extarray[1]` is 0, -1, or a valid table offset.
3. The CICON table is a bounded sequence of BE ULONG offsets ending in `0xffffffffUL`.
4. Each CICONBLK offset contains `DISK_CICONBLK_SIZE`; its `mainlist` count is bounded; its mono data/mask/text and each fixed 22-byte CICON header/payload fit in range.

For each CICON, detect selected payload from its disk `sel_data` marker, validate its data/mask spans, and accumulate `ciconblk_count` and `cicon_count` for the native layout. Reject a non-final disk `next_res` marker other than `1L`.

- [ ] **Step 2: Reserve native CICON sections**

Extend `struct native_rsc_layout` with offsets for:

```c
LONG extension;
LONG cicon_table;
LONG ciconblks;
LONG cicons;
```

Reserve `3 * sizeof(LONG)` for the native extension array, `(count + 1) * sizeof(CICONBLK *)` for the sentinel-terminated table, `count * sizeof(CICONBLK)` for blocks, and `cicon_count * sizeof(CICON)` for native variants. Keep all sections 4-byte aligned and below `0xffffL`.

- [ ] **Step 3: Materialize each native CICONBLK and CICON chain**

For each disk table entry, decode the embedded disk ICONBLK into its native
`monoblk`; set `ib_pdata`, `ib_pmask`, and `ib_ptext` to the converted raw-tail
spans that follow the disk header. Decode mono bitmap words with
`copy_disk_words()`.

For each disk CICON record, populate a native CICON:

```c
cicon->num_planes = disk_word(disk, disk_cicon + D_CICON_PLANES);
cicon->col_data = (WORD *)(image + layout.raw + colour_data_offset);
cicon->col_mask = (WORD *)(image + layout.raw + colour_mask_offset);
cicon->sel_data = has_selected ? (WORD *)(image + layout.raw + selected_data_offset) : NULL;
cicon->sel_mask = has_selected ? (WORD *)(image + layout.raw + selected_mask_offset) : NULL;
cicon->next_res = next_native_cicon_or_null;
```

Decode normal and selected colour data/mask words to native order. Set each
native CICONBLK `mainlist` to its first native CICON and each native table slot

- [ ] **Step 4: Publish native extension metadata and transform CICONs**

Set the native header's `rsh_rssize` to `layout.extension`. Fill:

```c
extarray[0] = layout.total;
extarray[1] = layout.cicon_table;
extarray[2] = 0L;
```

Replace old `fix_cicons()` raw-layout traversal with a native-only version:

```c
static void transform_cicons(void)
{
    CICONBLK **table = get_ciconblkptr(rs_hdr);
    LONG count;

    if (!table)
        return;
    for (count = 0; table[count] != (CICONBLK *)-1L; count++)
        ;
    transform_all_cicons(count, table);
}
```

Call it after successful CICON materialization. In `fix_objects()`, replace a
G_CICON object's retained disk table index with `get_ciconblkptr(rs_hdr)[index]`
`fixup_colour_icons()` on disk bytes; remove them when unused.

- [ ] **Step 5: Verify both test-enabled images before calibration**

Enable `CONFIG_CONF_WITH_VDI_CICON_TEST=y` non-interactively after each defconfig, then:

```sh
make rpi2_defconfig
make menuconfig
# Enable: EmuDesk desktop -> Embedded colour-icon rendering test
make
arm-none-eabi-nm kernel7.elf | grep -E "cicontest_rsc|rs_loadmem"

make atari512_defconfig
make menuconfig
# Enable: EmuDesk desktop -> Embedded colour-icon rendering test
make
m68k-atari-mintelf-nm obj/cicontest_rsc.o | grep cicontest_rsc
```

Expected: both link the one canonical `cicontest_rsc` symbol. Do not commit `.config`.

- [ ] **Step 6: Commit CICON materialization**

```sh
make gitready
git add aes/gemrslib.c tools/mkciconrsc.py desk/cicontest_rsc.c
git commit -m "aes: decode canonical CICON resource data (#150)"
```

---

### Task 4: End-to-end portable-RSC verification and colour calibration

**Files:**
- Modify only if calibration proves it necessary: `aes/gemrslib.c` (`pack_planes()` bit expression)
- Modify: `docs/superpowers/plans/2026-08-09-truecolor-cicon-rendering.md` — record final quadrant mapping/calibration result

**Interfaces:**
- Consumes: single canonical `desk/cicontest_rsc.c`, `rs_loadmem(NULL, cicontest_rsc)`, `CONF_WITH_VDI_CICON_TEST`, QEMU raspi2b and Hatari STE.
- Produces: emulator evidence that the exact same RSC bytes decode on little-endian ARM and big-endian m68k; final colour-plane calibration note.

- [ ] **Step 1: Run ARM/truecolor end-to-end**

Build rpi2 with the test option enabled. Wait for desktop startup before requesting a monitor screendump:

```sh
{ sleep 22; echo "screendump /tmp/cicon-arm.ppm"; sleep 1; echo "quit"; } | \
  timeout 45 qemu-system-arm -M raspi2b -bios kernel7.img \
    -d guest_errors -serial file:/tmp/cicon-arm.serial \
    -display none -monitor stdio 2>/tmp/cicon-arm.guesterr
```

Expected: `/tmp/cicon-arm.serial` contains `AES: EMUDESK: evnt_multi()` and no
`desk_cicon_test: in-memory RSC load failed`; guest-error output has no Data
Abort and no new messages beyond the known QEMU `bcm2835_systmr_write` artifact.

- [ ] **Step 2: Sample and calibrate the four ARM quadrants**

Use Pillow to inspect `/tmp/cicon-arm.ppm`. The G_CICON is at roughly
`(2*gl_wchar, gl_hchar)` and has four 16x16 pixel quadrants. Sample their
centres, record RGB values, and confirm four distinct colours for plane codes
TL=1, TR=2, BL=4, BR=8. If the observed order is the strict bit reverse
TL=8/TR=4/BL=2/BR=1, change exactly:

```c
code |= (WORD)(1 << p);
```

to:

```c
code |= (WORD)(1 << (planes - 1 - p));
```

in `pack_planes()`, rebuild, and repeat Step 1. Do not alter the generator for
a bit-order result: the canonical disk plane order is the test contract.

- [ ] **Step 3: Run m68k/planar end-to-end**

Build atari512 with the same test option enabled, then:

```sh
hatari --tos ptos512k.img --machine ste --memsize 4 --sound off \
  --avirecord --avi-vcodec png --avi-file /tmp/cicon-planar.avi \
  --run-vbls 3000
```

Extract a late PNG stream frame with PIL (per `ptos-smoketest`) and confirm the
desktop plus four unswapped CICON quadrants. This validates the same canonical
BE bytes on m68k and the planar `gr_colourblit()` path.

- [ ] **Step 4: Run the default configuration matrix and no-depth-check scan**

```sh
make distclean
for c in atari512 rpi1 rpi2 virt-arm virt-m68k; do
  make ${c}_defconfig && make || echo "FAIL: $c"
done
grep -rn "v_planes" aes/gemrslib.c aes/gemgraf.c
grep -rn "planes > 8" aes/
```

Expected: all five builds succeed with the hook off; no `planes > 8` match;
the first grep contains no branch-added depth dispatch.

- [ ] **Step 5: Update evidence, run gitready, and commit calibration only if changed**

Record the ARM and STE quadrant observations plus the final bit expression in
`docs/superpowers/plans/2026-08-09-truecolor-cicon-rendering.md`. If Step 2
changed `pack_planes()`, commit the source and evidence together:

```sh
make gitready
git add aes/gemrslib.c docs/superpowers/plans/2026-08-09-truecolor-cicon-rendering.md
git commit -m "aes: calibrate CICON plane order on truecolor (#107)"
```

If no source calibration change was needed, commit only the evidence note with:

```sh
make gitready
git add docs/superpowers/plans/2026-08-09-truecolor-cicon-rendering.md
git commit -m "docs: record portable CICON rendering verification (#107)"
```

---

## Self-Review

**Spec coverage:** Task 1 establishes canonical BE disk reading and a single fixture; Task 2 materializes ordinary records into one native pseudo-image while preserving `ap_rscmem`/header-offset consumers; Task 3 handles fixed disk CICON records and native CICON conversion; Task 4 proves the same bytes load on ARM and m68k, calibrates colour order, and runs the regression matrix.

**Compatibility:** Task 2 explicitly preserves `deskapp.c`'s direct native-header/ICONBLK contract. The public AES resource API, CICON conversion-buffer lifetime, NULL `rs_loadmem()` context, and default-off hook remain unchanged.

**Placeholder scan:** all disk constants, record sizes, section layouts, function names, commands, expected results, and failure rules are stated. No target-specific disk layout remains.

**Type consistency:** disk bytes use UBYTE and BE UWORD/ULONG helpers; all potentially wide size arithmetic uses LONG; native public structures retain their existing declarations; native pointer tables use `sizeof(void *)` only inside the native pseudo-image, never to interpret disk bytes.
