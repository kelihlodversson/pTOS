# XRGB8888 Boot Palette Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Seed 32bpp XRGB8888 workstations with correctly ordered default colours.

**Architecture:** Keep `default_prgb_palette[]` in its existing `0x00BBGGRR` representation. Add a local conversion function that produces the little-endian XRGB8888 numeric value `0xFFRRGGBB`, and call it only from the 32bpp branch of `vdi_truecolor_init_palette()`.

**Tech Stack:** GNU90 C, pTOS VDI backend, ARM cross-build, QEMU virt-arm.

## Global Constraints

- Preserve the existing `0x00BBGGRR` PRGB table and all RGB565 behavior.
- Keep the conversion local to `vdi/vdi_backend_truecolor.c`.
- XRGB8888 numeric pixels must be `0xFFRRGGBB`, which writes B, G, R, X bytes on little-endian ARM.
- Use C90 declaration placement and four-space indentation.

---

### Task 1: Convert Default PRGB Entries for XRGB8888

**Files:**
- Modify: `vdi/vdi_backend_truecolor.c:106-167`
- Modify: `docs/superpowers/specs/2026-08-11-generic-xrgb8888-truecolor-design.md:148-154`

**Interfaces:**
- Consumes: `default_prgb_palette[]`, where red is bits 0-7, green is bits 8-15, and blue is bits 16-23.
- Produces: local `ULONG xrgb8888_from_prgb(ULONG prgb)` returning `0xFFRRGGBB`.

- [ ] **Step 1: Establish the failing static check**

Run:

```bash
git grep -n 'tc_palette\[i\] = default_prgb_palette\[i\] | 0xff000000UL' -- vdi/vdi_backend_truecolor.c
```

Expected: one match, demonstrating that the 32bpp initialization currently preserves the PRGB byte order instead of converting it.

- [ ] **Step 2: Implement the minimal conversion**

Add this local helper immediately after `rgb565_from_prgb()`:

```c
static ULONG xrgb8888_from_prgb(ULONG prgb)
{
    return 0xff000000UL | ((prgb & 0x000000ffUL) << 16)
           | (prgb & 0x0000ff00UL) | ((prgb & 0x00ff0000UL) >> 16);
}
```

Replace the 32bpp palette assignment with:

```c
vwk->tc_palette[i] = xrgb8888_from_prgb(default_prgb_palette[i]);
```

Update the adjacent comment and design specification so both state that PRGB is converted to `0xFFRRGGBB` for XRGB8888.

- [ ] **Step 3: Verify the static check now passes**

Run:

```bash
! git grep -n 'tc_palette\[i\] = default_prgb_palette\[i\] | 0xff000000UL' -- vdi/vdi_backend_truecolor.c
git grep -n 'xrgb8888_from_prgb(default_prgb_palette[i])' -- vdi/vdi_backend_truecolor.c
```

Expected: the old direct assignment has no matches; the helper call has one match.

- [ ] **Step 4: Build the affected target**

Run:

```bash
make virt-arm-tc32_defconfig && make
```

Expected: the build completes and reports `virt-arm.elf is ready`.

- [ ] **Step 5: Boot smoke-test the target**

Run (verified invocation from the ptos-smoketest skill):

```bash
timeout 5 qemu-system-arm -M virt,highmem=off -cpu cortex-a7 -m 128 -kernel virt-arm.elf \
  -d guest_errors,unimp -D /tmp/qemu.log -display none -serial stdio
cat /tmp/qemu.log
```

Expected: the process survives the full 5 s timeout (rc=124), serial output
shows `VDI video mode = ...`, `AES: EMUDESK: appl_init()`, and `AES: EMUDESK:
evnt_multi()`, and /tmp/qemu.log contains no `guest_errors`/`unimp` entries
beyond the single benign `Illegal Instruction` on m68k (not applicable on
ARM). The idle in `evnt_multi()` is the pass signal.

- [ ] **Step 6: Run repository formatting checks**

Run:

```bash
make gitready
git diff --check
```

Expected: both commands exit successfully.

- [ ] **Step 7: Commit**

```bash
git add vdi/vdi_backend_truecolor.c docs/superpowers/specs/2026-08-11-generic-xrgb8888-truecolor-design.md docs/superpowers/plans/2026-08-13-xrgb8888-boot-palette.md
git commit -m "Fix XRGB8888 boot palette"
```
