#
# Makefile - the pTOS/EmuTOS build system
#
# Copyright (C) 2001-2017 The EmuTOS development team.
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.
#
# The build is configured the same way the Linux kernel is:
#
#   make help                  list the ready-made configurations
#   make rpi2_defconfig        start from one of them
#   make menuconfig            adjust it interactively
#   make                       build the configured image
#
# Only GCC is supported, and GNU make is required.  See doc/build.txt for
# the details, including which toolchain each machine needs.
#
# C and assembler sources live in bios/, bdos/, util/, ...  Each of those
# directories has a build.mk listing its objects and a Kconfig describing
# its options.
#

# Grouped targets require GNU Make 4.3.  Apple still ships GNU Make 3.81 as
# /usr/bin/make, so reject it before make parses any grouped-target rules.
MAKE_MAJOR = $(word 1,$(subst ., ,$(MAKE_VERSION)))
MAKE_MINOR = $(word 2,$(subst ., ,$(MAKE_VERSION)))
ifneq (,$(filter 0 1 2 3,$(MAKE_MAJOR)))
$(error GNU Make 4.3 or later is required (found $(MAKE_VERSION)); use Homebrew gmake on macOS)
endif
ifeq (4,$(MAKE_MAJOR))
ifneq (,$(filter 0 1 2,$(MAKE_MINOR)))
$(error GNU Make 4.3 or later is required (found $(MAKE_VERSION)); use Homebrew gmake on macOS)
endif
endif

MAKEFLAGS = --no-print-directory

# Remove the target of any recipe that fails, so that a partially written
# image or generated file can never be mistaken for an up to date one.
.DELETE_ON_ERROR:

# Building the configured image is what "make" alone does.  This has to be
# stated explicitly, because the first rule of the makefile belongs to the
# configuration system included below.
.DEFAULT_GOAL := all

#
# EmuTOS version
#

include version.mk

#
# Configuration system: .config, obj/autoconf.h and obj/auto.conf
#

include tools/kconfig.mk

# Goals that must work without a configuration, either because they are
# what produces one, or because they do not compile anything.
UNCONFIGURED_GOALS = $(CONFIG_TARGETS) help version clean distclean \
                     charset gitready indent checkindent \
                     bugready coldfire-sources

ifeq (,$(filter $(UNCONFIGURED_GOALS),$(MAKECMDGOALS))$(filter release%,$(MAKECMDGOALS)))

# obj/auto.conf turns every enabled option into a make variable set to 'y',
# which is what the obj-$(CONF_WITH_FOO) lists in the build.mk files use.
# On the very first pass it does not exist yet: make generates it and then
# restarts itself, and BUILD_COUNTRY is what tells the two passes apart.
include $(AUTOCONF_MK)

# Simply expanded, so that "ifdef CONFIGURED" tests the result and not the
# text of the assignment.
CONFIGURED := $(if $(BUILD_COUNTRY),1)

endif

# Both can be overridden on the command line, which is how the release
# archives are built for every country in turn:
#   make atari256_defconfig && make COUNTRY=de UNIQUE=de
# UNIQUE is the country the image is restricted to, and is empty for a
# multi-language image.  It also ends up in the name of some images.
COUNTRY ?= $(BUILD_COUNTRY)
UNIQUE ?= $(if $(BUILD_UNIQUE_COUNTRY),$(COUNTRY))

# Country tables and font selection, driven by COUNTRY and UNIQUE.
include country.mk

#
# Machine and architecture
#
# ARCH may hold several values, from the most specific to the most
# generic, so that e.g. a ColdFire build picks up the ColdFire version of a
# file when there is one and the plain m68k version otherwise.
#

ARCH-$(ARCH_COLDFIRE) += coldfire
ARCH-$(ARCH_M68K) += m68k
ARCH-$(ARCH_ARM) += arm
ARCH = $(ARCH-y)

MACHINE-$(MACHINE_ATARI) += atari
MACHINE-$(MACHINE_ARANYM) += atari
MACHINE-$(MACHINE_FIREBEE) += atari
MACHINE-$(MACHINE_M548X) += atari
MACHINE-$(MACHINE_AMIGA) += amiga
MACHINE-$(MACHINE_RPI) += raspi
MACHINE-$(MACHINE_VIRT_ARM) += virt-arm
MACHINE-$(MACHINE_VIRT_M68K) += virt-m68k
MACHINE = $(MACHINE-y)

ifdef CONFIGURED
ifeq (,$(ARCH))
$(error No architecture selected.  Run "make <name>_defconfig" or "make menuconfig")
endif
endif

#
# Directories holding source code
#

# Core directories are essential for basic OS operation.
core-dirs-y = bios bdos fs util

# Optional directories may be disabled for reduced features.
optional-dirs-y = vdi
optional-dirs-$(CONF_WITH_AES) += aes desk
optional-dirs-$(CONF_WITH_CLI) += cli
optional-dirs-$(CONF_WITH_USB) += usb

core_dirs = $(core-dirs-y)
optional_dirs = $(optional-dirs-y)
dirs = $(core_dirs) $(optional_dirs)

arch_subdirs = $(addprefix machine/,$(MACHINE)) $(addprefix arch/,$(ARCH))
arch_dirs = $(foreach d,$(dirs),$(addprefix $(d)/,$(arch_subdirs)))

vpath %.c $(arch_dirs) $(dirs)
vpath %.S $(arch_dirs) $(dirs)

