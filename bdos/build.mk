#
# bdos/build.mk - objects making up GEMDOS
#

obj-y += bdosmain.o console.o fsbuf.o fsdir.o fsdrive.o fsfat.o fsglob.o \
	 fshand.o fsio.o fsmain.o fsopnclo.o iumem.o kpgmld.o kproc.o osmem.o \
	 proc.o time.o umem.o rwa.o launch.o
obj-$(CONF_WITH_ELF_LOADER) += elfld.o
obj-$(CONF_WITH_USER_COPY) += uaccess.o

# Ssystem() (bdos/ssystem.c) is needed by every non-m68k/ColdFire arch --
# see the comment in osif() (bdos/bdosmain.c).
obj-$(CONF_WITH_SSYSTEM) += ssystem.o
obj-$(CONF_WITH_KCALL) += kcall.o
obj-$(CONF_WITH_KPROC) += mintproc.o
obj-$(CONF_WITH_GEMDOS_SHUTDOWN) += xshutdown.o
