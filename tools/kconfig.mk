#
# kconfig.mk - Makefile fragment driving the configuration system
#
# Copyright (C) 2025-2026 The pTOS development team.
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.
#
# This provides the same configuration targets as the Linux kernel:
#
#   make <name>_defconfig  start from one of the configurations in configs/
#   make menuconfig        edit the configuration interactively
#   make savedefconfig     write a minimal defconfig for the current .config
#   make oldconfig         ask about the options added since the last run
#   make olddefconfig      accept the default for every new option
#
# The front ends come from kconfiglib, a pure Python implementation of the
# kconfig language:  pip3 install kconfiglib
#

PYTHON ?= python3

# The configuration file.  Override it to keep several configurations
# around, e.g. "make KCONFIG_CONFIG=rpi2.config menuconfig".
KCONFIG_CONFIG ?= .config
export KCONFIG_CONFIG

# Our Kconfig symbols are named exactly like the C macros and the Makefile
# variables they control, so kconfiglib must not add a prefix to them.
CONFIG_ :=
export CONFIG_

KCONFIG_TOP = Kconfig
KCONFIG_SOURCES = $(KCONFIG_TOP) $(wildcard Kconfig.*) $(wildcard */Kconfig)

DEFCONFIG_DIR = configs
DEFCONFIGS = $(sort $(notdir $(wildcard $(DEFCONFIG_DIR)/*_defconfig)))

# Generated configuration, in a form suitable for the C compiler and for
# make respectively.
AUTOCONF_H = obj/autoconf.h
AUTOCONF_MK = obj/auto.conf

GENCONFIG = $(PYTHON) tools/genconfig.py --kconfig $(KCONFIG_TOP) \
	      --config $(KCONFIG_CONFIG) --header $(AUTOCONF_H) \
	      --makefile $(AUTOCONF_MK)

# Targets that must work without a configuration.
CONFIG_TARGETS = $(DEFCONFIGS) menuconfig guiconfig oldconfig olddefconfig \
                 savedefconfig allnoconfig alldefconfig listnewconfig

#
# Generating the build configuration
#

# genconfig.py writes both files in one go, the makefile fragment first and
# the header last.  Two rules are needed nonetheless: make only restarts
# itself, and therefore only picks up the new settings, when the makefile
# fragment it includes is remade by a recipe of its own.
$(AUTOCONF_MK): $(KCONFIG_CONFIG) $(KCONFIG_SOURCES) tools/genconfig.py
	@mkdir -p $(dir $@)
	$(GENCONFIG)

$(AUTOCONF_H): $(AUTOCONF_MK)
	$(GENCONFIG)

# Never try to create the configuration behind the user's back: an
# explicit "make <name>_defconfig" or "make menuconfig" is much clearer
# than silently building something nobody asked for.
$(KCONFIG_CONFIG):
	@echo "### No $(KCONFIG_CONFIG) found."; \
	 echo "### Run \"make help\" for the list of ready-made configurations,"; \
	 echo "### then \"make <name>_defconfig\" followed by \"make\"."; \
	 false

#
# Configuration targets
#

.PHONY: $(DEFCONFIGS)
$(DEFCONFIGS): %_defconfig: $(DEFCONFIG_DIR)/%_defconfig
	$(PYTHON) -m defconfig --kconfig $(KCONFIG_TOP) $<

.PHONY: menuconfig
menuconfig:
	$(PYTHON) -m menuconfig $(KCONFIG_TOP)

.PHONY: guiconfig
guiconfig:
	$(PYTHON) -m guiconfig $(KCONFIG_TOP)

.PHONY: oldconfig
oldconfig:
	$(PYTHON) -m oldconfig --kconfig $(KCONFIG_TOP)

.PHONY: olddefconfig
olddefconfig:
	$(PYTHON) -m olddefconfig --kconfig $(KCONFIG_TOP)

.PHONY: savedefconfig
savedefconfig:
	$(PYTHON) -m savedefconfig --kconfig $(KCONFIG_TOP) --out defconfig
	@echo "# Minimal configuration written to defconfig"

.PHONY: allnoconfig
allnoconfig:
	$(PYTHON) -m allnoconfig --kconfig $(KCONFIG_TOP)

.PHONY: alldefconfig
alldefconfig:
	$(PYTHON) -m alldefconfig --kconfig $(KCONFIG_TOP)

.PHONY: listnewconfig
listnewconfig:
	$(PYTHON) -m listnewconfig --kconfig $(KCONFIG_TOP)