#
# Object lists
#
# Each directory contributes the objects it lists in obj-y.  The core and
# optional objects are kept apart because they are passed to the linker in
# a specific order, with the libgcc helpers in between.
#

# CORE_OBJ and OPTIONAL_OBJ are simply expanded, so that each build.mk
# contributes its own obj-y and not the one of the directory read last.
CORE_OBJ :=
OPTIONAL_OBJ :=

define collect-objects
obj-y :=
include $(1)/build.mk
$(2) += $$(addprefix obj/,$$(obj-y))
endef

$(foreach d,$(core_dirs),$(eval $(call collect-objects,$(d),CORE_OBJ)))
$(foreach d,$(optional_dirs),$(eval $(call collect-objects,$(d),OPTIONAL_OBJ)))

CORE_OBJ += obj/version.o
OBJECTS = $(CORE_OBJ) $(OPTIONAL_OBJ)

#
# Toolchain and compilation flags
#

# Name of the cross toolchain.  It follows the toolchain selected in the
# configuration; CROSS_COMPILE is only an override for a toolchain
# installed under a different name, and is left unset unless the user
# filled it in.  A command line CROSS_COMPILE= still wins over both.
CROSS_COMPILE-$(ARCH_ARM) = arm-none-eabi-
CROSS_COMPILE-$(BUILD_TOOLCHAIN_MINT) = m68k-atari-mint-
CROSS_COMPILE-$(BUILD_TOOLCHAIN_MINTELF) = m68k-atari-mintelf-
CROSS_COMPILE-$(BUILD_TOOLCHAIN_ELF) = m68k-elf-
CROSS_COMPILE ?= $(CROSS_COMPILE-y)

CC = $(CROSS_COMPILE)gcc
CPP = $(CC) -E
OBJDUMP = $(CROSS_COMPILE)objdump
OBJCOPY = $(CROSS_COMPILE)objcopy

# The native C compiler, used for the build tools.
NATIVECC = gcc -std=gnu90 -pedantic $(WARNFLAGS) -W -O

ifdef ARCH_ARM
MULTILIBFLAGS = $(CPUFLAGS) -fsigned-char
TOOLCHAIN_CFLAGS = -fleading-underscore -fno-reorder-functions -DELF_TOOLCHAIN
else
MULTILIBFLAGS = $(CPUFLAGS) -mshort
ifdef BUILD_TOOLCHAIN_IS_ELF
TOOLCHAIN_CFLAGS = -fleading-underscore -Wa,--register-prefix-optional \
                   -fno-reorder-functions -DELF_TOOLCHAIN
endif
endif

# EmuTOS requires C90 with some GNU extensions.
CSTANDARD = -std=gnu90

OTHERFLAGS = -fomit-frame-pointer -fno-common
DEBUGFLAGS = $(if $(DEBUG_INFO),-g)

WARNFLAGS = -Wall -Wundef -Wmissing-prototypes -Wstrict-prototypes

GCCVERSION := $(shell $(CC) -dumpversion 2>/dev/null | cut -d. -f1)
# Add the warning flags that GCC v2 does not support.
ifneq (,$(GCCVERSION))
ifneq (2,$(GCCVERSION))
WARNFLAGS += -Wold-style-definition -Wtype-limits
endif
endif

# Header lookup follows the same order as vpath: the machine specific
# directory first, then the architecture, then the generic one.  obj/ is
# where the generated autoconf.h lives.
current_dir = $(filter-out .,$(word 1,$(subst /, ,$(dir $<))))
include_dirs = $(addprefix include/,$(arch_subdirs)) include obj \
               $(if $(current_dir),$(addprefix $(current_dir)/,$(arch_subdirs)) $(current_dir))
INC = $(addprefix -I,$(include_dirs))

# Automatic header dependency generation, one .d file per object.
DEPFLAGS = -MMD -MP

CFLAGS = $(MULTILIBFLAGS) $(TOOLCHAIN_CFLAGS) $(CSTANDARD) $(OPTFLAGS) \
         $(DEBUGFLAGS) $(OTHERFLAGS) $(WARNFLAGS) $(INC)
CPPFLAGS = $(CFLAGS)

# Per-directory extra options; $(bios_copts) applies to bios/, and so on.
# The USB code is conceptually part of the BIOS and needs access to the
# BIOS private headers.  It also pulls in bdos/fs.h (via usb_global.h),
# which needs -Ifs below when CONF_WITH_PLUGGABLE_FS is set.
usb_copts = $(addprefix -Ibios/,$(arch_subdirs)) -Ibios -Ifs

# virtio_blk.c (bios/) needs the shared virtio-mmio transport header from
# util/.
bios_copts = -Iutil

# bdosmain.c's osif() dispatch hook needs the pluggable filesystem layer's
# public API from fs/, when CONF_WITH_PLUGGABLE_FS is set - as does every
# other bdos/ file that includes fs.h, since FTAB now embeds a PFSCOOKIE
# by value when that option is on.
bdos_copts = -Ifs

# fatfs_pfs.c (fs/) wraps the built-in FAT filesystem, so it needs bdos/'s
# private headers.  virtio_9p_pfs.c similarly wraps bios/virtio_9p.c's
# fid-level API, needing -Ibios.
fs_copts = -Ibdos -Ibios

CFILE_FLAGS = $(strip $(CFLAGS) $($(current_dir)_copts))
SFILE_FLAGS = $(strip $(CFLAGS) $($(current_dir)_sopts))

