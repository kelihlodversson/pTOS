# Legacy RSC Loader Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore the 192 KB Atari ROM build by selecting the former in-place m68k RSC loader while retaining canonical portable RSC loading everywhere else.

**Architecture:** `CONF_WITH_LEGACY_RSC_LOAD` selects the m68k in-place loader for constrained ROM targets.  The portable parser, native materializer, and `rs_loadmem()` remain one branch of `aes/gemrslib.c`; the original pointer-fixup loader is the other.  Kconfig prevents ARM from selecting the legacy branch.

**Tech Stack:** GNU C90, Kconfig, GNU make, m68k-atari-mintelf-gcc, arm-none-eabi-gcc.

## Global Constraints

- `CONF_WITH_LEGACY_RSC_LOAD` depends on `ARCH_M68K` and must not compile on ARM.
- It defaults to `y` for `TARGET_192` and `n` for other m68k targets.
- The option remains visible in the AES Kconfig menu for m68k users with constrained ROM budgets.
- The portable canonical loader remains the default path and is unchanged on ARM and non-192 KB m68k configurations.
- `rs_loadmem()` is portable-loader-only; its CICON test-hook consumer cannot be selected for `TARGET_192`.
- Preserve C90 declarations-before-statements and 4-space indentation.
- Do not add host-side unit-test infrastructure; this freestanding project validates loader paths with target builds.

---

### Task 1: Add the legacy-loader configuration selector

**Files:**
- Modify: `aes/Kconfig:52-63`

**Interfaces:**
- Produces: `CONF_WITH_LEGACY_RSC_LOAD`, an always-emitted feature macro that is `1` only for explicitly selected m68k legacy-loader builds.
- Consumes: `ARCH_M68K` and `TARGET_192` Kconfig symbols.

- [ ] **Step 1: Verify the selector is absent and the 192 KB configuration has no portable-loader choice**

Run:

```sh
make atari192_defconfig
python3 -c 'from pathlib import Path; h = Path("obj/autoconf.h").read_text(); assert "CONF_WITH_LEGACY_RSC_LOAD" not in h'
```

Expected: the assertion passes before the Kconfig symbol exists.

- [ ] **Step 2: Add the selectable m68k-only option after `CONF_WITH_COLOUR_ICONS`**

```kconfig
config CONF_WITH_LEGACY_RSC_LOAD
	bool "Use legacy resource loader"
	depends on ARCH_M68K
	default y if TARGET_192
	default n
	help
	  Use the former in-place resource loader instead of the portable
	  canonical resource loader.  It relies on the m68k native structure
	  layout matching Atari's big-endian resource-file layout.

	  The 192 KB ROM enables this by default because its ROM budget cannot
	  accommodate the portable loader.  Other m68k images should normally
	  use the portable loader, but may select this option when space is
	  more important than the portable loader's canonical decoding and
	  validation.
```

- [ ] **Step 3: Verify generated configuration values**

Run:

```sh
make atari192_defconfig
make obj/autoconf.h
python3 -c 'from pathlib import Path; h = Path("obj/autoconf.h").read_text(); assert "#define CONF_WITH_LEGACY_RSC_LOAD 1" in h'
make atari256_defconfig
make obj/autoconf.h
python3 -c 'from pathlib import Path; h = Path("obj/autoconf.h").read_text(); assert "#define CONF_WITH_LEGACY_RSC_LOAD 0" in h'
make rpi1_defconfig
make obj/autoconf.h
python3 -c 'from pathlib import Path; h = Path("obj/autoconf.h").read_text(); assert "#define CONF_WITH_LEGACY_RSC_LOAD 0" in h'
```

Expected: only the 192 KB default selects the legacy path; ARM emits the disabled feature macro.

- [ ] **Step 4: Commit the configuration change**

```sh
git add aes/Kconfig
git commit -m "aes: add legacy RSC loader option"
```

### Task 2: Compile the former in-place loader for legacy selections

**Files:**
- Modify: `aes/gemrslib.c:73-438,571-651,1012-1037,1236-1555`
- Modify: `aes/gemrslib.h:13-38`

**Interfaces:**
- Consumes: `CONF_WITH_LEGACY_RSC_LOAD` from generated `config.h`.
- Produces: `rs_load()` using in-place m68k fixups when the feature is `1`; `rs_loadmem(AESGLOBAL *, const void *, LONG)` only when it is `0`.

- [ ] **Step 1: Reproduce the 192 KB size failure before changing the loader path**

Run:

```sh
make atari192_defconfig && make
```

Expected: `./mkrom: emutos.img is too big` and `ptos192us.img` is not produced.

- [ ] **Step 2: Guard portable-only declarations and helpers**

Wrap the disk record constants, `struct disk_rsc`, `struct native_rsc_layout`, `struct disk_cicon_info`, and every helper from `disk_range()` through `layout_ordinary()` in:

```c
#if !CONF_WITH_LEGACY_RSC_LOAD
/* canonical big-endian disk parsing and native materialization helpers */
#endif
```

