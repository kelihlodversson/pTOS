# Legacy RSC Loader for the 192 KB ROM

## Context

The portable, canonical big-endian RSC materializer added for #150 makes the
`atari192` image exceed its fixed 192 KB ROM size by 4332 bytes.  The target
already disables colour icons, and its m68k native RSC structures have the
same big-endian layout as Atari disk RSC records.  It can therefore retain the
previous in-place loader without losing a supported feature.

## Configuration

Add a hidden AES Kconfig boolean, `CONF_WITH_LEGACY_RSC_LOAD`:

- It depends on `ARCH_M68K`, because the in-place loader relies on the m68k
  native structure layout and must never be selected on ARM.
- It defaults to `y` for `TARGET_192` and `n` for every other target.
- Its help text explains that it exists solely to preserve the 192 KB ROM
  budget, while normal builds use the portable canonical loader.

Keeping the policy in Kconfig makes a future `TARGET_256` space decision a
default change rather than another source-level target fork.

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
- All non-192 KB targets continue to use canonical big-endian RSC parsing.
- No CICON capability is removed from a configuration that currently enables
  it.
- LTO is not part of this change.  A trial with GCC 13.3 fails because
  `aes/gen_asm_defines.c` needs compiler-emitted assembly, whereas `-flto`
  emits LTO IR at that build step.

## Verification

Build the selected boundary configurations:

1. `make atari192_defconfig && make` must create `ptos192us.img` within the
   192 KB budget.
2. `make atari256_defconfig && make` and `make atari512_defconfig && make`
   must keep using the portable path and build successfully.
3. `make rpi1_defconfig && make` must compile the ARM portable path.
4. Run `make gitready`.
