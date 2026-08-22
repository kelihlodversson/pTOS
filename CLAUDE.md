# pTOS

Portable EmuTOS. The "p" stands for portable — and for the Raspberry Pi, the
first machine this port of [EmuTOS](https://github.com/emutos/emutos) (a free
implementation of the Atari TOS) targets, with the ambition of supporting
further hardware later on.

Portability is therefore the point, not a side effect: the upstream Atari,
Amiga and ColdFire targets still build, so a change to shared code has to keep
working on m68k as well as on ARM, and anything machine specific belongs behind
a configuration option or in an `arch/`/`machine/` subdirectory.

This is a freestanding OS image: no libc, no host runtime. Everything the code
needs lives in `util/` and `include/`.

## Workflow

Every change starts from a GitHub issue. If none exists for the work, create
it first.

1. Create a branch off `master` named `{type}/{issue}-{title-with-dashes}`,
   where `{type}` is the kind of work (`feature`, `bugfix`, `chore`, …) and
   `{issue}` is the issue number — e.g. `bugfix/37-fix-usb-detection`.
2. Immediately push an empty commit and open a **draft** PR from it, before
   any real work exists:
   ```sh
   git commit --allow-empty -m "Start work on #<issue>"
   git push -u origin HEAD
   gh pr create --draft --title "..." --body "Fixes #<issue>"
   ```
3. Then do the work. For anything longer than a single sitting, commit and
   push incrementally rather than holding a large diff locally.
4. When it's done, mark the PR ready for review (`gh pr ready`).

The user merges the PR themselves once they're satisfied and any review
comments are addressed — never merge or force-push a PR as part of this flow.

## Building

The build is configured the way the Linux kernel is. There is no `make rpi2`;
you pick a configuration first, then build.

```sh
make help                # list the configurations in configs/
make rpi2_defconfig      # load one
make menuconfig          # optional: adjust it
make                     # build the image named in the "is ready" line
```

Requirements: GNU make, `pip3 install kconfiglib`, and a cross toolchain —
`arm-none-eabi-*` for the Raspberry Pi, and for everything else one of the
three offered under "m68k toolchain" in the Toolchain menu: `m68k-atari-mintelf-`
(the default), `m68k-atari-mint-` (cross-mint), or a bare `m68k-elf-`.  That
choice sets both the prefix and the flags; `doc/install.txt` says where to get
them.

`make clean` keeps `.config`; `make distclean` removes it too. Changing
`.config` rebuilds everything, which is intended.

To smoke-test a build, use the `ptos-smoketest` skill
(`.claude/skills/ptos-smoketest/SKILL.md`). It has the verified QEMU
invocations for the raspi1 (QEMU machine `raspi1ap`), raspi2 (QEMU machine
`raspi2b`), virt-arm and virt-m68k machines and the Hatari
invocations for the Atari m68k targets, with pass signals and emulator
gotchas (unreliable Hatari debugger breakpoints, the Falcon IDE 31 s boot
wait). Minimal raspi1 smoke test, for reference:

```sh
make rpi1_defconfig && make
qemu-system-arm -M raspi1ap -bios kernel.img -d guest_errors -serial stdio
```

## How the configuration works

`.config` is the single source of truth. From it `tools/genconfig.py` generates
two files that must never be edited by hand:

- `obj/autoconf.h` — included by every source file through `include/config.h`
- `obj/auto.conf` — included by the top level `Makefile`

Symbol names are the C macro names, with no `CONFIG_` prefix. Three naming
conventions decide how a symbol reaches the code:

| Name | In C | Use it for |
| --- | --- | --- |
| `CONF_WITH_FOO`, and anything else | always defined, `0` or `1` — test with `#if` | features |
| `MACHINE_*`, `TARGET_*` | defined only when set — test with `#ifdef` | machine and image selection |
| `BUILD_*` | never emitted | build-system-only settings |

`-Wundef` is on, so `#if SOMETHING_UNDEFINED` is an error waiting to happen;
that is why plain feature options are always defined.

### Adding an option

Put it in the `Kconfig` of the directory it belongs to (`bios/Kconfig`,
`vdi/Kconfig`, …) or in one of the top level `Kconfig.machine`, `.image`,
`.i18n`, `.debug`. Give it a help text and real `depends on` clauses — a
combination that cannot work should be impossible to select, not something that
fails at compile time.

