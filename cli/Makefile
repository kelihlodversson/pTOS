#
# Makefile for EmuCON2 (standalone build)
#
# Copyright (C) 2013-2017 The EmuTOS development team.
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.
#
# Builds emucon2.tos, a portable command processor. Deliberately
# self-contained (its own compiler/flags, no dependency on the top-level
# Makefile or its Kconfig-driven configuration) so it stays
# "cd cli && make"-buildable on its own.
#
# ARCH selects which emucon2.tos gets built -- "make" (or explicitly
# "make ARCH=m68k") builds one that runs under ANY TOS, not just this
# OS's own ROM build; "make ARCH=arm" builds one that runs under pTOS's
# own ARM ports (raspi/virt-arm) specifically, since ARM has no other
# TOS to be portable against -- pTOS invented that whole ABI itself (see
# lib/libcmini/include/mint/arch/arm/osbind.h). Only one architecture is
# built per invocation, matching how the top-level Makefile's own
# Kconfig-driven .config always targets exactly one machine at a time.
#

include ../version.mk

TARGET = emucon2.tos
ARCH ?= m68k

# libcmini provides the C library, GEMDOS/BIOS/XBIOS bindings, and startup
# code (crt0) for this standalone program, replacing the private trap
# wrappers and _start it used to carry in cmdasm.S -- see cmd.h and the
# regression test harness's identical pattern in the top-level Makefile.
LIBCMINI_DIR      = ../lib/libcmini
LIBCMINI_INC      = $(LIBCMINI_DIR)/include
# Per-ARCH build directory: switching ARCH without "make clean" must not
# silently relink against a libcmini.a built for the other architecture.
# The top-level Makefile solves the same problem with a staleness-stamp
# file instead (LIBCMINI_STAMP); a separate directory per ARCH is enough
# here since this Makefile only ever builds one ARCH per invocation, so
# there's nothing to compare against -- just two possible locations.
LIBCMINI_BUILDDIR = $(abspath libcmini-build-$(ARCH))
LIBCMINI_MAKE     = $(MAKE) -C $(LIBCMINI_DIR) BUILDDIR=$(LIBCMINI_BUILDDIR)
LIBCMINI_OBJDIR   = $(LIBCMINI_BUILDDIR)/./objs
LIBCMINI_CRT0     = $(LIBCMINI_OBJDIR)/minicrt0.o
LIBCMINI_LIB      = $(LIBCMINI_BUILDDIR)/./libcmini.a

ifeq ($(ARCH),arm)
CC = arm-none-eabi-gcc
LIBCMINI_DEFCONFIG = ptos_arm_defconfig
# Matches libcmini's own ARM_ABI_CFLAGS (lib/libcmini/Makefile): the
# baseline FPU every Raspberry Pi target's own (vfp/neon-vfpv4/
# neon-fp-armv8) is call-ABI-compatible with, so the two link correctly
# regardless of which Pi this eventually runs on. No -march override,
# for the same reason: the toolchain's own default is what libcmini
# itself was built with.
ARCH_CFLAGS = -mfloat-abi=hard -mfpu=vfp
# ARM programs run under pTOS's own ELF loader (bios/elfld.c), which
# needs a relocatable image, not stripped -nostdlib a.out -- matches the
# top-level Makefile's runtests.tos link line (TEST_LDFLAGS).
LDFLAGS = -Wl,-q -Wl,-Ttext=0 -Wl,-e_start
# cmdasm.S is m68k assembly (the Line-A trap, which doesn't exist on
# ARM at all) -- cmdgetwh.c's STANDALONE_CONSOLE branch provides
# getwh()/getht() instead, via Ssystem(S_CONSOLE_DIM, ...).
OBJECTS = cmdmain.o cmdedit.o cmdexec.o cmdint.o cmdparse.o cmdutil.o cmdgetwh.o version.o
else
CC = m68k-atari-mintelf-gcc
LIBCMINI_DEFCONFIG = mintelf_defconfig
# Must not add -mshort: libcmini's own mintelf_defconfig build uses the
# toolchain's default (non -mshort) multilib, so this program has to
# match it or every call across the boundary into libcmini (strlen(),
# Bconin(), ...) would use a mismatched calling convention for plain int.
ARCH_CFLAGS = -Wa,--register-prefix-optional
LDFLAGS = -s
OBJECTS = cmdasm.o cmdmain.o cmdedit.o cmdexec.o cmdint.o cmdparse.o cmdutil.o version.o
endif

