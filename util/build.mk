#
# util/build.mk - objects making up the shared utility routines
#

obj-y += doprintf.o intmath.o langs.o memmove.o string.o miscasm.o nls.o \
	 setjmp.o cookie.o miscutil.o

obj-$(CONF_WITH_VIRTIO) += virtio.o

obj-$(ARCH_M68K) += memset.o stringasm.o

# The routines below are only used by the AES and by EmuDesk.
ifdef CONF_WITH_AES
obj-y += gemdos.o optimize.o rectfunc.o
obj-$(ARCH_M68K) += optimopt.o
endif
