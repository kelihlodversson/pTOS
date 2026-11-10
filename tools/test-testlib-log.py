#!/usr/bin/env python3
"""Production harness logging across fresh workers; host process/file model."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="ptos-testlib-log-") as directory:
    work = Path(directory)
    mint = work / "mint"
    mint.mkdir()
    (mint / "osbind.h").write_text("""
long Fcreate(const char *, int);
long Fopen(const char *, int);
long Fseek(long, short, int);
long Fwrite(short, long, const void *);
long Fclose(short);
void Cconws(const char *);
void Cconout(int);
void Pterm(int);
""")
    (mint / "basepage.h").write_text("""
typedef struct { char p_cmdlin[128]; } BASEPAGE;
extern BASEPAGE *_base;
""")
    # Only the freestanding harness object uses this header/architecture define
    # to select its managed-worker branch. No CPU context is modeled or tested.
    (work / "string.h").write_text(
        "int memcmp(const void *, const void *, __SIZE_TYPE__);\n")
    cc = shlex.split(os.environ.get("HOSTCC", "cc"))
    common = ["-std=gnu90", "-Wall", "-Wextra", "-Werror", "-Itests"]
    subprocess.run(cc + common + ["-I" + str(work), "-DTEST_LOG_FILE",
        "-DTEST_WITH_NATIVE_SCHED", "-DTEST_WITH_PCLONE", "-D__arm__",
        "-Dmain=ptest_harness_main", "-c", "tests/runtests.c",
        "-o", str(work / "harness.o")], cwd=ROOT, check=True)
    subprocess.run(cc + common + ["-I" + str(work), "-DTEST_LOG_FILE",
        "-c", "tests/testlib.c", "-o", str(work / "testlib.o")],
        cwd=ROOT, check=True)
    # Use real host string.h in the process/filesystem model.
    (work / "string.h").unlink()
    executable = work / "logging"
    subprocess.run(cc + common + ["-I" + str(work), "tests/testlib/logging.c",
        str(work / "harness.o"), str(work / "testlib.o"), "-o", str(executable)],
        cwd=ROOT, check=True)
    log = work / "RUNTESTS.LOG"
    log.write_text("stale previous run\n")
    for injected in (False, True, False):
        env = dict(os.environ)
        env.pop("INJECT_FAILURE", None)
        if injected:
            env["INJECT_FAILURE"] = "1"
        result = subprocess.run([str(executable)], cwd=work, env=env, check=False)
        assert result.returncode == (2 if injected else 0), result.returncode
        text = log.read_text()
        assert "stale previous run" not in text
        before = text.index("outer before... ")
        prefix = text.index("parent prefix before launch")
        worker = text.index("managed worker... ")
        after = text.index("outer after... ")
        summary = text.index("--- Summary: ")
        assert before < prefix < worker < after < summary, text
        assert text.count("--- Summary: ") == 1, text
        if injected:
            for diagnostic in ("outer failure diagnostic", "worker failure diagnostic",
                               "worker failure propagated to parent"):
                assert diagnostic in text, text
            assert "0 passed, 2 failed, 2 total" in text, text
            assert "Some tests FAILED." in text, text
        else:
            assert "failure diagnostic" not in text, text
            assert "2 passed, 0 failed, 2 total" in text, text
            assert "All tests passed." in text, text
print("Harness fresh-worker append, retained failure diagnostics and outer truncation: PASS")
