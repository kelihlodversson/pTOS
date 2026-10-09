#
# cli/build.mk - objects making up the built-in EmuCON
#

ifdef ARCH_X86_64
# EmuCON is an x32 program run in ring 3, built apart from the LP64 kernel
# (see the top level Makefile); the kernel only embeds the executable.
obj-y += emucon_image.o
else
obj-y += cmdasm.o cmdmain.o cmdedit.o cmdexec.o cmdint.o cmdparse.o cmdutil.o
obj-$(ARCH_ARM) += cmdgetwh.o
endif
