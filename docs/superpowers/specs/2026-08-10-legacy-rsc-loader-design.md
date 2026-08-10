# Legacy RSC Loader for Constrained ROMs

## Context

The portable, canonical big-endian RSC materializer added for #150 makes the
`atari192` image exceed its fixed 192 KB ROM size by 4332 bytes.  The target
already disables colour icons, and its m68k native RSC structures have the
same big-endian layout as Atari disk RSC records.  It can therefore retain the
previous in-place loader without losing a supported feature.

The same loader is the approved fallback for the fixed 256 KB ROM when its
language variants exhaust that target's ROM budget.  Both constrained ROM
sizes can use the in-place loader without changing its implementation.

## Configuration

Add an AES Kconfig boolean, `CONF_WITH_LEGACY_RSC_LOAD`:

- It depends on `ARCH_M68K`, because the in-place loader relies on the m68k
  native structure layout and must never be selected on ARM.
- It defaults to `y` for `TARGET_192` and `TARGET_256`, and `n` for every
  other target.
- Its help text explains that it preserves the constrained 192 KB and 256 KB
  ROM budgets, while normal builds use the portable canonical loader.
- It remains selectable for other m68k configurations when their ROM budget
  requires it.

Keeping the policy in Kconfig makes each constrained-ROM fallback a default
change rather than another source-level target fork.

## Loader Selection

When `CONF_WITH_LEGACY_RSC_LOAD` is enabled, `aes/gemrslib.c` compiles the
former m68k `rs_readit()` implementation and its in-place RSC pointer fixups:

- The file is read directly into the resource allocation.
- Resource offsets are converted in place to native pointers.
- The legacy CICON parsing helpers are retained with that path, although they
  are not compiled in the 192 KB configuration because colour icons are
  already disabled.

When the option is disabled, the current bounds-checked canonical disk parser,
native materializer, and `rs_loadmem()` implementation remain unchanged.

`rs_loadmem()` is only declared and built with the portable loader.  Its only
consumer is the configuration-gated CICON test hook, which requires colour
icons and cannot be enabled in the 192 KB configuration.

## Constraints

- No ARM target may compile or select the legacy path.
- Larger and default targets continue to use canonical big-endian RSC parsing.
- No CICON capability is removed from a configuration that currently enables
  it.
- LTO is not part of this change.  A trial with GCC 13.3 fails because
  `aes/gen_asm_defines.c` needs compiler-emitted assembly, whereas `-flto`
  emits LTO IR at that build step.

## Verification

Build the selected boundary configurations:

1. `make atari192_defconfig && make` must create `ptos192us.img` within the
   192 KB budget.
2. `make release-256k` must create every 256 KB language image, including the
   French image, within the 256 KB budget.
3. `make atari512_defconfig && make` must retain the portable path and build
   successfully.
4. `make rpi1_defconfig && make` must compile the ARM portable path.
5. Run `make gitready`.
