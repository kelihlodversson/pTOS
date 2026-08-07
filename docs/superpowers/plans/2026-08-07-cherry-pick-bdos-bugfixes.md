# Cherry-pick upstream bdos/ bugfixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port nine upstream EmuTOS `bdos/` correctness fixes (plus the one prerequisite OFD/DFD restructure) to pTOS, as one PR with one commit per fix, reconciled by hand to pTOS's divergent tree.

**Architecture:** This is a *port*, not a blind `git cherry-pick`. pTOS's `bdos/` diverges from upstream (ARM/ELF-loader maintenance in `kpgmld.c`/`proc.c`, FastRAM 4-byte alignment in `iumem.c`, `le2cpu16`/`le2cpu32` endian conversions in `makopn`, and a restructured `xrw()` in `fsio.c`). Each fix is applied to pTOS's current code; the upstream commit gives the canonical change. Each fix lands as its own commit, ordered low-to-high risk; the tree builds and stays green after every commit.

**Tech Stack:** C90 with GNU extensions (`-std=gnu90'), freestanding (no libc). Kconfig build (`.config` → `obj/autoconf.h`). Cross toolchains: `arm-none-eabi-` (ARM), `m68k-atari-mintelf-` (m68k, default since PR #128). Emulators for verification: QEMU `raspi2b` (ARM), Hatari `ste` (m68k).

## Global Constraints

- Work on branch `bugfix/109-cherry-pick-upstream-bdos-bugfixes` (already created; draft PR #130 open). Push after each commit (user instruction).
- After EVERY task edit: run `make gitready` (whitespace/style: 4 spaces, no tabs), then build both arches:
  - m68k: `make atari512_defconfig && make` — mintelf is now the default; do NOT hand-edit `.config` for the toolchain.
  - ARM: `make rpi2_defconfig && make`.
  - Note: `make <cfg>_defconfig` overwrites `.config`; the final task restores whatever the user's preferred `.config` was, but during the per-commit loop you will repeatedly switch configs. Each `make <cfg>_defconfig && make` pair is self-contained.
  - Both must reach the `... is ready` line with no new warnings.
- `int` is 16-bit on m68k (`-mshort`); use the `portab.h` types (`WORD`, `LONG`, `BOOL`, etc.) and suffix constants that must survive on m68k (`0L`, `1UL`). The fixes here are mostly small and arch-neutral; where a new declaration is needed it uses the surrounding file's existing type idiom.
- C90: declarations at the top of a block. Comments `/* */` to match the file. No trailing whitespace (gitready checks).
- Commit messages record both the upstream commit hash and the pTOS-specific adaptation. Suggested format:
  ```
  <short title>

  Ports upstream EmuTOS <hash>: <one line on what it fixes>.

  pTOS adaptation: <one line on what was reconciled — endian conversions,
  ELF path, alignment rounding, etc., or "none (applies clean)">.

  Refs #109
  ```
- Emulator gotchas (from the ptos-smoketest skill): QEMU raspi2 machine is `raspi2b` (not `raspi2`) on QEMU ≥ 9; pass `-d guest_errors`; the benign `bcm2835_systmr_write: read-only ofs 0x4` is a known QEMU quirk, ignore it. Hatari breakpoints are unreliable; verify boot via `--run-vbls` + AVI frame inspection, never breakpoints.
- Fork point: `aaf30d28fb`. All upstream hashes are present in the local object store (the fork history carries upstream), so `git show <hash>` works without an `upstream` remote.

---

## File Structure

| File | Role in this PR | Tasks |
|------|-----------------|-------|
| `bdos/fsdir.c` | `dcrack()`, `xrename()` | 1, 2, 9a |
| `bdos/kpgmld.c` | `pgfix01()` | 3 |
| `bdos/fsopnclo.c` | `ixcreat()`, `makopn()`, `sftdel()`, `xclose()`, `ixclose()` | 4, 8, 9a, 9b |
| `bdos/fsio.c` | `xrw()`, `addit()`, `eof()`, `xlseek()`, `ixlseek()`, `ixread()` | 5 (verify-only), 9a |
| `bdos/proc.c` | `xexec()` | 6 |
| `bdos/iumem.c` | `shrinkit()`, `freeit()` | 7 |
| `bdos/fs.h` | `DFD` typedef, `OFD` struct, `O_DIRTY` macro | 9a, 9b |
| `bdos/fsdrive.c` | `log_media()` | 9a |
| `bdos/fsfat.c` | `nextcl()` | 9a |
| `bdos/proc.h` | (no change in pTOS — upstream `8e04469b` touches it, pTOS does not) | — |

Buy-laid blast-radius facts (verified, current master `77b7c31b`):
- Only `bdos/fsdir.c`, `fsdrive.c`, `fsfat.c`, `fsio.c`, `fsopnclo.c` reference the OFD fields the 1c120131 restructure moves (`o_flag`, `o_td`, `o_strtcl`, `o_fileln`, `o_usecnt`). No `include/`, `bios/`, `aes/` file does. The restructure is contained.

---

### Task 1: dcrack() — guard against an empty pathname followed by a colon (`323ee921`)

**Files:**
- Modify: `bdos/fsdir.c` (function `dcrack`, currently ~line 1672)

**Interfaces:** none exposed; `dcrack()` is `static`.

**Reference:** orphan pTOS commit `7266b4f5` already carried this exact one-line adaptation; reproduce it verbatim.

- [ ] **Step 1: Inspect the current site**

```sh
grep -n "if we start with drive spec" bdos/fsdir.c
```

Expect line ~1672:
```c
    if (n[1] == ':')            /*  if we start with drive spec */
```

- [ ] **Step 2: Apply the one-line guard**

Change that line to:
```c
    if (n[0] && (n[1] == ':'))  /*  if we start with drive spec */
```

(The bug: `dcrack(const char **np)` dereferences `n[1]` without checking `n[0]`. When the caller passes an empty string immediately followed elsewhere by `:`, `n[0]` is `'\0'`, the old test still read `n[1]`, mis-parsing a phantom drive spec.)

- [ ] **Step 3: Verify**

```sh
make gitready
make atari512_defconfig && make
make rpi2_defconfig && make
```
All green, `... is ready` printed for both.

- [ ] **Step 4: Commit**

```
dcrack: guard against empty pathname followed by a colon

Ports upstream EmuTOS 323ee921: an empty pathname followed by ':' was
mis-parsed as a drive spec because n[0] was not tested.

pTOS adaptation: none (identical to orphan pTOS commit 7266b4f5, applies
clean).

Refs #109
```
Steps: `git add bdos/fsdir.c && git commit && git push`.

---

### Task 2: xrename() — disallow renaming a read-only file (`96165134`)

**Files:**
- Modify: `bdos/fsdir.c` (function `xrename`, currently ~line 918)

**Reference:** orphan pTOS commit `673f688c` already carried this exact 6-line adaptation; reproduce verbatim. (Issue #109's paraphrase inverts the direction; the patch is unambiguous — it *adds* the disallow.)

- [ ] **Step 1: Inspect the current site**

```sh
grep -n "if (!f).*old path doesn't exist" bdos/fsdir.c
grep -n "at this point:" -A2 bdos/fsdir.c
```
Expect ~line 916–918:
```c
    if (!f)                     /* old path doesn't exist */
        return EFILNF;

    /* at this point:
```

- [ ] **Step 2: Insert the 6-line guard between the two**

Between the `if (!f) return EFILNF;` and the `/* at this point:` comment, insert:
```c
    /*
     * renames are forbidden for Read-Only files
     */
    if (f->f_attrib & FA_RO)
        return EACCDN;

```

- [ ] **Step 3: Verify**

```sh
make gitready
make atari512_defconfig && make
make rpi2_defconfig && make
```

- [ ] **Step 4: Commit**

```
xrename: disallow renaming a read-only file

Ports upstream EmuTOS 96165134: renaming a read-only file was allowed
(bug introduced upstream by 7b03a8a). Add an FA_RO -> EACCDN guard in
xrename() so the operation is rejected, matching Atari TOS.

pTOS adaptation: none (identical to orphan pTOS commit 673f688c, applies
clean).

Refs #109
```
`git add bdos/fsdir.c && git commit && git push`.

---

### Task 3: pgfix01() — reject an odd relocation offset (`05797930`)

**Files:**
- Modify: `bdos/kpgmld.c` (function `pgfix01`, currently ~line 311)

**Reference:** orphan pTOS commit `8eb432e2` already adapted this to pTOS's `void *lastcp` signature; reproduce verbatim. (Skip `1e6758ef` — duplicate of this fix.)

- [ ] **Step 1: Locate the existing `cp >= bbase` test**

```sh
grep -n "if (cp >= bbase)" bdos/kpgmld.c
```
Expect ~line 313, inside `pgfix01()`:
```c
            if (cp >= bbase)
                return EPLFMT;
            *((long *)cp) += tbase;
```

- [ ] **Step 2: Add the odd-offset test immediately after the `bbase` test**

```c
            if (cp >= bbase)
                return EPLFMT;
            if (((LONG)cp) & 1)
                return EPLFMT;
            *((long *)cp) += tbase;
```

(The bug: a PRG with a corrupted relocation table containing an odd offset crashed with an address error on 68000 — `*((long *)cp)` requires 2-byte alignment — exactly like Atari TOS, instead of returning `EPLFMT`.)

- [ ] **Step 3: Verify**

```sh
make gitready
make atari512_defconfig && make
make rpi2_defconfig && make
```
(`LONG` is a `portab.h` type; no new suffixes needed. `((LONG)cp)` widens the pointer for the `& 1` mask, safe on both `int`-16 and `int`-32 arches.)

- [ ] **Step 4: Commit**

```
kpgmld: return EPLFMT for an odd relocation-table offset

Ports upstream EmuTOS 05797930: a corrupted PRG relocation table with an
odd offset crashed with a 68000 address error (the *((long*)cp) write needed
2-byte alignment). Return EPLFMT instead.

pTOS adaptation: none — the signature is already `void *lastcp` in pTOS;
the orphan pTOS commit 8eb432e2 is reproduced verbatim. (1e6758ef is a
duplicate and is skipped.)

Refs #109
```
`git add bdos/kpgmld.c && git commit && git push`.

---

### Task 4: ixcreat() — check ixdel() when overwriting an existing file (`777991d4`)

**Files:**
- Modify: `bdos/fsopnclo.c` (function `ixcreat`, currently ~line 161)

- [ ] **Step 1: Locate the bare `ixdel` call**

```sh
grep -n "ixdel(dn,f,pos);" bdos/fsopnclo.c
```
Expect ~line 161, inside `ixcreat()`:
```c
        pos -= 32;
        ixdel(dn,f,pos);
    }
    else
        pos = 0;
