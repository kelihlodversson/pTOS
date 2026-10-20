#
# vdi/freetype/freetype.mk - build FreeType (lib/freetype) for the VDI
#
# Copyright (C) 2026 The pTOS development team
#
# This file is distributed under the GPL, version 2 or at your
# option any later version.  See doc/license.txt for details.
#

#
# Included by the top level Makefile when CONF_WITH_VDI_SCALABLE_FONTS is
# set.  FreeType is compiled with the image's own compiler and CPU flags
# into a library of its own, $(FREETYPE_LIB), in a directory of its own:
# not through obj-y and vpath, where FreeType's file names would share
# the flat obj/ with pTOS's.  pTOS's configuration headers in this
# directory replace FreeType's (FT_CONFIG_*_H), so that nothing of the
# host's C library is used.
#

FREETYPE_DIR = lib/freetype
FREETYPE_BUILDDIR = obj/freetype-build
# FREETYPE_LIB is set by the top level Makefile, ahead of the link rules.

# The base layer (but not FreeType's ftsystem.c: pTOS's is
# vdi/vdi_ftsystem.c), and the modules of ptos_ftmodule.h.
FREETYPE_SRC = \
    src/base/ftinit.c src/base/ftbase.c src/base/ftdebug.c \
    src/base/ftbbox.c src/base/ftbitmap.c src/base/ftglyph.c \
    src/sfnt/sfnt.c src/truetype/truetype.c src/cff/cff.c \
    src/psaux/psaux.c src/psnames/psnames.c src/pshinter/pshinter.c \
    src/raster/raster.c

# Both FreeType and the pTOS files that use it see pTOS's configuration
# headers; the quotes keep the shell off the angle brackets.
FREETYPE_INC = -I$(FREETYPE_DIR)/include -Ivdi/freetype \
    -D'FT_CONFIG_OPTIONS_H=<ptos_ftoption.h>' \
    -D'FT_CONFIG_MODULES_H=<ptos_ftmodule.h>' \
    -D'FT_CONFIG_STANDARD_LIBRARY_H=<ptos_ftstdlib.h>'

# FreeType itself also needs pTOS's headers (ptos_ftstdlib.h includes
# emutos.h).  It is built with its own warnings, not with pTOS's
# -Wmissing-prototypes & co., which it is not written for.
#
# On ARM the vectorizer is told not to combine statements (SLP): GCC turns
# the four "x >>= shift" of FT_Matrix_Check() (src/base/ftcalc.c) into
# "vshl.s32 q3, q3, #0", a shift by nothing instead of by the variable, so
# that FreeType takes every matrix for one it cannot use and drops it: the
# slant of vst_skew() was lost (#598).
FREETYPE_CFLAGS = $(MULTILIBFLAGS) $(TOOLCHAIN_CFLAGS) -std=gnu99 \
    $(OPTFLAGS) $(DEBUGFLAGS) $(OTHERFLAGS) $(if $(ARCH_ARM),-fno-tree-slp-vectorize) \
    -Wall -DFT2_BUILD_LIBRARY \
    $(FREETYPE_INC) $(addprefix -Iinclude/,$(arch_subdirs)) -Iinclude -Iobj

FREETYPE_OBJ = $(addprefix $(FREETYPE_BUILDDIR)/,$(notdir $(FREETYPE_SRC:.c=.o)))

$(FREETYPE_BUILDDIR):
	@mkdir -p $@

# One rule per source, as they come from several directories.  On the
# m68k ELF toolchains the assembler output goes through the same local
# label fix as pTOS's own C files.
define freetype-object
$(FREETYPE_BUILDDIR)/$(notdir $(1:.c=.o)): $(FREETYPE_DIR)/$(1) $(AUTOCONF_H) | $(FREETYPE_BUILDDIR)
ifneq (,$(M68K_LOCAL_LABEL_FIX))
	$$(CC) $$(FREETYPE_CFLAGS) $$(DEPFLAGS) -MF $$(@:.o=.d) -MT $$@ -S $$< -o $$@.s
	python3 $$(M68K_LOCAL_LABEL_FIX) $$@.s $$@.fixed.s
	$$(CC) $$(FREETYPE_CFLAGS) -x assembler -c $$@.fixed.s -o $$@
	rm -f $$@.s $$@.fixed.s
else
	$$(CC) $$(FREETYPE_CFLAGS) $$(DEPFLAGS) -c $$< -o $$@
endif
endef
$(foreach s,$(FREETYPE_SRC),$(eval $(call freetype-object,$(s))))

$(FREETYPE_LIB): $(FREETYPE_OBJ)
	rm -f $@
	$(CROSS_COMPILE)ar rcs $@ $^

# The pTOS files that use FreeType, in vdi/.
vdi_copts += $(FREETYPE_INC)

-include $(wildcard $(FREETYPE_BUILDDIR)/*.d)
