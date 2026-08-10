# Suppressed Comment Fix Report

## Source

Two suppressed comments from the Copilot PR #144 review submitted
2026-08-10T20:58Z (21 of 21 files reviewed).  Not posted as review threads;
validated against the code before fixing.

## Finding 1: legacy `rs_readit()` leaks `rs_hdr` on error paths

**Location:** `aes/gemrslib.c` legacy branch, after the
`dos_alloc_anyram(rslsize)` allocation.

**Validation: LEGIT.**  `rs_hdr` is allocated at line 1621 but
`rs_global->ap_rscmem = rs_hdr` is only performed after both the `dos_lseek()`
and the full `dos_read()` succeed.  The two error paths returned `FALSE`
without freeing the untracked allocation, leaking it (the caller,
`rs_load()`, only closes the file and propagates `FALSE`).  The portable
branch already released its buffer through a `fail:` label; the legacy branch
had no such release.  `rs_hdr` is a file-static pointer used only inside
`gemrslib.c`.

**Fix:** route both error paths through a `fail:` label that
`dos_free(rs_hdr)` and clears the dangling global (`rs_hdr = NULL`).

## Finding 2: `pack_planes()` 16-bit stride overflow

**Location:** `aes/gemrslib.c` line 776,
`WORD mono_words = (w + 15) / 16;`.

**Validation: technically correct, latent.**  On m68k `-mshort`, `int` is 16
bits, so `w + 15` overflows for valid `WORD` widths above `0x7FF0`, which is
undefined behaviour.  In practice `pack_planes()` only compiles under
`CONF_WITH_VDI_BACKEND_TRUECOLOR`, which defaults to `y` only for `MACHINE_RPI`
(32-bit `int`), so no shipped configuration triggers it today.  The reviewer's
second occurrence claim ("line 1624") is a stale line reference; every other
stride computation in the file already casts to `LONG` (lines 299, 638, 1267,
1442).

**Fix:** use `LONG mono_words = ((LONG)w + 15) / 16;` so the rounding division
cannot overflow regardless of target word size.  The two downstream
multiplications already promoted through `LONG` casts.

## Changes

- `aes/gemrslib.c`: `fail:` cleanup in legacy `rs_readit()`; `LONG` stride in
  `pack_planes()`.

## Verification

Full config matrix built after the change:

```
make atari192_defconfig && make   # legacy, no colour icons; ptos192us.img ready
make atari256_defconfig && make   # legacy + colour icons; ptos256us.img ready
make atari512_defconfig && make   # portable + colour icons; ptos512k.img ready
make rpi1_defconfig && make       # portable + truecolor backend (compiles pack_planes); kernel.img ready
```

All produced their images.  Remaining compiler warnings are pre-existing and
unrelated (`vdi/vdi_text.c`, `aes/gemasync.c`, USB).

```
make gitready
```

Passed.

## Commit

`846593c2 aes: fix legacy loader leak and 16-bit stride overflow`

## Concerns

No new concerns.  Finding 2 was fixed defensively; it cannot be reached on
any current configuration but the `LONG` arithmetic removes the latent
overflow at no cost.