```
(Note pTOS uses `pos -= 32;` here, whereas upstream used `pos -= sizeof(FCB);` — both compile to the same value since `sizeof(FCB) == 32`; do not change this line.)

- [ ] **Step 2: Check the return value of ixdel**

```c
        pos -= 32;
        if (ixdel(dn,f,pos) < 0)    /* file currently open by another process? */
            return EACCDN;
    }
    else
        pos = 0;
```

(The bug: `Fcreate()` over an existing file called `ixdel()` to delete it, but did not check that the delete worked. If the file was open in another process the delete could fail, yet `Fcreate()` continued and could create a second file with the same name.)

- [ ] **Step 3: Verify**

```sh
make gitready
make atari512_defconfig && make
make rpi2_defconfig && make
```

- [ ] **Step 4: Commit**

```
ixcreat: check ixdel() return when overwriting an existing file

Ports upstream EmuTOS 777991d4: Fcreate() deleted an existing file before
recreating it but ignored ixdel()'s return, allowing a duplicate filename
when the existing file was open in another process.

pTOS adaptation: none (the conflict was context drift; the edited hunk
matches 1:1). pTOS uses `pos -= 32` here vs upstream `pos -= sizeof(FCB)`;
left unchanged — identical value.

Refs #109
```
`git add bdos/fsopnclo.c && git commit && git push`.

---

### Task 5: VERIFY-THEN-DECIDE — first file in empty filesystem corrupts its first cluster (`d4757152`)

**Files:**
- Possibly modify: `bdos/fsio.c` (function `xrw`, currently line 315). Possibly NO change.

**Why this is gated:** upstream `d4757152` fixes the "first time through" sentinel `if (last == 0L) last = p->o_currec;` in the *upstream* helper `xrw_recs()`. pTOS's `fsio.c` was **restructured**: the single `static long xrw(int wrtflg, OFD *p, long len, char *ubufr)` (line 315) absorbed the body and REPLACED the sentinel with a contiguity test `if ((!rc) && (p->o_currec == last + nrecs))` (line 415). The textual fix upstream replaces (`if (last == 0L)`) **does not exist in pTOS**. The bug may have been restructured away. Confirm before porting.

- [ ] **Step 1: Confirm the structural divergence**

```sh
grep -n "if (last == 0L)" bdos/fsio.c                   # expect: NO match
grep -n "xrw\|last = nrecs = 0L\|p->o_currec == last + nrecs" bdos/fsio.c
```
Expected: `xrw` at line 315, `last = nrecs = 0L;` at line 403, `p->o_currec == last + nrecs` at line 415. No `if (last == 0L)` sentinel.

- [ ] **Step 2: Read pTOS's whole-cluster loop**

`bdos/fsio.c`:
```c
        last = nrecs = 0L;          /* line 403 */
        nbyts = lflg = 0;

        while (num--)               /* for each whole cluster */
        {
            rc = nextcl(p,wrtflg);
            if ((!rc) && (p->o_currec == last + nrecs))        /* contiguous? */
            {
                nrecs += dm->m_clsiz;
                nbyts += dm->m_clsizb;
                if (!num) goto mulio;
            }
            else
            {
                if (!num) lflg = 1;
mulio:
                if (nrecs) usrio(wrtflg,nrecs,last,ubufr,dm);
                ubufr += nbyts;
                addit(p,nbyts,0);
                if (rc) goto eof;
                last = p->o_currec;        /* line 432: set on non-contiguous */
                nrecs = dm->m_clsiz;
                nbyts = dm->m_clsizb;
                ...
            }
        }