# Linker: relocation information and, for most targets, a raw binary.
LD = $(CC) $(MULTILIBFLAGS) -nostartfiles -nostdlib
LIBS = -lgcc
LDFLAGS = -Wl,-T,obj/emutospp.ld
PCREL_LDFLAGS = -Wl,--oformat=binary,-Ttext=0,--entry=0

ifdef ARCH_ARM
# The ARM linker script cannot produce a raw binary directly.
EMUTOS_IMG = emutos.elf
LDFLAGS += -Wl,-build-id=none
else
EMUTOS_IMG = emutos.img
endif

#
# TOCLEAN accumulates the names of the files to remove on "make clean".
# Temporary Makefile files are *.tmp.
#

ifneq (,$(findstring CYGWIN,$(shell uname)))
CORE = *.stackdump
else
CORE = core
endif

TOCLEAN = *~ */*~ $(CORE) *.tmp obj/*.tmp obj/*.o obj/*.d obj/*.h obj/*.c \
          obj/*.ld obj/auto.conf */*.dsm

#
# GEN_SRC accumulates the generated source files.  They are built before
# anything else is compiled, and removed by "make clean".
#

GEN_SRC =

#
# The image to build
#
# Every image type derives its default name from the configuration; the
# IMAGE_NAME option overrides it.
#

ROM_IMAGE := $(if $(TARGET_192)$(TARGET_256)$(TARGET_512)$(TARGET_CART),y)

ifdef TARGET_192
ROMSIZE = 192
image-default = ptos192$(UNIQUE).img
MEMBOT_REFERENCE = TOS102
endif
ifdef TARGET_256
ROMSIZE = 256
image-default = ptos256$(UNIQUE).img
MEMBOT_REFERENCE = TOS162
endif
ifdef TARGET_512
ROMSIZE = 512
image-default = ptos512k.img
# The symbol file is useful when debugging this image under Hatari.
image-extra = $(basename $(IMAGE)).sym
MEMBOT_REFERENCE = TOS404
endif
ifdef TARGET_CART
ROMSIZE = 128
image-default = ptoscart.img
MEMBOT_REFERENCE = TOS102
endif
ifdef TARGET_PRG
image-default = ptos$(UNIQUE).prg
endif
ifdef TARGET_FLOPPY
image-default = ptos$(UNIQUE).st
endif
ifdef TARGET_SREC
image-default = $(if $(MACHINE_FIREBEE),ptosfb.s19,ptos-m548x-$(if $(CONF_WITH_BAS_MEMORY_MAP),bas,dbug).s19)
MEMBOT_REFERENCE = TOS404
endif
ifdef TARGET_AMIGA_ROM
image-default = ptos-amiga.rom
MEMBOT_REFERENCE = TOS162
endif
ifdef TARGET_AMIGA_KICKDISK
image-default = ptos-kickdisk.adf
MEMBOT_REFERENCE = TOS162
endif
ifdef TARGET_AMIGA_FLOPPY
image-default = ptos.adf
endif
ifdef TARGET_RPI_KERNEL
image-default = $(strip \
    $(if $(TARGET_RPI1),kernel.img) \
    $(if $(TARGET_RPI2),kernel7.img) \
    $(if $(TARGET_RPI3),kernel8-32.img) \
    $(if $(TARGET_RPI4),kernel7l.img))
MEMBOT_REFERENCE = TOS162
endif
ifdef TARGET_VIRT_ARM_KERNEL
image-default = virt-arm.elf
MEMBOT_REFERENCE = TOS162
endif
ifdef TARGET_VIRT_M68K_KERNEL
image-default = virt-m68k.elf
MEMBOT_REFERENCE = TOS162
endif

IMAGE = $(if $(IMAGE_NAME),$(IMAGE_NAME),$(image-default))

TOCLEAN += *.img *.map *.elf *.prg *.st *.s19 *.stc *.rom *.adf *.sym

#
# Production targets
#

.PHONY: all
all: $(IMAGE) $(image-extra)
	@echo "# $(IMAGE) is ready"
	@$(call report-memory)

# The reference values below have been gathered from major TOS versions,
# and are used to put the amount of RAM used by EmuTOS in perspective.
MEMBOT_TOS102 = 0x0000ca00
MEMBOT_TOS104 = 0x0000a84e
MEMBOT_TOS162 = 0x0000a832
MEMBOT_TOS206 = 0x0000ccb2
MEMBOT_TOS305 = 0x0000e6fc
MEMBOT_TOS404 = 0x0000f99c

MEMBOT_TOS102_NAME = TOS 1.02
MEMBOT_TOS104_NAME = TOS 1.04
MEMBOT_TOS162_NAME = TOS 1.62
MEMBOT_TOS206_NAME = TOS 2.06
MEMBOT_TOS305_NAME = TOS 3.05
MEMBOT_TOS404_NAME = TOS 4.04

define report-memory
MEMBOT=$(call SHELL_SYMADDR,__end_os_stram,emutos.map); \
echo "# RAM used: $$(($$MEMBOT)) bytes$(if $(MEMBOT_REFERENCE), ($$(($$MEMBOT - $(MEMBOT_$(MEMBOT_REFERENCE)))) bytes more than $(MEMBOT_$(MEMBOT_REFERENCE)_NAME)))"
endef