# LIBCMINI_INC first: ../include carries pTOS's own string.h (needed only
# for the couple of headers libcmini doesn't have, e.g. version.h,
# scancode.h), which would otherwise shadow libcmini's <string.h> since
# both provide one.
CPPFLAGS = -I$(LIBCMINI_INC) -I../include -DUSE_STATIC_INLINES=0 -DSTANDALONE_CONSOLE
# -fno-tree-loop-distribute-patterns: matches the kernel's own OTHERFLAGS
# (see ../Makefile) for the same reason -- GCC can silently rewrite an
# ordinary loop (e.g. cmdutil.c's convulong()) into a call to memset()/
# memcpy()/memmove() the source never actually wrote. Found the hard way
# on ARM (undefined references at link time for exactly those functions,
# from code with no explicit call to any of them) before libcmini grew
# ARM implementations of them; kept here even now that it has, so this
# doesn't silently start depending on the optimizer's whims again the
# next time a loop shape changes.
CFLAGS = $(CPPFLAGS) -Os -Wall -Wundef -Wold-style-definition -fno-tree-loop-distribute-patterns $(ARCH_CFLAGS)

.PHONY: default all clean

default: $(TARGET)
all: default

HEADERS = cmd.h

# Order-only on $(LIBCMINI_DIR)/Makefile: the .c/.S sources #include
# libcmini headers (via cmd.h), so without this, an uninitialized submodule
# fails compilation with a raw "No such file" from the compiler instead of
# the friendly message below -- e.g. under "make -j", where object
# compilation can otherwise race ahead of the libcmini bootstrap chain.
%.o: %.c $(HEADERS) | $(LIBCMINI_DIR)/Makefile
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S $(HEADERS) | $(LIBCMINI_DIR)/Makefile
	$(CC) $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

# lib/libcmini is a git submodule of the pTOS tree: a plain "git clone"
# without --recurse-submodules (or before "git submodule update --init")
# leaves it present but empty.
$(LIBCMINI_DIR)/Makefile:
	@echo "$@ not found -- run 'git submodule update --init' from the pTOS tree root first" >&2
	@exit 1

# Configure once per ARCH; "make clean" removes both libcmini-build-*
# directories to force a fresh configure+build. A real (not order-only)
# prerequisite on the defconfig file itself: without it, a submodule
# bump that changes configs/$(LIBCMINI_DEFCONFIG)'s content -- without
# touching any source file -- would leave an already-built .config/
# libcmini.a looking up to date to Make, silently linking emucon2.tos
# against a stale libcmini configuration.
$(LIBCMINI_BUILDDIR)/.config: $(LIBCMINI_DIR)/configs/$(LIBCMINI_DEFCONFIG) | $(LIBCMINI_DIR)/Makefile
	@$(LIBCMINI_MAKE) $(LIBCMINI_DEFCONFIG)

$(LIBCMINI_LIB): $(LIBCMINI_BUILDDIR)/.config $(wildcard $(LIBCMINI_DIR)/sources/*.c $(LIBCMINI_DIR)/sources/arch/$(ARCH)/*.S)
	@$(LIBCMINI_MAKE) libs startups

$(LIBCMINI_CRT0): $(LIBCMINI_LIB)

$(TARGET): $(LIBCMINI_CRT0) $(OBJECTS) $(LIBCMINI_LIB)
	$(CC) -nostartfiles -nostdlib $(LIBCMINI_CRT0) $(OBJECTS) -L$(dir $(LIBCMINI_LIB)) -lcmini -lgcc $(LDFLAGS) -o $@

clean:
	-rm -f *.o version.c
	-rm -f $(TARGET)
	-rm -rf libcmini-build-m68k libcmini-build-arm

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