```
Trace the first-file-empty-FS write: a new file's first allocated record is non-zero (it sits after the root dir region), so on the first pass `p->o_currec != 0`, while `last == 0`, `nrecs == 0` ⇒ the contiguity test `(p->o_currec == 0)` is **false** ⇒ the `else` branch runs with `nrecs == 0` ⇒ `usrio(wrtflg,0,last=0,...)` writes zero records (harmless) ⇒ `last = p->o_currec`. The buggy sentinel upstream was that `last == 0L` re-fired on the next pass; pTOS has no such re-firing sentinel.

- [ ] **Step 3: Decide**

If the trace confirms pTOS's restructured loop does **not** reintroduce the `last == 0L` sentinel, **SKIP the port** — record the decision so a future re-sync against upstream is unambiguous:

```
fsio: skip d4757152 — restructured away in pTOS

Upstream EmuTOS d4757152 fixes an "first time through" sentinel
`if (last == 0L) last = p->o_currec;` in xrw_recs(). pTOS's fsio.c was
restructured into xrw() which uses a `p->o_currec == last + nrecs`
contiguity test and sets `last = p->o_currec` only on the non-contiguous
(else) branch. The buggy sentinel upstream replaces does not exist in
pTOS, so the first-file-in-empty-filesystem corruption d4757152 targets
does not reproduce here.

Refs #109
```
This is still a commit (an empty or note-only commit) so the loop is closed in the PR history; OR simply Omit this commit and document the omission in the PR description. **Recommended: omit the commit; add a one-line note to the PR body** ("`d4757152` skipped — pTOS's restructured `xrw()` no longer contains the `last == 0L` sentinel the fix replaces").

If a runtime check is wanted before committing to skipping: build `atari512`, run under Hatari STE (`hatari --tos ptos512k.img --machine ste --memsize 4 --sound off --run-vbls 1200`), attach an empty floppy image via `--drive-a empty.st` (create a 720 KB zeroed file), and from the EmuTOS desktop format the floppy then create the first file — verify the file persists and re-reads without cluster corruption. This is optional; the static trace is the primary evidence.

If, instead, re-examination shows the bug DOES reproduce, apply the conceptual fix: add a `BOOL first_time;` local in `xrw`'s block (line 326 declarations), set `first_time = TRUE;` after `last = nrecs = 0L;` (line 403), and guard the `last = p->o_currec;` at line 432 accordingly so that an initial `o_currec` of 0 cannot be re-clobbered. Do NOT port upstream's `xrw_recs` structure back.

- [ ] **Step 4: Verify (whichever branch)**

```sh
make gitready
make atari512_defconfig && make
make rpi2_defconfig && make
```
`git add` (only if a real change was made) `&& git commit && git push`.

---

### Task 6: xexec() — close the file handle on load-error paths (`8e04469b`)

**Files:**
- Modify: `bdos/proc.c` (function `xexec`, currently ~lines 293, 304, 325–336)

**Why pTOS differs from upstream:** upstream also refactored `kpgmhdrld()`'s signature (it moved the `xopen()` into `xexec()`). **pTOS does NOT need that refactor**: pTOS's `kpgmhdrld()` already self-closes the handle on its own error paths (the `fail: xclose(*h);` block at `bdos/kpgmld.c` ~106, present for the ELF/PRG dispatch), and pTOS's `kpgmld()` already closes `h` unconditionally before returning (`bdos/kpgmld.c:134`). The leak is therefore confined to the three `xexec()` paths that run **between** `kpgmhdrld()`'s success and the `kpgmld()` call. So this task touches ONLY `bdos/proc.c` — no `kpgmld.c` signature change, no `proc.h` change.

- [ ] **Step 1: Re-read the three leak sites**

```sh
sed -n '283,350p' bdos/proc.c
```
The relevant structure in pTOS's `xexec()`:
```c
    rc = kpgmhdrld(path, &hdr, &fh);       /* line 283 — fh is now open */
    if (rc) {
        KDEBUG(("BDOS xexec: kpgmhdrld returned %ld (0x%lx)\n",rc,rc));
        return rc;                         /* kpgmhdrld already closed fh on its error path */
    }

    env_ptr = alloc_env(hdr.h01_flags, env);
    if (env_ptr == NULL) {
        KDEBUG(("BDOS xexec: no memory for environment\n"));
        return ENSMEM;                     /* LEAK 1: fh open, not closed */
    }

    needed = hdr.h01_tlen + hdr.h01_dlen + hdr.h01_blen + sizeof(PD);
    p = (PD *)alloc_tpa(hdr.h01_flags,needed,&max);
    if (p == NULL) {
        KDEBUG(("BDOS xexec: no memory for TPA\n"));
        xmfree(env_ptr);
        return ENSMEM;                     /* LEAK 2: fh open, not closed */
    }
    ...
    memcpy(bakbuf, errbuf, sizeof(errbuf));
    if (setjmp(errbuf)) {
        KDEBUG(("Error and longjmp in xexec()!\n"));
        xmfree(cur_p->p_env);
        xmfree(cur_p);
        /* LEAK 3: fh open, not closed — and kpgmld()'s own xclose(h)
         * was bypassed by the longjmp, so even the later kpgmld path
         * would not have closed it here. */
        longjmp(bakbuf, 1);
    }
    ...
    rc = kpgmld(cur_p, fh, &hdr);          /* kpgmld() closes fh itself on both rc and !rc */
```
(The `return rc;` after `kpgmhdrld` does NOT leak: pTOS's `kpgmhdrld` has the `fail: xclose(*h)` label and returns through it on every error. The post-`kpgmld` path does NOT leak: `kpgmld` does `xclose(h)` at line 134. Only the three intermediate paths leak.)

- [ ] **Step 2: Insert `xclose(fh);` on the three paths**

In `xexec()`, at the `alloc_env` failure (Leak 1):
```c
    env_ptr = alloc_env(hdr.h01_flags, env);
    if (env_ptr == NULL) {
        KDEBUG(("BDOS xexec: no memory for environment\n"));
        xclose(fh);
        return ENSMEM;
    }
