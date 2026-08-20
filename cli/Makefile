#
# Makefile for EmuCON2 (standalone build)
#
# Copyright (C) 2013-2017 The EmuTOS development team.
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.
#
# Builds emucon2.tos, a portable command processor that can run under any
# TOS -- not just this OS's own ROM build. Deliberately self-contained
# (its own compiler/flags, no dependency on the top-level Makefile or its
# Kconfig-driven configuration) so it stays "cd cli && make"-buildable on
# its own.
#

include ../version.mk

TARGET = emucon2.tos
CC = m68k-atari-mintelf-gcc

# libcmini provides the C library, GEMDOS/BIOS/XBIOS bindings, and startup
# code (crt0) for this standalone program, replacing the private trap
# wrappers and _start it used to carry in cmdasm.S -- see cmd.h and the
# regression test harness's identical pattern in the top-level Makefile.
LIBCMINI_DIR      = ../lib/libcmini
LIBCMINI_INC      = $(LIBCMINI_DIR)/include
LIBCMINI_BUILDDIR = $(abspath libcmini-build)
LIBCMINI_MAKE     = $(MAKE) -C $(LIBCMINI_DIR) BUILDDIR=$(LIBCMINI_BUILDDIR)
LIBCMINI_OBJDIR   = $(LIBCMINI_BUILDDIR)/./objs
LIBCMINI_CRT0     = $(LIBCMINI_OBJDIR)/minicrt0.o
LIBCMINI_LIB      = $(LIBCMINI_BUILDDIR)/./libcmini.a

# Must not add -mshort: libcmini's own mintelf_defconfig build uses the
# toolchain's default (non -mshort) multilib, so this program has to match
# it or every call across the boundary into libcmini (strlen(), Bconin(),
# ...) would use a mismatched calling convention for plain int.
#
# LIBCMINI_INC first: ../include carries pTOS's own string.h (needed only
# for the couple of headers libcmini doesn't have, e.g. version.h,
# scancode.h), which would otherwise shadow libcmini's <string.h> since
# both provide one.
CPPFLAGS = -I$(LIBCMINI_INC) -I../include -DUSE_STATIC_INLINES=0 -DSTANDALONE_CONSOLE
# --register-prefix-optional: cmdasm.S (and libcmini's own m68k asm) write
# bare register names (a0, d0, sp), which mintelf's assembler otherwise
# rejects without a leading %.
CFLAGS = $(CPPFLAGS) -Os -Wall -Wundef -Wold-style-definition -Wa,--register-prefix-optional

.PHONY: default all clean

default: $(TARGET)
all: default

OBJECTS = cmdasm.o cmdmain.o cmdedit.o cmdexec.o cmdint.o cmdparse.o cmdutil.o version.o
HEADERS = cmd.h

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

# lib/libcmini is a git submodule of the pTOS tree: a plain "git clone"
# without --recurse-submodules (or before "git submodule update --init")
# leaves it present but empty.
$(LIBCMINI_DIR)/Makefile:
	@echo "$@ not found -- run 'git submodule update --init' from the pTOS tree root first" >&2
	@exit 1

# Configure once; "make clean" removes libcmini-build to force a fresh
# configure+build. Unlike the top-level Makefile's LIBCMINI_STAMP, this
# doesn't need to detect defconfig switches -- this Makefile only ever
# builds one architecture (m68k), so there is nothing to switch between.
$(LIBCMINI_BUILDDIR)/.config: | $(LIBCMINI_DIR)/Makefile
	@$(LIBCMINI_MAKE) mintelf_defconfig

$(LIBCMINI_LIB): $(LIBCMINI_BUILDDIR)/.config $(wildcard $(LIBCMINI_DIR)/sources/*.c $(LIBCMINI_DIR)/sources/arch/m68k/*.S)
	@$(LIBCMINI_MAKE) libs startups

$(LIBCMINI_CRT0): $(LIBCMINI_LIB)

$(TARGET): $(LIBCMINI_CRT0) $(OBJECTS) $(LIBCMINI_LIB)
	$(CC) -nostartfiles -nostdlib $(LIBCMINI_CRT0) $(OBJECTS) -L$(dir $(LIBCMINI_LIB)) -lcmini -lgcc -s -o $@

clean:
	-rm -f *.o version.c
	-rm -f $(TARGET)
	-rm -rf libcmini-build

#
# version string
#

# This temporary file is always generated
version2.c:
	@echo '/* Generated from Makefile */' > $@
	@echo 'const char version[] = "$(VERSION)";' >> $@

# If the official version file is different than the temporary one, update it
version.c: version2.c
	@if ! cmp -s $@ $< ; then \
	  echo '# Updating $@ with VERSION=$(VERSION)' ; \
	  cp $< $@ ; \
	fi ; \
	rm $<
