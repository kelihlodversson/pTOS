#!/usr/bin/env python3
#
# genconfig.py - turn a Kconfig configuration into build system inputs
#
# Copyright (C) 2025-2026 The pTOS development team.
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.
#
"""Generate the C header and the Makefile fragment describing a configuration.

Reads the Kconfig tree and the current configuration file (.config by default,
or whatever KCONFIG_CONFIG points at) and writes:

  * a Makefile fragment, included by the top level Makefile
  * a C header, included by every source file through include/config.h

They are written in that order, and always rewritten, so that the header is
never older than the fragment; the build system relies on it.

Two styles of C macro are emitted, because the sources use both:

  * symbols named MACHINE_* or TARGET_* are only defined when they are set,
    so that they can be tested with #ifdef / defined()
  * every other bool is always defined, to 1 or to 0, so that it can be
    tested with #if even when -Wundef is in effect

int and hex symbols are emitted with their value, and are left undefined when
they have none, which happens when they depend on a disabled option.

Symbols named BUILD_* only concern the build system and never reach the C
header.  Use that prefix for anything the compiler has no business seeing,
in particular when the obvious name would clash with a macro of the sources.
"""

import argparse
import os
import sys

try:
    import kconfiglib
except ImportError:
    sys.exit("genconfig.py: the kconfiglib Python module is required.\n"
             "               Install it with: pip3 install kconfiglib")

# Symbols using the "#ifdef" style described above.
DEFINED_STYLE_PREFIXES = ("MACHINE_", "TARGET_")

# Symbols that are of no interest to the C code.
BUILD_ONLY_PREFIXES = ("BUILD_",)

BANNER = """\
/*
 * %s - automatically generated, do not edit
 *
 * Generated from %s by tools/genconfig.py.
 * Run "make menuconfig" to change the configuration.
 */
"""

MAKE_BANNER = """\
#
# %s - automatically generated, do not edit
#
# Generated from %s by tools/genconfig.py.
# Run "make menuconfig" to change the configuration.
#
"""


def header_lines(kconf, path, config_name):
    yield BANNER % (os.path.basename(path), config_name)
    yield "\n#ifndef AUTOCONF_H\n#define AUTOCONF_H\n\n"

    for sym in kconf.unique_defined_syms:
        if sym.name.startswith(BUILD_ONLY_PREFIXES):
            continue
        if sym.type == kconfiglib.BOOL:
            if sym.str_value == "y":
                yield "#define %s 1\n" % sym.name
            elif not sym.name.startswith(DEFINED_STYLE_PREFIXES):
                yield "#define %s 0\n" % sym.name
        elif sym.type in (kconfiglib.INT, kconfiglib.HEX):
            if sym.str_value:
                yield "#define %s %s\n" % (sym.name, sym.str_value)
        elif sym.type == kconfiglib.STRING:
            if sym.str_value:
                yield '#define %s "%s"\n' % (sym.name,
                                             kconfiglib.escape(sym.str_value))

    yield "\n#endif /* AUTOCONF_H */\n"


def makefile_lines(kconf, path, config_name):
    yield MAKE_BANNER % (os.path.basename(path), config_name)

    for sym in kconf.unique_defined_syms:
        if sym.type == kconfiglib.BOOL:
            if sym.str_value == "y":
                yield "%s = y\n" % sym.name
            else:
                yield "# %s is not set\n" % sym.name
        elif sym.str_value:
            yield "%s = %s\n" % (sym.name, sym.str_value)


def write(path, lines):
    """Write lines to path, replacing it atomically.

    An interrupted run must not leave a truncated file behind: make includes
    obj/auto.conf, and would silently build from whatever half of it survived.
    """
    directory = os.path.dirname(path)
    if directory and not os.path.isdir(directory):
        os.makedirs(directory)

    tmp = path + ".tmp"
    try:
        with open(tmp, "w") as f:
            f.write("".join(lines))
        os.replace(tmp, path)
    except BaseException:
        if os.path.exists(tmp):
            os.remove(tmp)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kconfig", default="Kconfig",
                        help="top level Kconfig file (default: Kconfig)")
    parser.add_argument("--config",
                        default=os.environ.get("KCONFIG_CONFIG", ".config"),
                        help="input configuration file (default: .config)")
    parser.add_argument("--header", required=True,
                        help="output C header")
    parser.add_argument("--makefile", required=True,
                        help="output Makefile fragment")
    args = parser.parse_args()

    if not os.path.exists(args.config):
        sys.exit("genconfig.py: '%s' does not exist.\n"
                 "               Run \"make <name>_defconfig\" or "
                 "\"make menuconfig\" first." % args.config)

    kconf = kconfiglib.Kconfig(args.kconfig, warn_to_stderr=True)
    kconf.load_config(args.config)

    # The header must be written last; see the module docstring.
    write(args.makefile, makefile_lines(kconf, args.makefile, args.config))
    write(args.header, header_lines(kconf, args.header, args.config))

    print("# Configuration written to %s and %s"
          % (args.makefile, args.header))


if __name__ == "__main__":
    main()
