#
# desk/program.mk - EmuDesk as a program (CONF_EXTERNAL_DESK)
#
# Included by the top level Makefile.  emudesk.prg is built from the objects
# of desk/build.mk's desk-prog-y, compiled here (in obj/desk/) with the
# flags of the kernel, and linked as an ordinary program: ELF on ARM, PRG
# or ELF on m68k, like the test programs (see the top level Makefile) --
# but without libcmini, whose headers and calling conventions the desktop
# does not use, with a startup (deskstart.S) and runtime (deskrt.c) of
# its own.
#
# The objects are not the kernel's: a program is loaded away from its link
# address, and on ARM the loader (bdos/elfld.c) only rebases words, so
# -mword-relocations; and the kernel's util/ objects are compiled again for
# the same reason.
#
# Copyright (C) 2026 The pTOS development team
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.
#

DESK_PRG = emudesk.prg
DESK_OBJDIR = obj/desk
DESK_OBJ = $(addprefix $(DESK_OBJDIR)/,$(desk-prog-y))

# On ARM the same file goes on the card of every Raspberry Pi (the Pi 1 is
# the oldest), so it is built for the Pi 1 and not for the CPU of the kernel.
ifdef ARCH_ARM
DESK_CPUFLAGS = -march=armv6k -mtune=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard
else
DESK_CPUFLAGS = $(CPUFLAGS)
endif
DESK_MULTILIBFLAGS = $(subst $(CPUFLAGS),$(DESK_CPUFLAGS),$(MULTILIBFLAGS))

DESK_CFLAGS = $(subst $(CPUFLAGS),$(DESK_CPUFLAGS),$(CFLAGS)) -ffunction-sections -fdata-sections $(if $(ARCH_ARM),-mword-relocations)
ifdef ARCH_X86_64
DESK_CFLAGS = $(X32_CLI_CFLAGS) $(if $(CONF_WITH_DYNAMIC_DESK),$(LDSO_APP_CFLAGS)) -DX32_USERLAND -Idesk/x32 -Iinclude -Iobj -Idesk -Iaes -Iaes/shared -Iutil -Iinclude/arch/x86_64 -ffunction-sections -fdata-sections
$(DESK_OBJDIR)/%.o: desk/x32/%.c $(AUTOCONF_H) | $(DESK_OBJDIR)
	$(X32_CC) $(DESK_CFILE_FLAGS) $(DEPFLAGS) -c $< -o $@
$(DESK_OBJDIR)/%.o: desk/x32/%.S | $(DESK_OBJDIR)
	$(X32_CC) $(DESK_SFILE_FLAGS) $(DEPFLAGS) -c $< -o $@
endif
DESK_CFILE_FLAGS = $(strip $(DESK_CFLAGS) $($(current_dir)_copts))
DESK_SFILE_FLAGS = $(strip $(DESK_CFLAGS) $($(current_dir)_sopts))

$(DESK_OBJDIR):
	@mkdir -p $@

# the translated sources (po/POTFILES.in) of a multi-language image, or of
# one in a language other than English, are compiled from their .tr.c, like
# the kernel's (see "Mono-country translated" in the top level Makefile)
ifeq (,$(and $(UNIQUE),$(filter us,$(ETOSLANG))))
$(DESK_OBJDIR)/%.o : %.tr.c | $(DESK_OBJDIR)
	$(CC) $(DESK_CFILE_FLAGS) $(DEPFLAGS) -c $< -o $@

$(DESK_OBJ): $(filter desk/%,$(TRANS_SRC))
endif

ifdef ARCH_M68K
ifdef BUILD_TOOLCHAIN_IS_ELF
$(DESK_OBJDIR)/%.o : %.c $(M68K_LOCAL_LABEL_FIX) | $(DESK_OBJDIR)
	$(CC) $(DESK_CFILE_FLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -MT $@ -S $< -o $@.s
	python3 $(M68K_LOCAL_LABEL_FIX) $@.s $@.fixed.s
	$(CC) $(DESK_SFILE_FLAGS) -x assembler -c $@.fixed.s -o $@
	rm -f $@.s $@.fixed.s
else
$(DESK_OBJDIR)/%.o : %.c | $(DESK_OBJDIR)
	$(CC) $(DESK_CFILE_FLAGS) $(DEPFLAGS) -c $< -o $@
endif
else
$(DESK_OBJDIR)/%.o : %.c | $(DESK_OBJDIR)
	$(CC) $(DESK_CFILE_FLAGS) $(DEPFLAGS) -c $< -o $@
endif

$(DESK_OBJDIR)/%.o : %.S | $(DESK_OBJDIR)
	$(CC) $(DESK_SFILE_FLAGS) $(DEPFLAGS) -c $< -o $@

# the version string, generated into obj/ (see the top level Makefile)
$(DESK_OBJDIR)/version.o: obj/version.c | $(DESK_OBJDIR)
	$(CC) $(DESK_CFILE_FLAGS) $(DEPFLAGS) -c $< -o $@

# The entry is the label of deskstart.S as the linker knows it: C_SYM() of
# asmdefs.h puts no underscore on an ELF toolchain.
ifneq (,$(or $(ARCH_ARM),$(BUILD_TOOLCHAIN_IS_ELF)))
DESK_ENTRY = deskstart
else
DESK_ENTRY = _deskstart
endif

# ARM and the bare m68k-elf: a relocatable ELF at address 0 for the loader of
# bdos/elfld.c; the other m68k toolchains make a PRG with their linker script.
ifdef ARCH_ARM
DESK_LDFLAGS = -Wl,-q -Wl,-Ttext=0
else ifdef BUILD_TOOLCHAIN_ELF
DESK_LDFLAGS = -Wl,-q -Wl,-Ttext=0
endif

ifdef ARCH_X86_64
ifeq ($(CONF_WITH_DYNAMIC_DESK),y)
DESK_LIBAES = $(if $(CONF_WITH_LIBAES),obj/libaes/libaes.so)
$(DESK_PRG): $(DESK_OBJ) obj/x32/libptos-aes.a obj/x32/libptos-vdi.a obj/ldso/dyncrt.o obj/ldso/libptos.so $(DESK_LIBAES)
	$(X32_CC) $(LDSO_APP_LDFLAGS) -Wl,--gc-sections -o $@ $(filter %.o %.a,$^) $(if $(CONF_WITH_LIBAES),-Lobj/libaes -laes) -Lobj/ldso -lptos
else
$(DESK_PRG): $(DESK_OBJ) obj/x32/libptos-aes.a obj/x32/libptos-vdi.a
	$(X32_CC) $(X32_CLI_LDFLAGS) -Wl,--gc-sections -o $@ $^
endif
else
$(DESK_PRG): $(DESK_OBJ)
	@echo '  LD     $@'
	@$(CC) $(DESK_MULTILIBFLAGS) -nostartfiles -nostdlib $(DESK_LDFLAGS) -Wl,-e,$(DESK_ENTRY) -Wl,--gc-sections $(DESK_OBJ) $(LIBS) -o $@
endif

-include $(DESK_OBJ:.o=.d)
