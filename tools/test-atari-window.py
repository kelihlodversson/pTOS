#!/usr/bin/env python3
"""Exercise the 68030 identity and kernel-window DMA address boundaries."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix="atari-window-") as temp:
    tmp = Path(temp)
    (tmp / "emutos.h").write_text("""\
#include <stdint.h>
typedef uint32_t ULONG;
typedef uint8_t UBYTE;
#define CONF_ATARI_HIGHER_HALF 1
#define CONF_WITH_PROCESS_ASPACES 0
#define KDEBUG(args) ((void)0)
""")
    (tmp / "atari_mmu.h").write_text("#define ATARI_WINDOW_VBASE 0x80000000UL\n")
    (tmp / "check.c").write_text("""\
#include <stdint.h>
#include <stdio.h>
uint32_t virt_to_phys(const void *);
int main(void)
{
    static const struct { uint32_t input, expected; } cases[] = {
        { 0x00001000, 0x00001000 }, { 0x1fffffff, 0x1fffffff },
        { 0x20000000, 0x20000000 }, { 0x40000000, 0x40000000 },
        { 0x7fffffff, 0x7fffffff }, { 0x80000000, 0x00000000 },
        { 0x82000000, 0x02000000 }, { 0xbfffffff, 0x3fffffff },
        { 0xc0000000, 0x00000000 }
    };
    unsigned i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint32_t actual = virt_to_phys((const void *)(uintptr_t)cases[i].input);
        if (actual != cases[i].expected) {
            fprintf(stderr, "virt_to_phys(%08x): got %08x, expected %08x\\n",
                    cases[i].input, actual, cases[i].expected);
            return 1;
        }
    }
    return 0;
}
""")
    cc = shlex.split(os.environ.get("HOSTCC", "cc"))
    subprocess.run(cc + ["-std=c99", "-Wall", "-Wextra", "-Werror", "-I", str(tmp),
                         str(root / "bios/atari_window.c"), str(tmp / "check.c"),
                         "-o", str(tmp / "check")], check=True)
    subprocess.run([str(tmp / "check")], check=True)
print("Atari higher-half DMA translation boundaries passed")
