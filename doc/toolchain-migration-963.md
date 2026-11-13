# Packaged toolchain migration validation (#963)

pTOS now defaults to the SDKs from the signed apt.ptos.dev repository on
Linux and the pTOS Homebrew tap on macOS. The validation SDK is 0.2.0,
GCC 16.2.0 and Binutils 2.45. Local APT installation used the checksum-verified
official release packages in Ubuntu 24.04 because the managed environment
blocks apt.ptos.dev. CI exercises the public signed repository directly.
Homebrew installation and all three target builds run on Apple Silicon macOS 26.

## m68k correctness workaround and measured cost

[Toolchain issue #15](https://github.com/kelihlodversson/pTOS-toolchain/issues/15)
records a GCC 16 auto-inc-dec miscompilation: source post-increment is combined
with a destination that also uses that address register. Page-table copies
shift by four bytes and the address-space kernel faults immediately. Builds
use `-fno-auto-inc-dec` for SDK m68k code, including LTO, bundled libcmini,
programs and the runtime linker. Remove the workaround only after a fixed SDK
has passed the runtime tests.

The SDK and workaround change the measured graphics performance. The new
512 KiB and fastcall references are the complete results from
[pBench run 43480691522](https://github.com/kelihlodversson/pTOS/actions/runs/43480691522),
head `91cc8b3e93942e87cbd99a9e16f1541715f513db`, merge image `7e60fd53`.
They use the same Ubuntu 26.04 Hatari 2.6.1 and benchmark workloads as the old
references. The table preserves the comparison with the previous committed
references; negative values mean slower. Thresholds remain 3% for the geometric
mean and 15% for a primitive. The 256 KiB references stay unchanged because its
comparison passed.

| Mode | Display geometric mean | Slowest primitive change |
| --- | ---: | ---: |
| `atari512-f030-blitter` | -2.30% | -10.48% |
| `atari512-f030-noblitter` | -2.20% | -7.78% |
| `atari512-f040-blitter` | -0.72% | -10.85% |
| `atari512-f040-noblitter` | -0.06% | -10.66% |
| `atari512-fastcall-f030-blitter` | -1.89% | -11.24% |
| `atari512-fastcall-f030-noblitter` | -3.10% | -10.91% |
| `atari512-fastcall-f040-blitter` | -1.60% | -10.81% |
| `atari512-fastcall-f040-noblitter` | -1.13% | -10.15% |
| `atari512-high-blitter` | -2.53% | -10.85% |
| `atari512-high-noblitter` | -2.51% | -10.84% |
| `atari512-low-blitter` | -4.07% | -13.27% |
| `atari512-low-noblitter` | -2.99% | -10.84% |
| `atari512-med-blitter` | -3.60% | -11.97% |
| `atari512-med-noblitter` | -3.00% | -10.84% |