Do **not** add per-target defaults to `include/config.h`. That file now holds
only values derived from the configuration, the fixed system limits, and sanity
checks.

### Adding a source file

Add its object to the `obj-y` list in that directory's `build.mk`, Kbuild
style:

```make
obj-y += foo.o                    # always
obj-$(CONF_WITH_FOO) += foo.o     # only when the option is set
obj-$(ARCH_ARM) += foo.o          # only on ARM
```

Ordering inside `build.mk` is link order, and a few entries depend on it:
`startup.o` must stay first in `bios/`, `endvdi.o` last in `vdi/`, `endgem.o`
last in `desk/`.

### Adding a configuration

Adjust it with `make menuconfig`, run `make savedefconfig`, and copy the
resulting `./defconfig` to `configs/<name>_defconfig`.

## Layout

`bios/ bdos/ vdi/ aes/ desk/ cli/ usb/ util/` hold the OS itself; `include/` the
shared headers; `tools/` the host programs the build runs.

Each of those may have `arch/<m68k|coldfire|arm>/` and `machine/<raspi|…>/`
subdirectories. They are searched before the generic directory through `vpath`,
so `bios/arch/m68k/memory.S` and `bios/machine/raspi/memory.c` are two
implementations of the same `obj/memory.o`, chosen by the configuration. Prefer
that over `#ifdef __arm__` in a shared file.

Objects land flat in `obj/`, so **source basenames must be unique across the
whole tree** unless they are deliberately alternative implementations of the
same object.

## Code conventions

`doc/coding.txt` is the authority; the parts that bite most often:

- C90 with GNU extensions (`-std=gnu90`): declarations at the top of a block.
  The codebase overwhelmingly uses `/* */` comments; match the file you are in.
- 4 spaces, never a hard tab, in `.c`, `.h` and `.S`. Run `make gitready`
  before committing.
- **`int` is 16 bits on m68k** (`-mshort`) and 32 bits on ARM. Suffix
  constants that must survive on m68k (`132 * 1000UL`). For inherited/ABI-
  facing code, use the traditional `portab.h` types — `WORD`, `LONG`,
  `UBYTE`, `UWORD`, `ULONG`, `BOOL`; for new architecture-neutral
  infrastructure, follow Type naming below instead.
- Assembler: leading underscore on symbols callable from C, CDECL conventions,
  68000-only instructions, `movem.l` never `movem.w`.
- Trace with `KDEBUG(("..."))` / `KINFO(())` from `include/kprint.h`, not with
  a private printf.

### Type naming

- Reserve `_t`-suffixed names for types the C implementation or a system
  header provides — `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`,
  `uintptr_t`, `intptr_t`, `size_t`, and the like. Do not introduce new
  project-defined typedefs ending in `_t`; someone reading `_t` should be able
  to assume it did not come from this tree.
- Name new pTOS-defined semantic typedefs with a `_type` suffix instead, e.g.
  `typedef uintptr_t virt_addr_type;` or `typedef uint32_t mmu_attr_type;`.
  Don't add a `ptos_` prefix merely for branding — only reach for one if a
  real name collision requires it.
- In new, architecture-neutral infrastructure, prefer the standard fixed-width
  and pointer-width C types (`uint32_t`, `uintptr_t`, `size_t`, ...) over the
  inherited all-caps aliases (`ULONG`, `UQUAD`, ...) when the only thing being
  expressed is integer or pointer width.
- Leave `BYTE`, `WORD`, `LONG`, `UBYTE`, `UWORD`, `ULONG`, `QUAD` and `UQUAD`
  exactly as they are in existing code — do not mechanically convert them.
  They stay the right choice where the code is inherited from EmuTOS (converting
  it just creates upstream/cherry-pick noise), where the name expresses a
  TOS/GEM ABI concept rather than a plain integer width, or where the type is
  part of a public ABI-facing structure or interface with no separate reason
  to touch it.

## Documentation

- `doc/install.txt` — the build system in full, and every configuration
- `doc/coding.txt` — coding style
- `doc/country.txt`, `doc/nls.txt` — countries, keyboard layouts, translations
- `doc/status.txt`, `doc/bugs.txt` — what works and what does not
- `readme.md` — what this fork is about and where it is going
