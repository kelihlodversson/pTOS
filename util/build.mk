#
# util/build.mk - objects making up the shared utility routines
#

obj-y += doprintf.o intmath.o langs.o memmove.o string.o nls.o \
	 setjmp.o cookie.o miscutil.o
obj-$(ARCH_M68K) += miscasm.o pexec.o
obj-$(ARCH_ARM) += miscasm.o

# the shell a kernel without one of its own looks for
ifneq ($(CONF_EXTERNAL_CLI)$(CONF_EXTERNAL_DESK),)
obj-y += shells.o
endif

obj-$(CONF_WITH_VIRTIO) += virtio.o

obj-$(ARCH_M68K) += memset.o stringasm.o

# The routines below are only used by the AES and by EmuDesk.
ifdef CONF_WITH_AES
obj-y += gemdos.o optimize.o rectfunc.o
obj-$(ARCH_M68K) += optimopt.o
endif

# C library routines that only FreeType needs (CONF_WITH_VDI_SCALABLE_FONTS).
obj-$(CONF_WITH_VDI_SCALABLE_FONTS) += clibext.o
