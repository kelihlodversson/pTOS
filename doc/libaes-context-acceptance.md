# Explicit AES context acceptance (#953)

The userspace refactor is complete. `AESGLOBAL` is caller-owned and authoritative;
its `ap_3resv` points at the private retained state. The table maps the acceptance
criteria to executable checks, rather than treating a successful build as proof
of lifetime or reentrancy. The existing toolchains on master remain in use;
packaged SDK migration #964 is on hold pending pTOS-toolchain#15.

| Contract | Evidence |
| --- | --- |
| One typed global layout: m68k is 30 bytes, native ARM/x32 is 32 bytes | Member-width, offset, size and alignment assertions in `include/aesglobal.h`, compiled by each architecture's normal AES consumers; `tests/cuis/header_compat.c` checks binding/header combinations. |
| Null context allocates; a supplied context is reused without lookup | `tests/cuis/app_context.c` counts allocations and session opens, tests explicit aliases, and injects allocation/session/label/tag failures. `tests/cuis/gem.c` exercises the real allocator and CUIS backend. |
| Public fields belong to each array; aliases share only private state | `tests/cuis/app_context.c` preserves distinct resource fields and `ap_private`; `tests/cuis/resources.c` and `gem_resources.c` select different resource images through aliases. The native context fixture checks independent tree selectors across reinitialization. |
| Distinct fresh contexts, with the current same-process kernel constraint explicit | Host `app_context.c` supplies distinct mock sessions to test private-state isolation, including two concurrent pthreads with synchronized enumeration calls. Real kernel `tests/cuis/session.c` checks repeated registration returns one session; native `gem.c` verifies two separate allocations receive that same session. This does not establish independent native AES identities. |
| Exit cleans up and frees once, clearing only the supplied slot | Host `app_context.c` injects close/resource/free failures and retries, verifies no duplicate close/reset/free, and retains the other independently initialized context. Native `gem.c` exits through an alias without changing GEMlib's default binding state. Invalid or dangling contexts are never reused. |
| Only `mt_*` AES exports; classic bindings and globals live in GEMlib | `tools/check-libaes-compatibility.py` classifies 91 exports and 80 legacy operations. `tools/check-libaes-elf.py` compares actual dynamic symbols, static GEMlib definitions, and client imports, including caller-owned binding data. |
| No library-owned mutable globals, TLS or shared active helper pointers | The ELF audit rejects allocated writable sections outside sealed GNU_RELRO and all TLS. `tools/test-libaes-state.py` checks data/BSS/TLS rejection fixtures. `tests/cuis/object_draw.c` tests nested USERDEF painters and form-error isolation; `format_context.c` tests nested formatter sinks. |
| Retained state and cleanup follow the supplied context | `window_api.c`, `window_desktop_api.c`, `graphics.c`, `resources.c`, `menu_items.c`, `shell.c` and lifecycle fixtures exercise distinct private state and explicit aliases. Their mocked session identities isolate userspace ownership from kernel policy. |
| Standard clients and classic AES remain supported | Native GEM/VDI/window/input/resource boot tests, EmuDesk acceptance and classic ARM/m68k builds remain CI gates. RCS and TypeShelf have separate emulator acceptance runners in their repositories. ARM/m68k external libaes ports are tracked in #898. |

## Kernel identity dependency

There is still one CUIS client per `PD`. Two fresh globals allocate two private
contexts, but receive the same kernel session and AES ID. Closing that session
invalidates both contexts' kernel access. The native integration fixture does
not invoke AES through either a freed alias or the remaining closed-session
context; process exit reclaims the latter allocation. It does not add an orphan
cleanup API, context registry, reference counting, TLS or stale-pointer guards.

Independent simultaneous GUI applications within one process require the
separate CUIS ownership work coordinated with #896/#907/#915. That work must
distinguish PROCESS/PID, THREAD/TID, ASPACE and GUI identity, authorize the
supplied endpoint explicitly, and retire only the appropriate endpoint on
thread exit. MagiC shell threads and Pclone remain outside this refactor.
The library is prepared to accept distinct native session handles; this PR
does not advertise or implement them. Native integration tests must be revised
when that kernel identity contract changes.

## Reproducing validation

Use `.claude/skills/toolchain-install/SKILL.md` and the current master's compiler
prefixes, not the parked SDK migration branch. Initialize the submodules first.

```sh
python3 tools/test-cuis.py
python3 tools/test-elf-shared.py
HOSTCC='cc -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie' \
    python3 tools/test-cuis.py
```

Build the combined native regression as in `.github/workflows/build.yml` and
run `tools/test-mmap-boot.sh`. It requires both the normal GEM lifecycle marker
and `x32 AES explicit contexts and shared CUIS boundary: PASS`, along with the
existing ELF/libaes/VDI/window/input/resource checks. Run the desktop acceptance
job for the ordinary applications. Classic `virt-arm` and `virt-m68k` builds
compile their real layout assertions and retain the kernel AES entry paths.

The PR records the actual local and CI results for its final commit. These
checks establish the explicit userspace contract; they do not substitute for
the future shared-process, distinct-TID GUI identity regression matrix.