.PHONY: help
help:
	@echo "pTOS - Portable EmuTOS"
	@echo
	@echo "Configuration targets:"
	@echo "  <name>_defconfig  start from the configuration configs/<name>_defconfig"
	@echo "  menuconfig        edit the configuration interactively"
	@echo "  guiconfig         same, in a graphical window"
	@echo "  oldconfig         ask about the options added since the last run"
	@echo "  olddefconfig      accept the default value for every new option"
	@echo "  savedefconfig     write a minimal ./defconfig for the current .config"
	@echo "  allnoconfig       disable every optional feature"
	@echo
	@echo "Build targets:"
	@echo "  all               build the configured image (this is the default)"
	@echo "  clean             remove the generated files, keeping .config"
	@echo "  distclean         remove everything, including .config"
	@echo "  dsm               dsm.txt, an edited disassembly of the image"
	@echo "  *.dsm             disassembly of any .c or almost any .img file"
	@echo "  tools             build the host tools used by the build"
	@echo "  release           build the release archives into $(RELEASE_DIR)"
	@echo "  version           display the EmuTOS version"
	@echo
	@echo "Available configurations:"
	@$(foreach c,$(DEFCONFIGS),echo "  $(c)";)

# Display the EmuTOS version
.PHONY: version
version:
	@echo '$(VERSION)'

#
# Makefile functions
#

# Shell command to get the address of a symbol
FUNCTION_SHELL_GET_SYMBOL_ADDRESS = printf 0x%08x $$(awk '/^ *0x[^ ]* *$(1)( |$$)/{print $$1}' $(2))

# Function to get the address of a symbol into a Makefile variable
# $(1) = symbol name
# $(2) = map file name
MAKE_SYMADDR = $(shell $(call FUNCTION_SHELL_GET_SYMBOL_ADDRESS,$(1),$(2)))

# Function to get the address of a symbol into a shell variable.
# This is useful to make an action in the first line of a recipe,
# then to get the result on the second line.  Makefile variables in a
# recipe can't be used for that, because they are evaluated before
# executing all recipe lines (but after building prerequisites).
SHELL_SYMADDR = $$($(call FUNCTION_SHELL_GET_SYMBOL_ADDRESS,$(1),$(2)))

#
# Preprocess the linker script, to allow #include, #define, #if, etc.
#

obj/emutospp.ld: emutos.ld include/config.h tosvars.ld $(AUTOCONF_H)
	$(CPP) $(CPPFLAGS) -P -x c $< -o $@

#
# The kernel image itself.  The map file must be built at the same time,
# to enable one generic target to deal with all edited disassembly.
#

$(EMUTOS_IMG): $(OBJECTS) obj/emutospp.ld
	$(LD) $(CORE_OBJ) $(LIBS) $(OPTIONAL_OBJ) $(LIBS) $(LDFLAGS) \
	  -Wl,-Map=emutos.map -o $@
	@if [ $$(($$(awk '/^\.data /{print $$3}' emutos.map))) -gt 0 ]; then \
	  echo "### Warning: The DATA segment is not empty."; \
	  echo "### Please examine emutos.map and use \"const\" where appropriate."; \
	fi
	@echo "# TEXT=$(call SHELL_SYMADDR,__text,emutos.map)"\
" STKBOT=$(call SHELL_SYMADDR,_stkbot,emutos.map)"\
" LOWSTRAM=$(call SHELL_SYMADDR,__low_stram_start,emutos.map)"\
" BSS=$(call SHELL_SYMADDR,__bss,emutos.map)"\
" MEMBOT=$(call SHELL_SYMADDR,__end_os_stram,emutos.map)"

#
# Padded ROM images (192/256/512 KB and the 128 KB diagnostic cartridge)
#

ifdef ROM_IMAGE
$(IMAGE): $(EMUTOS_IMG) mkrom
	./mkrom pad $(ROMSIZE)k $< $@
ifdef TARGET_CART
	./mkrom stc $(EMUTOS_IMG) ptos.stc
endif
endif

# Hatari symbol file, useful with the 512 KB image
%.sym: $(EMUTOS_IMG) tools/map2sym.sh
	$(SHELL) tools/map2sym.sh emutos.map >$@

#
# Motorola S-record image, for the ColdFire boards
#

ifdef TARGET_SREC
$(IMAGE): $(EMUTOS_IMG)
	$(OBJCOPY) -I binary -O srec --change-addresses $(SREC_LMA) $< $@
endif

#
# Raspberry Pi kernel image
#

ifdef TARGET_RPI_KERNEL
$(IMAGE): $(EMUTOS_IMG)
	$(OBJCOPY) $< -O binary $@
endif

#
# QEMU virt (ARM) kernel image — passed to QEMU as an ELF, unchanged
#

ifdef TARGET_VIRT_ARM_KERNEL
$(IMAGE): $(EMUTOS_IMG)
	cp $< $@
endif

#
# QEMU virt (m68k) kernel image — passed to QEMU as an ELF, unchanged
#

ifdef TARGET_VIRT_M68K_KERNEL
$(IMAGE): $(EMUTOS_IMG)
	cp $< $@
endif

#
# Amiga images
#

AMIGA_ROM = ptos-amiga.rom

ifdef TARGET_AMIGA_ROM
$(IMAGE): $(EMUTOS_IMG) mkrom
	./mkrom amiga $< $@
endif

ifdef TARGET_AMIGA_KICKDISK
$(IMAGE): $(AMIGA_ROM) mkrom
	./mkrom amiga-kickdisk $< $@

$(AMIGA_ROM): $(EMUTOS_IMG) mkrom
	./mkrom amiga $< $@
endif

