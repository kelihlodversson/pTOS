# Review Comment Fix Report

## Root Cause And Reproduction

The portable canonical resource materializer validated TEDINFO `te_ptext` and
`te_ptmplt` with `disk_string()`, which only required an in-image NUL byte.
It did not validate the decoded `te_txtlen` and `te_tmplen` values, even
though those fields are writable-buffer capacities.  A resource could
therefore declare a capacity beyond the end of its input image.

Before changing production code, `/tmp/opencode/tedinfo-span-repro.py` modeled
the existing acceptance condition with an image ending in `x\0` and a declared
text capacity of eight bytes.  Running it failed as intended with:

```
AssertionError: pre-fix loader accepts a TEDINFO text capacity beyond the RSC image
```

After the change, the same focused reproduction verifies rejection of the
overflowing and negative capacities, acceptance of an in-range NUL-terminated
string, and preservation of the `-1L` pointer sentinel.

## Changes

- `aes/gemrslib.c`: added `disk_string_capacity()` and use it for TEDINFO text
  and template strings.  It rejects negative lengths, ranges beyond the input
  image, and strings without a NUL within their declared capacity; `-1L`
  remains a valid absent-pointer sentinel after length validation.
- `vdi/vdi_raster.c`: corrected the comment pairing to
  `BM_S_ONLY/BM_S_OR_D`.
- `tools/mkciconrsc.py`: emits `const LONG cicontest_rsc_size`.
- `desk/cicontest_rsc.c`: regenerated from the generator with the exported
  size value.
- `desk/deskmain.c`: references the generated size symbol instead of owning a
  second fixture-size macro.

## Verification

Commands run and results:

```
python3 /tmp/opencode/tedinfo-span-repro.py
```

Passed after the production edit.

```
python3 tools/mkciconrsc.py | cmp -s desk/cicontest_rsc.c -
```

Passed; the checked-in generated fixture matches generator output.

```
make atari256_defconfig && make
```

Passed; produced `ptos256us.img`.  Existing unrelated compiler warnings were
reported in `vdi/vdi_text.c` and `aes/gemasync.c`.

```
make rpi2_defconfig && make
```

Passed; produced `kernel7.img`.  Existing unrelated warnings were reported in
AES vector access, `aes/gemasync.c`, CLI, and USB sources.

```
make gitready
git diff --check
```

Both passed.

## Commit

Implementation: `a75b5701 aes: validate TEDINFO string capacities`

## Concerns

No new concerns.  This validation intentionally rejects malformed TEDINFO
records whose current text or template does not fit its declared writable
capacity; no host unit-test framework exists, so target builds and the focused
boundary reproduction provide the available coverage.
