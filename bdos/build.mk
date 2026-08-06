#
# bdos/build.mk - objects making up GEMDOS
#

obj-y += bdosmain.o console.o fsbuf.o fsdir.o fsdrive.o fsfat.o fsglob.o \
	 fshand.o fsio.o fsmain.o fsopnclo.o iumem.o kpgmld.o osmem.o \
	 proc.o time.o umem.o rwa.o
obj-$(CONF_WITH_ELF_LOADER) += elfld.o
