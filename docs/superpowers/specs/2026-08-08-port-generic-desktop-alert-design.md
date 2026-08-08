# Design: Port generic desktop alert refactor (#118)

Part of #111 (cherry-pick upstream EmuTOS desktop/AES features).

## Goal

Port upstream EmuTOS commit `47f05896` "Implement generic desktop alert
function" (Roger Burrows, 2019-07-23) into this fork, adapted to the fork's
divergences.

The commit replaces three desktop alert helpers with a single variadic one,
simplifying the code and saving ROM.

## Note on the issue text

Issue #118 says the change is "Self-contained in `aes/gemfmalt.c`". That is
wrong: the commit touches only `desk/` files and `aes/gemfmalt.c` is
untouched. Per the maintainer's decision, we port the actual commit in
`desk/`.

## Upstream change (before fork adaptation)

The three functions

- `fun_alert_merge(WORD defbut, WORD stnum, BYTE merge)`  (always built)
- `fun_alert_long(WORD defbut, WORD stnum, LONG merge)`   (#if CONF_WITH_FORMAT)
- `fun_alert_string(WORD defbut, WORD stnum, BYTE *merge)` (#if CONF_WITH_DESKTOP_SHORTCUTS in fork)

collapse into

```c
WORD fun_alert_merge(WORD defbut, WORD stnum, ...)
{
    va_list ap;
    _Static_assert(sizeof(void *) >= sizeof(long), "incompatible type sizes");

    va_start(ap, stnum);
    sprintf(G.g_work, desktop_str_addr(stnum), va_arg(ap, void *));
    va_end(ap);

    return form_alert(defbut, G.g_work);
}
```

plus `#include <stdarg.h>` and the header prototype

```c
WORD fun_alert_merge(WORD defbut, WORD stnum, ...);
```

Upstream call sites renamed `fun_alert_string`/`fun_alert_long` to
`fun_alert_merge` in `deskapp.c`, `deskdir.c` and `desksupp.c`.

Current upstream master still carries the variadic form, so this port tracks
upstream's final state.

## Fork divergences and how the port adapts

| Upstream | Fork | Port action |
| --- | --- | --- |
| `desktop_str_addr(stnum)` | `rsrc_gaddr_rom(R_STRING, stnum, (void **)&G.a_alert)` (no `desktop_str_addr` in fork) | keep fork's fetch inside the variadic body |
| `G.g_work` buffer | `G.g_1text` buffer | keep `G.g_1text` |
| `deskapp.c` retry loop calls `fun_alert_string(1, STCRTFIL, ...)` | fork `save_to_disk()` (deskapp.c:906) has no retry loop; uses `fun_alert(1, STSVINF)`/`fun_alert(1, STNOINF)` | no change |
| `deskdir.c` STDELFIL/STDELDIR/STOPFAIL/STCRTFIL `fun_alert_string` calls | fork has no such retry loops | no change |
| `desksupp.c` STFILENF/STTRINFO/STPRINFO `fun_alert_string` calls | fork lacks these call sites | no change |
| — | fork callers: deskdir.c:437 `fun_alert_merge(1, STDISKFU, pdst_file[0])`, deskdir.c:878 `fun_alert_merge(2, STDELDIS, psrc_path[0])`, desksupp.c:344 `fun_alert_string(1, STRMVLOC, ...)`, desksupp.c:1410 `fun_alert_long(2, STFMTINF, avail)` | deskdir.c callers unchanged (already call `fun_alert_merge`); desksupp.c:344/1410 renamed to `fun_alert_merge` |

## Configuration

The fork guards `fun_alert_string` under `CONF_WITH_DESKTOP_SHORTCUTS` and
`fun_alert_long` under `CONF_WITH_FORMAT`. Per the maintainer's decision we
port faithfully: both functions are deleted entirely and the variadic
`fun_alert_merge` is unconditional, matching upstream. The guards in
`deskfun.h`/`deskfun.c` disappear; the remaining `CONF_WITH_DESKTOP_SHORTCUTS`
and `CONF_WITH_FORMAT` options stay (they gate other code) and the renamed
callers remain inside their existing `#if` blocks. No Kconfig change.

## Files changed

- `desk/deskfun.c` — add `#include <stdarg.h>`; replace the three functions
  (deskfun.c:76-110) with the variadic `fun_alert_merge`, using
  `rsrc_gaddr_rom` + `G.g_1text`; drop the two `#if` guards.
- `desk/deskfun.h` — replace the three prototypes and both `#if` guards
  (deskfun.h:21-29) with the single variadic prototype.
- `desk/desksupp.c` — rename `fun_alert_string(1, STRMVLOC, ...)` at :344 to
  `fun_alert_merge(...)`; rename `fun_alert_long(2, STFMTINF, avail)` at :1410
  to `fun_alert_merge(...)`.

## Portability notes

- The `va_arg(ap, void *)` read is a deliberate upstream "kludge": on m68k
  `int` is 16 bits but `void *` is 32 bits, so a `%c` merge passes a promoted
  char while the callee reads a pointer-sized slot. This ships in upstream
  EmuTOS on real m68k hardware since 2019; on ARM both are 32-bit and clean.
  `_Static_assert(sizeof(void *) >= sizeof(long))` guards the `%ld` case.
- `_Static_assert` is already used in the fork (`aes/gemaplib.c:203`).
- Format strings (`desk/desk_rsc.c`): STDISKFU `%c`, STDELDIS `%c`, STRMVLOC
  `%s`, STFMTINF `%ld`; each passes the matching argument.

## Verification

- Build all five configs used for issue #138: atari512, rpi2, virt-arm,
  atari512-dispatch, rpi2-sparse. Check `obj/` object sets unchanged except
  expected.
- Hatari boot atari512 and atari512-dispatch; QEMU boot rpi2-sparse and
  virt-arm; confirm GEM desktop reachable (checkerboard counts), no new
  `guest_errors`.
- Confirm `fun_alert_string`/`fun_alert_long` no longer referenced anywhere.
- Build with `CONF_WITH_DESKTOP_SHORTCUTS` off and `CONF_WITH_FORMAT` on and
  vice versa to prove the merged function is unconditionally available.

## Out of scope

- Upstream's `deskapp.c`/`deskdir.c` retry-loop call sites (do not exist in
  the fork).
- Any change to `aes/gemfmalt.c` (unrelated to this commit).
- Behavior of `form_alert()` itself.