```
At the `alloc_tpa` failure (Leak 2):
```c
    if (p == NULL) {
        KDEBUG(("BDOS xexec: no memory for TPA\n"));
        xmfree(env_ptr);
        xclose(fh);
        return ENSMEM;
    }
```
In the `longjmp` handler (Leak 3), before `longjmp`:
```c
    if (setjmp(errbuf)) {
        KDEBUG(("Error and longjmp in xexec()!\n"));
        /* free any memory allocated so far & close the file */
        xmfree(cur_p->p_env);
        xmfree(cur_p);
        xclose(fh);
        longjmp(bakbuf, 1);
    }
```
(Update the preceding comment, which currently says "free any memory allocated yet", to mention closing the file — match upstream's wording "free any memory allocated so far & close the file".)

No change to `kpgmhdrld()`'s signature, no change to `kpgmld.c`, no change to `proc.h`.

- [ ] **Step 3: Verify**

```sh
make gitready
make atari512_defconfig && make
make rpi2_defconfig && make
```
(Both the PRG and ELF load paths now close `fh` on every post-`kpgmhdrld` failure, because the ELF path goes through the same `xexec()` body — the leak fix applies to both program formats for free.)

- [ ] **Step 4: Commit**

```
xexec: close the file handle on the post-kpgmhdrld load-error paths

Ports upstream EmuTOS 8e04469b: Pexec() opened a file handle in
kpgmhdrld() but several error paths before the file was fully loaded
returned without closing it, leaking the handle (reported by Ryan Daum,
Jan 2017).

