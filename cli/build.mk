#
# cli/build.mk - objects making up the built-in EmuCON
#

ifdef ARCH_X86_64
# EmuCON is an x32 program run in ring 3, built apart from the LP64 kernel
# (see the top level Makefile).  The kernel embeds the executable, unless
# CONF_EXTERNAL_CLI has it loaded from the boot drive as EMUCON.TOS.
ifneq ($(CONF_EXTERNAL_CLI),y)
obj-y += emucon_image.o
endif
else ifneq ($(CONF_EXTERNAL_CLI),y)
# (with CONF_EXTERNAL_CLI, EmuCON is the program emucon.tos: top level Makefile)
obj-y += cmdasm.o cmdmain.o cmdedit.o cmdexec.o cmdint.o cmdparse.o cmdutil.o
obj-$(ARCH_ARM) += cmdgetwh.o
endif