Keep `fix_chpos()`, `get_addr()`, `best_match()`, `expand_cicondata()`,
`transform_cicon()`, `free_cicon_buffers()`, `rs_sglobe()`, `rs_free()`,
`rs_gaddr()`, `rs_saddr()`, `rs_fixit()`, `rs_load()`, and `rs_str()` shared
unless their logic is selected below.

- [ ] **Step 3: Restore the legacy CICON layout fixups under the legacy branch**

Replace the disabled `#if 0` around `fixup_colour_icons()` and
`fixup_all_ciconblks()` with `#if CONF_WITH_LEGACY_RSC_LOAD`.  Make the
CICON entry point select one of these exact forms:

```c
#if CONF_WITH_LEGACY_RSC_LOAD
static void fix_cicons(void)
{
    CICONBLK **ciconblkptr, **p;
    CICON *cicondata;
    LONG num_ciconblks;

    ciconblkptr = get_ciconblkptr(rs_hdr);
    if (!ciconblkptr)
        return;
    for (num_ciconblks = 0, p = ciconblkptr; *p != (CICONBLK *)-1L; p++)
        num_ciconblks++;
    cicondata = (CICON *)(p+1);
    fixup_all_ciconblks(num_ciconblks, ciconblkptr, cicondata);
    transform_all_cicons(num_ciconblks, ciconblkptr);
}
#else
static void transform_cicons(RSHDR *hdr)
{
    CICONBLK **ciconblkptr, **p;
    LONG num_ciconblks;

    ciconblkptr = get_ciconblkptr(hdr);
    if (!ciconblkptr)
        return;
    for (num_ciconblks = 0, p = ciconblkptr; *p != (CICONBLK *)-1L; p++)
        num_ciconblks++;
    transform_all_cicons(num_ciconblks, ciconblkptr);
}
#endif
```

- [ ] **Step 4: Restore legacy ordinary-resource pointer fixups**

Add these helpers in the legacy branch before `fix_objects()`:

```c
static BOOL fix_long(LONG *plong)
{
    LONG lngval;

    lngval = *plong;
    if (lngval != -1L)
    {
        *plong = (LONG)rs_hdr + lngval;
        return TRUE;
    }
    return FALSE;
}

static void fix_trindex(void)
{
    WORD ii;
    LONG *ptreebase;

    ptreebase = (LONG *)get_sub(R_TREE, rs_hdr->rsh_trindex, sizeof(LONG));
    rs_global->ap_ptree = (OBJECT **)ptreebase;
    for (ii = 0; ii < rs_hdr->rsh_ntree; ii++)
        fix_long(ptreebase+ii);
}

static void fix_nptrs(WORD cnt, WORD type)
{
    WORD i;

    for (i = 0; i < cnt; i++)
        fix_long(get_addr(type, i));
}

static BOOL fix_ptr(WORD type, WORD index)
{
    return fix_long(get_addr(type, index));
}

static void fix_tedinfo(void)
{
    WORD ii;
    TEDINFO *ted;

    for (ii = 0; ii < rs_hdr->rsh_nted; ii++)
    {
        ted = (TEDINFO *)get_addr(R_TEDINFO, ii);
        if (fix_ptr(R_TEPTEXT, ii))
            ted->te_txtlen = strlen(ted->te_ptext) + 1;
        if (fix_ptr(R_TEPTMPLT, ii))
            ted->te_tmplen = strlen(ted->te_ptmplt) + 1;
        fix_ptr(R_TEPVALID, ii);
    }
}
```

Make `fix_objects()` select legacy behavior for ordinary specs and CICONs:

```c
static void fix_objects(void)
{
    WORD ii;
    WORD obtype;
    OBJECT *obj;
#if CONF_WITH_COLOUR_ICONS
#if CONF_WITH_LEGACY_RSC_LOAD
    CICONBLK **ciconblkptr = get_ciconblkptr(rs_hdr);
#endif
    OBSPEC *spec;
#endif

    for (ii = 0; ii < rs_hdr->rsh_nobs; ii++)
    {
        obj = (OBJECT *)get_addr(R_OBJECT, ii);
        rs_obfix(obj, 0);
        obtype = obj->ob_type & 0x00ff;
        switch (obtype)
        {
        case G_CICON:
#if CONF_WITH_COLOUR_ICONS
#if CONF_WITH_LEGACY_RSC_LOAD
            if (ciconblkptr)
            {
                if (obj->ob_flags & INDIRECT)
                    fix_long(&obj->ob_spec.index);
                spec = (obj->ob_flags & INDIRECT) ? obj->ob_spec.indirect : &obj->ob_spec;
                spec->ciconblk = ciconblkptr[spec->index];
            }
#else
            spec = (obj->ob_flags & INDIRECT) ? obj->ob_spec.indirect : &obj->ob_spec;
            spec->ciconblk = get_ciconblkptr(rs_hdr)[spec->index];
#endif
#endif
            break;
        case G_BOX:
        case G_IBOX:
        case G_BOXCHAR:
            break;
        default:
#if CONF_WITH_LEGACY_RSC_LOAD
            fix_long(&obj->ob_spec.index);
#endif
            break;
        }
    }
}
```