pTOS adaptation: drop upstream's kpgmhdrld() signature refactor (pTOS's
kpgmhdrld keeps the open and self-closes on its own error paths, needed
for the ELF/PRG dispatch; pTOS's kpgmld() also already closes h). The
leak is confined to the three xexec() paths between kpgmhdrld() success
and the kpgmld() call: env alloc fail, TPA alloc fail, and the setjmp
longjmp handler (where kpgmld()'s own xclose(h) is bypassed). Insert
xclose(fh) on those three paths only. No kpgmld.c or proc.h change.

Refs #109
```
`git add bdos/proc.c && git commit && git push`.

---

### Task 7: shrinkit() — coalesce the freed portion via freeit() (`e65ae149`)

**Files:**
- Modify: `bdos/iumem.c` (function `shrinkit`, currently lines 231–273)

**What pTOS adds that must survive:** `shrinkit()` rounds `newlen` up to a multiple of 4 bytes "to keep alignment; alignment on long boundaries is faster in FastRAM" (lines 234–238). Upstream's `e65ae149` does not have this rounding. KEEP IT. Upstream's mechanism: place the freed portion's MD on the **allocated** list, update `m->m_length`, then call `freeit(f, mp)` which coalesces — instead of inserting directly into the free list (which breaks coalescing when `Mshrink()` is called repeatedly on the same block, leaving 2+ adjacent MDs describing adjacent free memory).

- [ ] **Step 1: Re-read the current `shrinkit()`**

```sh
sed -n '231,273p' bdos/iumem.c
```
Current (the bug):
```c
WORD shrinkit(MD *m, MPB *mp, LONG newlen)
{
    MD *f, *p, *q;
    /*
     * round the size up to a multiple of 4 bytes to keep alignment;
     * alignment on long boundaries is faster in FastRAM
     */
    newlen = (newlen + 3) & ~3;

    /*
     * Create a memory descriptor for the freed portion of memory.
     */
    f = xmgetmd();
    if (!f) { ... return -1; }

    f->m_start = m->m_start + newlen;
    f->m_length = m->m_length - newlen;

    /*
     * Add it to the free list.
     */
    for (p = mp->mp_mfl, q = NULL; p; q = p, p = p->m_link)
        if (f->m_start <= p->m_start)
            break;
    f->m_link = p;
    if (q) q->m_link = f;
    else mp->mp_mfl = f;

    /*
     * Update existing memory descriptor.
     */
    m->m_length = newlen;

    return 0;
}
```

- [ ] **Step 2: Apply the fix, keeping the rounding**

Change `MD *f, *p, *q;` to `MD *f;`. Keep the rounding block (`newlen = (newlen + 3) & ~3;`) untouched. Replace the "Add it to the free list" insertion block with pushing onto the allocated list, and call `freeit()` after updating `m->m_length`:

```c
WORD shrinkit(MD *m, MPB *mp, LONG newlen)
{
    MD *f;
    /*
     * round the size up to a multiple of 4 bytes to keep alignment;
     * alignment on long boundaries is faster in FastRAM
     */
    newlen = (newlen + 3) & ~3;

    /*
     * Create a memory descriptor for the freed portion of memory.
     */
    f = xmgetmd();
    if (!f)
    {
        KDEBUG(("BDOS shrinkit: not enough OS memory for new MD\n"));
        return -1;
    }

    f->m_start = m->m_start + newlen;
    f->m_length = m->m_length - newlen;

    /*
     * Add it to the allocated list.
     */
    f->m_link = mp->mp_mal;
    mp->mp_mal = f;

    /*
     * Update existing memory descriptor.
     */
    m->m_length = newlen;

    /*
     * Free new memory descriptor via freeit() which takes care of
     * coalescing free blocks (important!).
     */
    freeit(f, mp);

    return 0;
}
```

(`freeit(MD *m, MPB *mp)` already exists in this file, `bdos/iumem.c:147`. It removes `m` from the allocated list and places it on the free list with neighbor coalescing — exactly what upstream's `e65ae149` relies on. Verify with `grep -n "void freeit" bdos/iumem.c`.)

- [ ] **Step 3: Verify**

```sh
make gitready
make atari512_defconfig && make
make rpi2_defconfig && make
```

- [ ] **Step 4: Commit**

```
shrinkit: coalesce the freed portion through freeit()

Ports upstream EmuTOS e65ae149: Mshrink() called repeatedly on the same
block could leave two or more adjacent MDs describing adjacent free memory
on the free chain, because the freed portion was inserted into the free
list directly, bypassing coalescing. Place the freed MD on the allocated
list and call freeit() so it is coalesced with its neighbours.

pTOS adaptation: keep the FastRAM 4-byte alignment rounding of `newlen`
(pTOS-specific, not in upstream); only the list-management block changes.

Refs #109
```
`git add bdos/iumem.c && git commit && git push`.

---

### Task 8: xclose() — fix the off-by-one standard-handle range test (`d2d08811`)

**Files:**
- Modify: `bdos/fsopnclo.c` (function `xclose`, currently lines 403–409)

**Resolved (no rewrite needed):** pTOS's `xclose()` standard-handle branch has the **same structure** as upstream's pre-fix code. The fix maps 1:1 — change `if (h <= 0)` to `if (h < 0)` AND add a guard against a standard handle Fforce'd to another standard handle.

- [ ] **Step 1: Re-read the branch**

```sh
sed -n '391,430p' bdos/fsopnclo.c
```
Current:
```c
    if ((h0 = h) < NUMSTD)
    {
        h = run->p_uft[h];
        run->p_uft[h0] = get_default_handle(h0);    /* revert to default */
        if (h <= 0)                 /* M01.01.1023.01 */
            return E_OK;
    }
    else if (((long) sft[h-NUMSTD].f_ofd) < 0L)
    {
        ...
    }
```

- [ ] **Step 2: Change the test and add the standard-handle guard**

```c
    if ((h0 = h) < NUMSTD)
    {
        h = run->p_uft[h];
        run->p_uft[h0] = get_default_handle(h0);    /* revert to default */
        if (h < 0)                  /* M01.01.1023.01 */
            return E_OK;
        if (h < NUMSTD)             /* "can't happen" (bug in Fforce()?) */
            return EIHNDL;
    }
    else if (((long) sft[h-NUMSTD].f_ofd) < 0L)
    {
        ...
    }
```

(The bug: `if (h <= 0)` treated "Fforce'd to stdin (h == 0)" as "mapped to a character device, done", short-circuiting the close. Handle 0 is a valid standard handle. The new `if (h < 0)` keeps "mapped to a character device" returning `E_OK`, while `h < NUMSTD` catches a standard handle Fforce'd to *another* standard handle — illegal, returns `EIHNDL` — and only `h >= NUMSTD` falls through to close the non-standard handle below.)

- [ ] **Step 3: Verify**

```sh
make gitready
make atari512_defconfig && make
make rpi2_defconfig && make
```

- [ ] **Step 4: Commit**

```
xclose: fix off-by-one range test for standard handles

Ports upstream EmuTOS d2d08811: when closing a standard handle, the test
for "mapped to a character device" was `h <= 0` — handle 0 (stdin) is a
valid standard handle, so closing one Fforce'd to it was mis-handled.
Change to `h < 0`, and add a guard so a standard handle Fforce'd to
another standard handle returns EIHNDL.

pTOS adaptation: none — pTOS's xclose() standard-handle branch matches
upstream's pre-fix structure 1:1; the two-line fix applies directly
(the probe-reported conflict was context drift in the rest of the file,
not in the edited hunk).

Refs #109
```
`git add bdos/fsopnclo.c && git commit && git push`.

---

### Task 9a: Reorganise the OFD — introduce the DFD structure (`1c120131`)

**Files:**
- Modify: `bdos/fs.h` (introduce `DFD`, move 5 fields out of `OFD`)
- Modify: `bdos/fsdir.c` (`xmkdir`, `xgsdtof`, `xrename`, `makofd`)
- Modify: `bdos/fsdrive.c` (`log_media`)
- Modify: `bdos/fsfat.c` (`nextcl`)
- Modify: `bdos/fsio.c` (`addit`, `eof`, `xlseek`, `ixlseek`, `ixread`; plus `xrw`'s `p->o_fileln` references if any — verify)
- Modify: `bdos/fsopnclo.c` (`ixcreat`, `makopn`, `ixclose`, and any `o_flag`/`o_td`/`o_strtcl`/`o_fileln` reference)

**Why this is here:** prerequisite for Task 9b (`f73f452b`). Upstream 1c120131 "Reorganise the filesystem's OFD structure" committed one day before the lost-clusters fix, upstream itself described as "in preparation for a fix to a filesystem bug that can cause lost clusters". pTOS has no `DFD`, no `o_disk`, no `o_dfd` anywhere. Get the canonical diff with `git show 1c120131`. **The blast radius is verified contained to the 6 files above** (no `include/`/`bios/`/`aes/` file references the moved fields).

**What pTOS adds that must survive:** `makopn()` writes the start cluster and file length via `le2cpu16(f->f_clust)` and `le2cpu32(f->f_fileln)` — pTOS's endian conversions. Upstream's pre-fix `makopn` used `swpw(p->o_strtcl); swpl(p->o_fileln);`. When porting the `makopn` hunk, keep the `le2cpu16`/`le2cpu32` form, redirecting into the DFD.

- [ ] **Step 1: Apply the `fs.h` keystone**

In `bdos/fs.h`, **before** the `/* OFD - open file descriptor */` comment (currently ~line 113), insert the `DFD` typedef and `O_DIRTY` macro exactly as upstream:
```c
/*
 *  DFD - disk file data
 *
 *  this contains a copy of the data from the FCB on disk and is
 *  contained within the OFD.
 *
 *  a future change will ensure that if there are multiple opens
 *  for the same file, only one copy of the data is maintained, in
 *  the DFD in the first-opened OFD.
 */
typedef struct
{
    UWORD o_flag;       /* see below                            */
    WORD  o_usecnt;     /* count of open OFDs pointing here     */
                    /* the following 3 items must be as in FCB: */
    DOSTIME o_td;       /* creation time/date: little-endian!   */
    CLNO  o_strtcl;     /* starting cluster number              */
    long  o_fileln;     /* length of file in bytes              */
} DFD;

/*
 * bit usage in o_flag
 */
#define O_DIRTY     1   /* contents have changed, FCB on disk must be updated */
```

Then in `struct _ofd` (currently ~lines 119–140), make these field changes:
- **Remove** from the top of the struct: `UWORD o_flag;`, the `/* the following 3 items must be as in FCB: */` comment, `DOSTIME o_td;`, `CLNO o_strtcl;`, `long o_fileln;`.
- **Add** near the top (right after `OFD *o_link;`): `DFD   *o_dfd;       /*  link to DFD for this file           */`
- **Remove** `WORD o_usecnt;` ("use count for inherited files").
- **Add** at the bottom (after `UWORD o_mod;` and a blank line): `DFD   o_disk;       /* data to be synchronised with the disk*/`
- **Remove** the now-redundant standalone `O_DIRTY` comment-and-`#define` block (the original "O_DIRTY - Dirty Flag" doc block plus `#define O_DIRTY 1`) lower in `fs.h`, since `O_DIRTY` is now defined inside the DFD block above. Verify with `grep -n "O_DIRTY" bdos/fs.h` after the edit — expect exactly one definition.

Confirm the 64-byte `FOLDRnnn.PRG` OFD constraint comment stays (upstream kept it; the embedded `DFD o_disk` still fits — upstream verified this in production).

- [ ] **Step 2: Update `fsdir.c` call sites**

`git show 1c120131 -- bdos/fsdir.c` is the canonical reference. The transformations:
- **`xmkdir()`**: add `DFD *dfd;` local; replace `f0->o_td`→`f0->o_dfd->o_td`, `f0->o_strtcl`→`f0->o_dfd->o_strtcl`; for the `f->o_dirfil->o_td`/`o_strtcl` accesses, go through `f->o_dirfil->o_dfd`; the `f->o_flag |= O_DIRTY;` line becomes `f->o_disk.o_flag |= O_DIRTY;` (upstream comment: "must set flag in f, not f0!").
- **`xgsdtof()`**: add `DFD *dfd; ` local set to `f->o_dfd;`; replace `f->o_td.*` with `dfd->o_td.*`, and `f->o_flag |= O_DIRTY` with `dfd->o_flag |= O_DIRTY`.
- **`xrename()`**: add `DFD *dfd;` local; around the "copy the time/date/cluster/length to the OFD" section, replace `fd2->o_td.*`/`fd2->o_strtcl`/`fd2->o_fileln` with `dfd = fd2->o_dfd; dfd->o_td.*`/`dfd->o_strtcl`/`dfd->o_fileln`, **and add `dfd->o_usecnt++;`**. Lower in `xrename`, `fd2->o_fileln = DIR_FILE_LENGTH` → `dfd->o_fileln = DIR_FILE_LENGTH`; `fdparent->o_strtcl` → `fdparent->o_dfd->o_strtcl`; `fd2->o_flag |= O_DIRTY` → `dfd->o_flag |= O_DIRTY`.
  - **Ordering note:** Task 2 already added the `FA_RO` guard higher up in `xrename()`. The Task 9a hunks are further *down* in the function and do not overlap. Verify by re-reading the function before editing.
- **`makofd()`**: add `DFD *dfd;`; set `dfd = &f->o_disk; f->o_dfd = dfd;` early; replace `f->o_strtcl = p->d_strtcl;`, `f->o_fileln = DIR_FILE_LENGTH;`, `f->o_td.date/time = p->d_td.*` with `dfd->...` assignments; set `dfd->o_usecnt = 1;`. Keep `f->o_dirfil`, `f->o_dnode`, `f->o_dirbyt`, `f->o_dmd` untouched.

- [ ] **Step 3: Update `fsdrive.c` `log_media()`**

`git show 1c120131 -- bdos/fsdrive.c`. The transformations:
- Add `DFD *dfd;` local.
- For the root-dir OFD `f`: set `f->o_dfd = dfd = &f->o_disk;`; replace `f->o_fileln = n * rsiz;` → `dfd->o_fileln = n * rsiz;`; replace `d->d_strtcl = f->o_strtcl = 2;` → `d->d_strtcl = dfd->o_strtcl = 2;`.
- For the FAT OFD `fo`: set `fo->o_dfd = dfd = &fo->o_disk;` (reuse `dfd`); replace `fo->o_strtcl = 2;` and the later `fo->o_fileln = fs * rsiz;` with `dfd->o_strtcl = 2;` and `dfd->o_fileln = fs * rsiz;`. (Upstream folds the trailing `fo->o_fileln = fs * rsiz;` up into the `dfd` block.)

- [ ] **Step 4: Update `fsfat.c` `nextcl()`**

`git show 1c120131 -- bdos/fsfat.c`. Add `DFD *dfd = p->o_dfd;` at the top of `nextcl`; replace `p->o_strtcl` → `dfd->o_strtcl` (the `cl2 = (p->o_strtcl ? p->o_strtcl : ENDOFCHAIN)` and the `p->o_strtcl = cl2; p->o_flag |= O_DIRTY;` branches) with `dfd->o_strtcl` / `dfd->o_flag`.

- [ ] **Step 5: Update `fsio.c` sites**

`git show 1c120131 -- bdos/fsio.c`. The transformations:
- **`addit()`**: add `DFD *dfd = p->o_dfd;`; replace `p->o_fileln` and `p->o_flag` with `dfd->o_fileln` and `dfd->o_flag`.
- **`eof()`**: `f->o_fileln` → `f->o_dfd->o_fileln`.
- **`xlseek()`**: `n += f->o_fileln` → `n += f->o_dfd->o_fileln`.
- **`ixlseek()`**: add `DFD *dfd = p->o_dfd;`; replace the two `p->o_fileln` (range check) and the `p->o_strtcl` ("start at the beginning" cluster) with `dfd->o_fileln` / `dfd->o_strtcl`.
- **`ixread()`**: `p->o_fileln - p->o_bytnum` → `p->o_dfd->o_fileln - p->o_bytnum`.
- **`xrw()` (Task 5 restructured function)**: `grep -n "o_fileln\|o_strtcl\|o_td\|o_flag\|o_usecnt" bdos/fsio.c` after the named-site changes and fix any remaining references inside `xrw()` (it may reference `p->o_fileln` for `addit`/eof-like checks — confirm none remain).

- [ ] **Step 6: Update `fsopnclo.c` sites**

`git show 1c120131 -- bdos/fsopnclo.c`. The transformations:
- **`ixcreat()`**: `getofd(f2)->o_flag |= O_DIRTY;` → `getofd(f2)->o_dfd->o_flag |= O_DIRTY;`.
- **`makopn()`** — **this is the pTOS-reconciliation site**. Add `DFD *dfd;` local. Drop the `p->o_usecnt = 0;` line (replace with the comment upstream added: `/* the following 2 assignments are unnecessary, since MGET zeroes the OFD */`). After `dn->d_files = p;`, add:
  ```c
      dfd = &p->o_disk;           /* for now, always use our own DFD */
      p->o_dfd = dfd;
      dfd->o_usecnt = 1;
  ```
  For the `if (p2)` "steal time/date" branch, redirect the `memcpy` to the DFD: `memcpy(&dfd->o_td,&p2->o_dfd->o_td,sizeof(DOSTIME)+sizeof(CLNO)+sizeof(long));`. For the `else` branch, **keep pTOS's `le2cpu16`/`le2cpu32`**, redirected to the DFD:
  ```c
      else
      {
          dfd->o_td.date = f->f_td.date;    /* note: OFD time/date are  */
          dfd->o_td.time = f->f_td.time;    /*  actually little-endian! */
          dfd->o_strtcl = le2cpu16(f->f_clust);  /* 1st cluster of file */
          dfd->o_fileln = le2cpu32(f->f_fileln);  /* init length of file */
      }
  ```
  (Do NOT introduce `swpw(dfd->o_strtcl); swpl(dfd->o_fileln);` — that is upstream's form; pTOS uses the explicit `le2cpu16/32` conversions, which must survive.)
- **`ixclose()`**: add `DFD *dfd = fd->o_dfd;` local; replace `fd->o_flag` (the `O_DIRTY` test, the clearing) with `dfd->o_flag`; replace the `memcpy(&fcb->f_td,&fd->o_td,10)` with `memcpy(&fcb->f_td,&dfd->o_td,10)`.

- [ ] **Step 7: Whole-tree sweep for stragglers**

```sh
grep -rnE "\->o_flag|\->o_td|\->o_strtcl|\->o_fileln|\->o_usecnt" bdos/ include/ bios/
grep -rn "O_DIRTY" bdos/ include/ bios/ | grep define
```
After the edits, `o_flag`/`o_td`/`o_strtcl`/`o_fileln` should only appear as `o_dfd->...` or `o_disk....` references; `O_DIRTY` should have exactly one `#define`. (Blast radius is verified contained, so no external sites should surface.)

- [ ] **Step 8: Verify**

```sh
make gitready
make atari512_defconfig && make
make rpi2_defconfig && make
```
If a reference was missed, the build fails with "no member named `o_fileln`" (or `o_td`/`o_strtcl`/`o_flag`/`o_usecnt`); fix the missed site, rebuild.

- [ ] **Step 9: Commit**

```
fs: reorganise the OFD with an embedded DFD structure

Ports upstream EmuTOS 1c120131: introduce a DFD ("disk file data")
structure *within* the OFD holding the FCB-mirror fields (o_flag,
o_usecnt, o_td, o_strtcl, o_fileln), and an o_dfd pointer the code
indirects through. Preparation for the lost-clusters fix (next commit),
as upstream's commit message states.

pTOS adaptation: in makopn() keep the le2cpu16()/le2cpu32() endian
conversions for strtcl and fileln (upstream's pre-fix code used
swpw()/swpl() — pTOS uses the explicit little-endian conversions), now
writing into the DFD. The 64-byte OFD FOLDRnnn.PRG constraint is
unchanged. Blast radius verified to the 6 files upstream touched — no
bios/ aes/ or include/ reference the moved fields.

Refs #109
```
`git add -A && git commit && git push`.

---

### Task 9b: Share the DFD across OFDs of the same file — fix lost clusters (`f73f452b`)

**Files:**
- Modify: `bdos/fs.h` (the DFD doc comment — wording change only)
- Modify: `bdos/fsopnclo.c` (`makopn` — share `o_dfd`; `sftdel` — refcount the base OFD)

**Why this builds on 9a:** `f73f452b` assumes `DFD`, `o_dfd`, `o_disk`, and `o_usecnt`-in-DFD all exist (now in place from 9a). The fix: a file opened multiple times shares a *single* DFD via `o_dfd`; the first-opened OFD is the "base", holding the embedded `o_disk`; later opens point `o_dfd` at the base's DFD and bump `o_usecnt`. On close, decrement; free the non-base OFD immediately, and free the base OFD only when the count hits zero. This is exactly the lost-cluster bug: each handle acquired free clusters independently, and the chain written to disk reflected only the last handle closed — sharing one DFD keeps a single authoritative copy of strtcl/fileln so cluster allocation is visible to all handles.

- [ ] **Step 1: Update the DFD doc comment in `fs.h`**

`git show f73f452b -- bdos/fs.h`. Replace the "a future change will ensure that ..." paragraph (just added in Task 9a) with the post-fix wording:
```c
/*
 *  DFD - disk file data
 *
 *  this contains a copy of the data from the FCB on disk and is
 *  contained within the OFD.
 *
 *  note: only one copy of the data is maintained in memory, in the
 *  DFD in the first-opened OFD for a given file (the 'base OFD').
 *  until all OFDs for that file are closed, other OFDs access this
 *  data via the o_dfd pointer, which always points to the DFD in
 *  the 'base OFD'.
 */
```

- [ ] **Step 2: Rewrite `makopn()`'s DFD setup to share**

`git show f73f452b -- bdos/fsopnclo.c` is the canonical reference. In `makopn()`, the block Task 9a added:
```c
    dfd = &p->o_disk;           /* for now, always use our own DFD */
    p->o_dfd = dfd;
    dfd->o_usecnt = 1;

    if (p2)
    {       /* steal time/date,startcl,fileln (a bit clumsily) */
        memcpy(&dfd->o_td,&p2->o_dfd->o_td,sizeof(DOSTIME)+sizeof(CLNO)+sizeof(long));
        /* not used yet... TBA *********/
        p2->o_thread = p;
    }
    else
    {
        dfd->o_td.date = f->f_td.date;    /* note: OFD time/date are  */
        dfd->o_td.time = f->f_td.time;    /*  actually little-endian! */
        dfd->o_strtcl = le2cpu16(f->f_clust);     /* 1st cluster of file */
        dfd->o_fileln = le2cpu32(f->f_fileln);    /* init length of file */
    }
```
is restructured to:
```c
    /*
     * if this file is already open, we copy the DFD pointer; this
     * ensures that that all OFDs for the same file use the same DFD.
     * otherwise, we use the DFD in the current OFD.
     */
    if (p2)
    {
        dfd = p2->o_dfd;
        dfd->o_usecnt++;                /* more than one user of DFD! */
        /* not used yet... TBA *********/
        p2->o_thread = p;
    }
    else
    {
        dfd = &p->o_disk;
        dfd->o_usecnt = 1;              /* only OFD using this DFD */
        dfd->o_td.date = f->f_td.date;  /* note: OFD time/date are  */
        dfd->o_td.time = f->f_td.time;  /*  actually little-endian! */
        dfd->o_strtcl = le2cpu16(f->f_clust);     /* 1st cluster of file */
        dfd->o_fileln = le2cpu32(f->f_fileln);    /* init length of file */
    }

    p->o_dfd = dfd;                     /* for future reference ... */
```
(Still keep pTOS's `le2cpu16`/`le2cpu32`, not `swpw`/`swpl`. The key behavioural change: when the file is already open (`p2` non-NULL), the new OFD's `o_dfd` now points at `p2->o_dfd` (the base's DFD) instead of copying the data into its own `o_disk`. `o_usecnt` reflects the count.)

- [ ] **Step 3: Refcount-aware `sftdel()`**

In `sftdel()` (currently ~line 360), the post-9a body is:
```c
static void sftdel(FTAB *sftp)
{
    FTAB *s;
    OFD *ofd;

    ofd = (s=sftp)->f_ofd;
    s->f_ofd = 0;
    s->f_own = 0;
    s->f_use = 0;

    if (sftofdsrch(ofd) == NULL)
        xmfreblk((int *)ofd);
}
```
Port `f73f452b`'s `sftdel()` body (add `DFD *d;` local; decrement; free non-base OFD now, free base OFD when count hits zero):
```c
static void sftdel(FTAB *sftp)
{
    FTAB *s;
    OFD *ofd;
    DFD *d;

    ofd = (s=sftp)->f_ofd;

    s->f_ofd = 0;
    s->f_own = 0;
    s->f_use = 0;

    /*
     * if there are no other sft entries with same OFD, delete the OFD
     * (subject to the complication of multiple OFDs pointing to the same file)
     */
    if (sftofdsrch(ofd) == NULL)
    {
        d = ofd->o_dfd;
        if (d->o_usecnt > 0)        /* paranoia */
            d->o_usecnt--;

        if (d != &ofd->o_disk)      /* not the 'base OFD', */
            xmfreblk(ofd);          /*  so OK to delete it */

        if (d->o_usecnt == 0)       /* no more users of this file */
        {
            ofd = (OFD *)((char *)d - offsetof(OFD, o_disk));
            xmfreblk(ofd);          /* delete the 'base OFD' */
        }
    }
}
```
(`offsetof` needs `<stddef.h>`; pTOS freestanding headers provide it — confirm `grep -rn "offsetof" bdos/ include/ | head` shows it already used elsewhere in the tree, or the build's include path supplies it. Upstream uses `offsetof` here, so the build supports it.)

- [ ] **Step 4: Verify**

```sh
make gitready
make atari512_defconfig && make
make rpi2_defconfig && make
```

- [ ] **Step 5: Commit**

```
fs: share the DFD across OFDs of the same file (fix lost clusters)

Ports upstream EmuTOS f73f452b: with multiple handles concurrently open
for writing to the same file, each handle acquired free clusters
independently and the on-disk cluster chain reflected only the last
handle closed — clusters were lost. Fix by sharing a single DFD through
o_dfd across all OFDs of a file: the first-opened OFD is the 'base'
holding the embedded o_disk; later opens point o_dfd at the base's DFD
and bump o_usecnt. sftdel() decrements, freeing the non-base OFD at once
and the base OFD only when the count hits zero.

pTOS adaptation: in makopn() the existing-file branch keeps pTOS's
le2cpu16()/le2cpu32() endian conversions (already redirected into the
DFD by the previous commit) for the base-OFD case. Builds on the
1c120131 restructure landed in the previous commit.

Refs #109
```
`git add -A && git commit && git push`.

---

### Task 10: Final verification — Hatari STE smoke + optional QEMU

**Files:** none.

This task exercises the GEMDOS paths the PR changed: file open/close/rename/create (`fsopnclo.c`, `fsdir.c`), program load (`proc.c`), memory shrink (`iumem.c`), cluster allocation (`fsio.c`, `fsfat.c`). Load the ptos-smoketest skill for the verified invocations.

- [ ] **Step 1: Build the m68k image for the smoke test**

```sh
make atari512_defconfig && make
```
Confirm `ptos512k.img is ready`.

- [ ] **Step 2: Hatari STE boot test**

```sh
hatari --tos ptos512k.img --machine ste --memsize 4 --sound off \
  --avirecord --avi-vcodec png --avi-file /tmp/boot109.avi --run-vbls 1200
```
Pass signal (from the skill): the GEM desktop renders — EmuTOS default background `IP_4PATT` (white+green 2×2 checkerboard) plus floppy/hard-drive icons. Analyze the last AVI frame with the skill's PIL snippet (extract PNG frames, count green checkerboard pixels: `> 1000 ⇒ desktop ⇒ booted`). **Do NOT use Hatari debugger breakpoints** (unreliable per the skill).

- [ ] **Step 3: Optional — QEMU raspi2 boot (ARM)**

```sh
make rpi2_defconfig && make
timeout 30 qemu-system-arm -M raspi2b -bios kernel7.img -d guest_errors -D /tmp/q109.log -serial stdio
```
Pass signal: `AES: EMUDESK: appl_init()` then `AES: EMUDESK: evnt_multi()` on serial; `guest_errors` log empty except the benign `bcm2835_systmr_write: read-only ofs 0x4`.

- [ ] **Step 4: Restore the user's preferred `.config`**

The per-commit loop repeatedly ran `make atari512_defconfig` / `make rpi2_defconfig`. Restore whatever `.config` the user had before this session (if `make distclean` would lose something wanted, ask). `.config` is untracked (gitignored), so nothing in the PR is affected.

- [ ] **Step 5: Mark the PR ready for review**

```sh
gh pr ready 130
```
(Per CLAUDE.md, the user merges the PR themselves. Do not merge.)

If the Hatari boot regresses across the batch, bisect with `git bisect` across the per-fix commits (they are granular precisely to make this possible); `git revert <hash>` the breaking fix, rebuild, retest.

---

## Plan self-review (writer's checklist)

**Spec coverage:**
- Spec fix 1 (`323ee921`) → Task 1. ✓
- Spec fix 2 (`96165134`) → Task 2. ✓
- Spec fix 3 (`05797930`, skip `1e6758ef`) → Task 3. ✓
- Spec fix 4 (`777991d4`) → Task 4. ✓
- Spec fix 5 (`d4757152`) → Task 5 (verify-then-decide, per the restructure finding). ✓
- Spec fix 6 (`8e04469b`) → Task 6, narrowed to `proc.c`-only 3-site fix. ✓
- Spec fix 7 (`e65ae149`) → Task 7, alignment preserved. ✓
- Spec fix 8 (`d2d08811`) → Task 8, 1:1 mapping. ✓
- Spec fix 9a (`1c120131`) → Task 9a. ✓
- Spec fix 9b (`f73f452b`) → Task 9b. ✓
- Spec verification → Task 10. ✓

**Placeholder scan:** No "TBD"/"fill in"/"similar to" — each task cites the canonical upstream commit (`git show <hash>`), names the pTOS file:function:line, gives the before→after code, and states the pTOS reconciliation explicitly. Task 5's verify-then-decide is intentional, not a placeholder; its "skip" branch gives exact decision text.

**Type/name consistency:** `DFD`, `o_dfd`, `o_disk`, `o_usecnt` (now in DFD, moved out of OFD) used consistently across 9a→9b. `first_time` is `BOOL` (Task 5 conditional). `xclose(fh)` matches `fh` declared `FH` in `xexec()`. `o_flag`/`o_td`/`o_strtcl`/`o_fileln` consistently accessed as `o_dfd->...` post-9a. `offsetof(OFD, o_disk)` matches the embedded field name added in 9a.

**Open risks carried into plan:**
- Task 5 (d4757152) may resolve as "skip" if pTOS's restructured `xrw()` does not reproduce the bug — the plan makes this a first-class, actionable decision rather than a silent guess.
- Task 9a's restructure is the largest blast radius; the plan provides the keystone (`fs.h`) verbatim, a per-site transformation list, a final whole-tree `grep` sweep to catch stragglers, and the build-error fallback ("no member named `o_fileln`").
- Task 9b's `offsetof` usage assumes the freestanding header provides it; the plan includes a verification grep before trusting it.