ifdef TARGET_AMIGA_FLOPPY
$(IMAGE): amigaboot.img $(EMUTOS_IMG) mkrom
	./mkrom amiga-floppy amigaboot.img $(EMUTOS_IMG) $@

amigaboot.img: obj/amigaboot.o obj/bootram.o
	$(LD) $+ $(PCREL_LDFLAGS) -o $@

obj/amigaboot.o: obj/ramtos.h
endif

#
# Special variants of EmuTOS running in RAM instead of ROM.
# In this case, $(EMUTOS_IMG) needs to be loaded into RAM by some loader.
#

obj/ramtos.h: $(EMUTOS_IMG)
	@echo '# Generating $@'
	@printf \
'/* Generated from emutos.map */\n'\
'#define ADR_TEXT $(call MAKE_SYMADDR,__text,emutos.map)\n'\
'#define ADR_ALTRAM_REGIONS $(call MAKE_SYMADDR,_altram_regions,emutos.map)\n'\
>$@

# incbin dependencies are not automatically detected
obj/ramtos.o: $(EMUTOS_IMG)
obj/boot.o obj/bootsect.o: obj/ramtos.h

ifdef TARGET_PRG
$(IMAGE): obj/minicrt.o obj/boot.o obj/bootram.o obj/ramtos.o
	$(LD) $+ $(LIBS) -o $@ -s
endif

ifdef TARGET_FLOPPY
$(IMAGE): mkflop bootsect.img $(EMUTOS_IMG)
	./mkflop bootsect.img $(EMUTOS_IMG) $@

bootsect.img: obj/bootsect.o obj/bootram.o
	$(LD) $+ $(PCREL_LDFLAGS) -o $@
endif

#
# Misc utilities, built on demand
#

date.prg: obj/minicrt.o obj/doprintf.o obj/date.o
	$(LD) $+ $(LIBS) -o $@ -s

dumpkbd.prg: obj/minicrt.o obj/memmove.o obj/dumpkbd.o obj/doprintf.o \
	     obj/string.o
	$(LD) $+ $(LIBS) -o $@ -s

#
# Host tools
#

TOCLEAN += bug draft erd grd ird mrd mkflop mkrom tos-lang-change \
           temp.rsc temp.def

bug: tools/bug.c
	$(NATIVECC) $< -o $@

mkrom: tools/mkrom.c
	$(NATIVECC) $< -o $@

mkflop: tools/mkflop.c
	$(NATIVECC) $< -o $@

erd: tools/erd.c
	$(NATIVECC) $< -o $@
grd: tools/erd.c
	$(NATIVECC) -DGEM_RSC $< -o $@
ird: tools/erd.c
	$(NATIVECC) -DICON_RSC $< -o $@
mrd: tools/erd.c
	$(NATIVECC) -DMFORM_RSC $< -o $@

# draft reads the configuration to know whether EmuCON is included.
draft: tools/draft.c $(AUTOCONF_H)
	$(NATIVECC) -Iobj $< -o $@

# User tool, not needed to build EmuTOS
tos-lang-change: tools/tos-lang-change.c
	$(NATIVECC) $< -o $@

.PHONY: tools
tools: bug draft erd grd ird mrd mkflop mkrom tos-lang-change

#
# NLS support
#

