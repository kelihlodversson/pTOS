#!/usr/bin/env python3
#
# check-duplicate-configs.py - fail when two defconfigs make the same .config
#
# Usage: tools/check-duplicate-configs.py      (from the top of the tree)
#
# Every configuration in configs/ is resolved the way "make NAME_defconfig"
# does, and the resulting .config files are compared.  Two that are the same,
# option for option, build the same thing: one of them is a duplicate, which
# CI builds for nothing (#989).  Run by tools/check-ci-configs.sh; needs
# kconfiglib.
#

import glob
import os
import sys
import tempfile

import kconfiglib

os.environ["CONFIG_"] = ""           # the symbols have no prefix (tools/kconfig.mk)
kconf = kconfiglib.Kconfig("Kconfig", warn=False)

seen = {}
with tempfile.TemporaryDirectory() as tmp:
    out = os.path.join(tmp, "config")
    for path in sorted(glob.glob("configs/*_defconfig")):
        name = os.path.basename(path)[:-len("_defconfig")]
        kconf.load_config(path)
        kconf.write_config(out, header="")
        with open(out) as f:
            seen.setdefault(f.read(), []).append(name)

status = 0
for names in seen.values():
    if len(names) > 1:
        print("error: these configurations make the same .config: "
              + ", ".join(names) + " (keep one of them)", file=sys.stderr)
        status = 1
if status == 0:
    print("No two configurations make the same .config.")
sys.exit(status)