- [ ] **Step 5: Select complete loader implementations**

Keep the current bounded `rs_readit()` and `rs_loadmem()` in the portable
branch.  Add the legacy `rs_readit()` in the alternate branch:

```c
static WORD rs_readit(AESGLOBAL *pglobal, UWORD fd)
{
    WORD ibcnt;
    LONG rslsize;
    RSHDR hdr_buff;

    if (dos_read(fd, sizeof(hdr_buff), &hdr_buff) != sizeof(hdr_buff))
        return FALSE;
    rslsize = hdr_buff.rsh_rssize;
#if CONF_WITH_COLOUR_ICONS
    if (hdr_buff.rsh_vrsn & NEW_FORMAT_RSC)
    {
        if (dos_lseek(fd, 0, rslsize) < 0L)
            return FALSE;
        if (dos_read(fd, sizeof(rslsize), &rslsize) != sizeof(rslsize))
            return FALSE;
    }
#endif
    rs_hdr = (RSHDR *)dos_alloc_anyram(rslsize);
    if (!rs_hdr)
        return FALSE;
    if (dos_lseek(fd, 0, 0x0L) < 0L)
        return FALSE;
    if (dos_read(fd, rslsize, rs_hdr) != rslsize)
        return FALSE;
    rs_global = pglobal;
    rs_global->ap_rscmem = rs_hdr;
    rs_global->ap_rsclen = rslsize;
    fix_trindex();
#if CONF_WITH_COLOUR_ICONS
    fix_cicons();
#endif
    fix_tedinfo();
    ibcnt = rs_hdr->rsh_nib;
    fix_nptrs(ibcnt, R_IBPMASK);
    fix_nptrs(ibcnt, R_IBPDATA);
    fix_nptrs(ibcnt, R_IBPTEXT);
    fix_nptrs(rs_hdr->rsh_nbb, R_BIPDATA);
    fix_nptrs(rs_hdr->rsh_nstring, R_FRSTR);
    fix_nptrs(rs_hdr->rsh_nimages, R_FRIMG);
    return TRUE;
}
```

Include `config.h` in `aes/gemrslib.h`, then guard the `rs_loadmem()`
declaration and its definition with:

```c
#if !CONF_WITH_LEGACY_RSC_LOAD
OBJECT *rs_loadmem(AESGLOBAL *pglobal, const void *rsmem, LONG size);
#endif
```

- [ ] **Step 6: Build both loader paths**

Run:

```sh
make atari192_defconfig && make
make atari256_defconfig && make
make rpi1_defconfig && make
```

Expected: `ptos192us.img` is produced; `atari256` and `rpi1` compile the
portable path successfully.

- [ ] **Step 7: Commit the loader selection**

```sh
git add aes/gemrslib.c aes/gemrslib.h
git commit -m "aes: use legacy RSC loader for 192K ROMs"
```

### Task 3: Verify the boundary configurations and source hygiene

**Files:**
- Modify: none expected

**Interfaces:**
- Verifies: the legacy path fits the 192 KB ROM and canonical loading remains
  available for 256 KB m68k, 512 KB m68k, and ARM targets.

- [ ] **Step 1: Build the complete selected matrix**

Run:

```sh
make atari192_defconfig && make
make atari256_defconfig && make
make atari512_defconfig && make
make rpi1_defconfig && make
```

Expected: every command exits zero; `atari192` reports `ptos192us.img is ready`.

- [ ] **Step 2: Verify generated selector values at the target boundaries**

Run:

```sh
make atari192_defconfig
make obj/autoconf.h
python3 -c 'from pathlib import Path; assert "#define CONF_WITH_LEGACY_RSC_LOAD 1" in Path("obj/autoconf.h").read_text()'
make atari256_defconfig
make obj/autoconf.h
python3 -c 'from pathlib import Path; assert "#define CONF_WITH_LEGACY_RSC_LOAD 0" in Path("obj/autoconf.h").read_text()'
make rpi1_defconfig
make obj/autoconf.h
python3 -c 'from pathlib import Path; assert "#define CONF_WITH_LEGACY_RSC_LOAD 0" in Path("obj/autoconf.h").read_text()'
```

Expected: the legacy implementation is selected only for the constrained m68k image.

- [ ] **Step 3: Run repository formatting and safety checks**

Run:

```sh
make gitready
git diff --check
```

Expected: both commands exit zero.

- [ ] **Step 4: Commit verification-only documentation only if verification changes it**

Do not create an empty commit.  If an existing specification needs an evidence
update, commit only that file:

```sh
git add docs/superpowers/specs/2026-08-10-legacy-rsc-loader-design.md
git commit -m "docs: record legacy RSC loader verification"
```