POFILES = $(wildcard po/*.po)

GEN_SRC += util/langs.c
TOCLEAN += po/messages.pot

util/langs.c: $(POFILES) po/LINGUAS bug po/messages.pot
	./bug make

po/messages.pot: bug po/POTFILES.in $(shell grep -v '^#' po/POTFILES.in)
	./bug xgettext

#
# Resource support
#

# erd/grd normally fold repeated strings ("OK", "Cancel", ...) into one
# shared 'char[]' array referenced from every object that uses them. That
# array can only be initialized from a real string literal.  In a
# multi-language (CONF_WITH_NLS) build the resource sources are translated
# with 'bug translate all' (see the NLS support section below), which turns
# every N_(...) into a numeric message id disguised as a pointer -- valid
# for initializing a 'const char *', not a 'char[]'.  -n disables the shared
# table so each object gets its own N_()-wrapped pointer instead.
ifeq (,$(UNIQUE))
NOSHAREARG = -n
endif

DESKRSC_BASE = desk/desktop
DESKRSCGEN_BASE = desk/desk_rsc
GEMRSC_BASE = aes/gem
GEMRSCGEN_BASE = aes/gem_rsc
ICONRSC_BASE = desk/icon
ICONRSCGEN_BASE = desk/icons
MFORMRSC_BASE = aes/mform
MFORMRSCGEN_BASE = aes/mforms
GEN_SRC += $(DESKRSCGEN_BASE).c $(DESKRSCGEN_BASE).h \
           $(GEMRSCGEN_BASE).c $(GEMRSCGEN_BASE).h \
           $(ICONRSCGEN_BASE).c $(ICONRSCGEN_BASE).h \
           $(MFORMRSCGEN_BASE).c $(MFORMRSCGEN_BASE).h

$(DESKRSCGEN_BASE).c $(DESKRSCGEN_BASE).h &: draft erd $(DESKRSC_BASE).rsc $(DESKRSC_BASE).def
	./draft $(DESKRSC_BASE) temp
	./erd $(NOSHAREARG) -pdesk temp $(DESKRSCGEN_BASE)
$(GEMRSCGEN_BASE).c $(GEMRSCGEN_BASE).h &: grd $(GEMRSC_BASE).rsc $(GEMRSC_BASE).def
	./grd $(NOSHAREARG) $(GEMRSC_BASE) $(GEMRSCGEN_BASE)
$(ICONRSCGEN_BASE).c $(ICONRSCGEN_BASE).h &: ird $(ICONRSC_BASE).rsc $(ICONRSC_BASE).def
	./ird -picon $(ICONRSC_BASE) $(ICONRSCGEN_BASE)
$(MFORMRSCGEN_BASE).c $(MFORMRSCGEN_BASE).h &: mrd $(MFORMRSC_BASE).rsc $(MFORMRSC_BASE).def
	./mrd -pmform $(MFORMRSC_BASE) $(MFORMRSCGEN_BASE)

#
# Set up files in preparation for 'bug update'
#

.PHONY: bugready
bugready: bug erd grd
	./erd $(NOSHAREARG) -pdesk $(DESKRSC_BASE) $(DESKRSCGEN_BASE)
	./grd $(NOSHAREARG) $(GEMRSC_BASE) $(GEMRSCGEN_BASE)
	./bug xgettext

#
# Mono-country translated EmuTOS: translate the sources only when the
# language is not 'us' and a single-country image is requested.
#
# When the '.tr.c' files are present, the '.o' files are compiled from them
# because the '%.o: %.tr.c' rule comes before the normal '%.o: %.c' rule.
# Changing the country removes both the '.o' files (to force rebuilding
# them) and the '.tr.c' files, otherwise switching from fr to us would
# falsely keep the French translations.  See the obj/country target below.
#

TRANS_CSRC = $(shell sed -e '/^[^a-z]/d' <po/POTFILES.in)
TRANS_SRC = $(subst .c,.tr.c,$(TRANS_CSRC))

TOCLEAN += */*.tr.c obj/country

ifneq (,$(UNIQUE))
ifneq (us,$(ETOSLANG))
TRANSLATE = 1
endif
endif

$(EMUTOS_IMG): $(TRANS_SRC)

ifdef TRANSLATE
obj/%.o : %.tr.c
	$(CC) $(CFILE_FLAGS) $(DEPFLAGS) -c $< -o $@

%.tr.c : %.c po/$(ETOSLANG).po bug po/LINGUAS obj/country
	./bug translate $(ETOSLANG) $<
else ifeq (,$(UNIQUE))
obj/%.o : %.tr.c
	$(CC) $(CFILE_FLAGS) $(DEPFLAGS) -c $< -o $@

# Multi-language (CONF_WITH_NLS) image: util/nls.c's etos_gettext() indexes
# a per-language offset table directly, so every _()/N_() call site must be
# rewritten at build time to carry the numeric message id instead of the
# original string -- that's what 'bug translate all' does.  All of
# po/POTFILES.in is translated in a single pass (a grouped target, so make
# only runs the recipe once) because message ids come from po/messages.pot
# and must stay consistent across every translated file, exactly as they do
# in the offset tables 'bug make' writes to util/langs.c.
$(TRANS_SRC) &: po/messages.pot bug po/LINGUAS $(TRANS_CSRC) obj/country
	./bug translate all $(TRANS_CSRC)
else
# A '.d' generated for a translated country names the '.tr.c' as the source
# of its '.o'.  -MMD -MP only writes the phony targets that keep a deleted
# *header* from breaking the build; the source itself never gets one, so
# without a rule for the '.tr.c' make stops at "No rule to make target
# 'bios/bios.tr.c'" as soon as an untranslated country follows a translated
# one -- which is what every "make release" does when it walks the
# countries.  Deleting the stale '.d' is not enough, because make has read
# it long before any recipe runs.
#
# An empty rule is exactly what -MP would have emitted: the missing '.tr.c'
# counts as made, and the '.o' is rebuilt from the plain '.c'.
$(TRANS_SRC): ;
endif

# obj/country contains the current values of $(COUNTRY) and $(UNIQUE).
# Whenever it changes, the stale translated sources and objects are
# removed, even without doing a full rebuild.
#
# The generated dependency file goes with them: it describes a '.tr.c' that
# no longer exists, so leaving it behind would be a trap for the next reader.
# See the empty rule for $(TRANS_SRC) above for why removing it here cannot
# be what keeps the build working.

# A phony target is never up to date, so the recipe below always runs.
# If it does not touch obj/country, the target is considered up to date.
.PHONY: always-execute-recipe

obj/country: always-execute-recipe | obj
	@echo $(COUNTRY) $(UNIQUE) > last.tmp; \
	if [ -e $@ ]; \
	then \
	  if cmp -s last.tmp $@; \
	  then \
	    rm last.tmp; \
	    exit 0; \
	  fi; \
	fi; \
	echo "echo $(COUNTRY) $(UNIQUE) > $@"; \
	mv last.tmp $@; \
	for i in $(TRANS_SRC); \
	do \
	  j=obj/`basename $$i tr.c`o; \
	  d=obj/`basename $$i tr.c`d; \
	  echo "rm -f $$i $$j $$d"; \
	  rm -f $$i $$j $$d; \
	done

#
# i18nconf.h - the parts of the localization that need symbolic names
# (CONF_KEYB is #defined to KEYB_US, itself #defined elsewhere) and are
# therefore not generated by the configuration system.
#

GEN_SRC += include/i18nconf.h

ifneq (,$(UNIQUE))
include/i18nconf.h: obj/country
	@echo '# Generating $@ with CONF_LANG="$(ETOSLANG)" CONF_KEYB=KEYB_$(ETOSKEYB) CONF_CHARSET=CHARSET_$(ETOSCSET)'
	@rm -f $@; touch $@
	@echo '#define CONF_MULTILANG 0' >> $@
	@echo '#define CONF_WITH_NLS 0' >> $@
	@echo '#define CONF_LANG "$(ETOSLANG)"' >> $@
	@echo '#define CONF_KEYB KEYB_$(ETOSKEYB)' >> $@
	@echo '#define CONF_CHARSET CHARSET_$(ETOSCSET)' >> $@
	@echo "#define CONF_IDT ($(ETOSIDT))" >> $@
else
include/i18nconf.h: obj/country
	@echo '# Generating $@ with CONF_MULTILANG=1'
	@rm -f $@; touch $@
	@echo '#define CONF_MULTILANG 1' >> $@
	@echo '#define CONF_WITH_NLS 1' >> $@
endif

#
# ctables.h - the country tables, generated from country.mk, and only
# included in bios/country.c
#

GEN_SRC += bios/ctables.h

bios/ctables.h: country.mk tools/genctables.awk
	awk -f tools/genctables.awk < country.mk > $@

#
# OS header
#

GEN_SRC += bios/header.h

bios/header.h: tools/mkheader.awk obj/country
	awk -f tools/mkheader.awk $(COUNTRY) > $@

#
# Version string
#

GEN_SRC += obj/version.c

# This temporary file is always generated
obj/version2.c: | obj
	@echo '/* Generated from Makefile */' > $@
	@echo 'const char version[] = "$(VERSION)";' >> $@

# If the official version file is different than the temporary one, update it
obj/version.c: obj/version2.c
	@if ! cmp -s $@ $< ; then \
	  echo '# Updating $@ with VERSION=$(VERSION)' ; \
	  cp $< $@ ; \
	fi ; \
	rm $<

#
# Build rules
#
# The little black magic here allows e.g. $(bios_copts) to specify
# additional options for the C sources in bios/, and $(vdi_sopts) to
# specify additional options for the assembler sources in vdi/.
#

obj:
	@mkdir -p obj

obj/%.o : %.c | obj
	$(CC) $(CFILE_FLAGS) $(DEPFLAGS) -c $< -o $@

obj/%.o : %.S | obj
	$(CC) $(SFILE_FLAGS) $(DEPFLAGS) -c $< -o $@

%.dsm : %.c
	$(CC) $(CFILE_FLAGS) -S $< -o $@

# The version string is generated into obj/, which is not in the vpath.
obj/version.o: obj/version.c
	$(CC) $(CFILE_FLAGS) $(DEPFLAGS) -c $< -o $@

# Objects that are not part of the image itself, but are linked into the
# RAM TOS loaders and into the small utility programs.
EXTRA_OBJ = obj/minicrt.o obj/boot.o obj/bootram.o obj/ramtos.o \
            obj/bootsect.o obj/amigaboot.o obj/date.o obj/dumpkbd.o

# Only when the AES is built: it describes aes/struct.h, whose uda holds
# AES_STACK_SIZE longs, and the configuration only defines AES_STACK_SIZE
# when CONF_WITH_AES is set.  Generating it unconditionally breaks the
# configurations that leave the AES out, such as the diagnostic cartridge.
ifdef CONF_WITH_AES
GEN_SRC += aes/asm_struct_gen.h

aes/asm_struct_gen.h: aes/gen_asm_defines.c $(AUTOCONF_H)
	$(CC) $(CFILE_FLAGS) -S $< -o - | grep '^#define' > $@
endif

#
# We don't generate this automatically, because it might be processed by
# the wrong compiler when a non-m68k machine is configured.
#
include/arch/m68k/lineaasm.h: vdi/gen_asm_defines.c bios/lineavars.h
	@echo "warning: $@ is out of date" >&2
	@echo "run \"$(CC) $(CFILE_FLAGS) -S $< -o - | grep '^#define' > $@\" to regenerate it" >&2

#
# Generic dsm handling
#

TOCLEAN += *.dsm dsm.txt

%.dsm: %.map %.img
	vma=`sed -e '/^\.text/!d;s/[^0]*//;s/ .*//;q' $<`; \
	$(OBJDUMP) --target=binary --architecture=$(if $(ARCH_ARM),arm,m68k) \
	  --adjust-vma=$$vma -D $*.img \
	  | sed -e '/^ *[0-9a-f]*:/!d;s/^   /000/;s/^  /00/;s/:	/: /' > dsm.tmp
	sed -e '/^ *0x/!d;s///;s/  */:  /' $< > map.tmp
	cat dsm.tmp map.tmp | LC_ALL=C sort > $@
	rm -f dsm.tmp map.tmp

dsm.txt: emutos.dsm
	cp $< $@

.PHONY: dsm
dsm: dsm.txt

.PHONY: show
show: dsm.txt
	cat dsm.txt

#
# indent - indents the files except when there are warnings
# checkindent - check for indent warnings, but do not alter files
#

INDENT = indent -kr
INDENTFILES = bdos/*.c bios/*.c util/*.c tools/*.c desk/*.c aes/*.c vdi/*.c

.PHONY: checkindent
checkindent:
	@err=0 ; \
	for i in $(INDENTFILES) ; do \
		$(INDENT) <$$i 2>err.tmp >/dev/null; \
		if test -s err.tmp ; then \
			err=`expr $$err + 1`; \
			echo in $$i:; \
			cat err.tmp; \
		fi \
	done ; \
	rm -f err.tmp; \
	if [ $$err -ne 0 ] ; then \
		echo indent issued warnings on $$err 'file(s)'; \
		false; \
	else \
		echo done.; \
	fi

.PHONY: indent
indent:
	@err=0 ; \
	for i in $(INDENTFILES) ; do \
		$(INDENT) <$$i 2>err.tmp | expand >indent.tmp; \
		if ! test -s err.tmp ; then \
			if ! cmp -s indent.tmp $$i ; then \
				echo indenting $$i; \
				mv $$i $$i~; \
				mv indent.tmp $$i; \
			fi \
		else \
			err=`expr $$err + 1`; \
			echo in $$i:; \
			cat err.tmp; \
		fi \
	done ; \
	rm -f err.tmp indent.tmp; \
	if [ $$err -ne 0 ] ; then \
		echo $$err 'file(s)' untouched because of warnings; \
		false; \
	fi

#
# gitready
#

# Check the sources charset (no automatic fix)
.PHONY: charset
charset:
	@echo "# All the files below should use charset=utf-8"
	find . -type f '!' -path '*/.git/*' '!' -path './obj/*' '!' -path './*.img' '!' -path './?rd*' '!' -path './draft*' '!' -path './bug*' '!' -path './mkrom*' '!' -name '*.def' '!' -name '*.rsc' '!' -name '*.icn' '!' -name '*.po' -print0 | xargs -0 file -i |grep -v us-ascii

.PHONY: gitready
gitready:
	tools/check-gitready.sh

#
# ColdFire autoconverted sources.
# They are not generated automatically.
# To regenerate them, type "make coldfire-sources".
# You will need the PortAsm/68K for ColdFire tool from MicroAPL.
# See http://www.microapl.co.uk/Porting/ColdFire/pacf_download.html
#

PORTASM = pacf
PORTASMFLAGS = -blanks on -core v4 -hardware_divide -hardware_mac -a gnu -out_syntax standard -nowarning 402,502,900,1111,1150 -noerrfile

GENERATED_COLDFIRE_SOURCES = vdi/arch/coldfire/vdi_tblit.S

.PHONY: coldfire-sources
coldfire-sources:
	rm -f $(GENERATED_COLDFIRE_SOURCES)
	$(MAKE) firebee_defconfig
	$(MAKE) $(GENERATED_COLDFIRE_SOURCES)

# Intermediate target (intermediate files are automatically removed)
TOCLEAN += vdi/*_preprocessed.*
vdi/%_preprocessed.s: vdi/%.S
	$(CPP) $(CFILE_FLAGS) $< -o $@

vdi/%_cf.S: vdi/%_preprocessed.s
	cd $(<D) && $(PORTASM) $(PORTASMFLAGS) -o $(@F) $(<F)
	dos2unix $@
	sed -i $@ \
		-e "s:\.section\t.bss,.*:.bss:g" \
		-e "s:\( \|\t\)bsr\(  \|\..\):\1jbsr :g" \
		-e "s:\( \|\t\)bra\(  \|\..\):\1jra  :g" \
		-e "s:\( \|\t\)beq\(  \|\..\):\1jeq  :g" \
		-e "s:\( \|\t\)bne\(  \|\..\):\1jne  :g" \
		-e "s:\( \|\t\)bgt\(  \|\..\):\1jgt  :g" \
		-e "s:\( \|\t\)bge\(  \|\..\):\1jge  :g" \
		-e "s:\( \|\t\)blt\(  \|\..\):\1jlt  :g" \
		-e "s:\( \|\t\)ble\(  \|\..\):\1jle  :g" \
		-e "s:\( \|\t\)bcc\(  \|\..\):\1jcc  :g" \
		-e "s:\( \|\t\)bcs\(  \|\..\):\1jcs  :g" \
		-e "s:\( \|\t\)bpl\(  \|\..\):\1jpl  :g" \
		-e "s:\( \|\t\)bmi\(  \|\..\):\1jmi  :g" \
		-e "s:\( \|\t\)bhi\(  \|\..\):\1jhi  :g" \
		-e "s:\( \|\t\)blo\(  \|\..\):\1jlo  :g" \
		-e "s:\( \|\t\)bhs\(  \|\..\):\1jhs  :g" \
		-e "s:\( \|\t\)bls\(  \|\..\):\1jls  :g" \
		-e "s:\( \|,\)0(%:\1(%:g"

#
# Standalone EmuCON, built by cli/Makefile
#

TOCLEAN += cli/version.c cli/*.o cli/*.tos

#
# The targets for building a release are in a separate file
#

include release.mk

#
# Local Makefile, not imported into Git
#

ifneq (,$(wildcard local.mk))
include local.mk
endif

#
# Clean
#

TOCLEAN += $(GEN_SRC)

.PHONY: clean
clean:
	rm -f $(TOCLEAN)

.PHONY: distclean
distclean: clean
	rm -rf dep
	rm -f $(KCONFIG_CONFIG) $(KCONFIG_CONFIG).old defconfig

#
# Every object depends on the configuration, and none can be compiled
# before the generated sources exist.  This has to come after the last
# GEN_SRC assignment, because prerequisites are expanded when read.
#

$(OBJECTS) $(EXTRA_OBJ): $(AUTOCONF_H)
$(OBJECTS) $(EXTRA_OBJ): | $(GEN_SRC)

#
# Header dependencies, generated by the compiler along with the objects
#

-include $(wildcard obj/*.d)
