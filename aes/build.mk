#
# aes/build.mk - objects making up the AES
#

obj-$(CONF_WITH_CUIS) += cuis_session.o

obj-$(CONF_WITH_X32_AES) += aes_service.o aes_image.o

ifeq ($(CONF_WITH_AES),y)
obj-y += gemasm.o gemstart.o gemdosif.o
obj-$(ARCH_ARM) += gemdosifc.o
obj-$(ARCH_X86_64) += aes_accessory.o aes_window.o aes_objects_kernel.o aes_tree.o aes_shell.o

obj-y += gemaplib.o gemasync.o gemctrl.o gemdisp.o gemevlib.o gemflag.o \
	 gemfmalt.o gemfmerror.o gemfmlib.o gemfslib.o gemgraf.o gemgraphutil.o gemgrlib.o gemgsxif.o \
	 geminit.o geminput.o gemmnlib.o gemobed.o gemobjop.o gemoblib.o gemobjtree.o \
	 gempd.o gemqueue.o gemrslib.o gemsclib.o gemshlib.o gemsuper.o \
	 gemwmlib.o gemwrect.o gsx2.o gem_rsc.o mforms.o
obj-$(CONF_WITH_LEGACY_RSC_LOAD) += rsload_legacy.o
obj-$(CONF_WITH_PORTABLE_RSC_LOAD) += rsload_portable.o
obj-$(CONF_WITH_MENU_EXTENSION) += gemmnext.o

endif
