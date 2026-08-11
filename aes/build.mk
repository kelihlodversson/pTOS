#
# aes/build.mk - objects making up the AES
#

obj-y += gemasm.o gemstart.o gemdosif.o
obj-$(ARCH_ARM) += gemdosifc.o

obj-y += gemaplib.o gemasync.o gemctrl.o gemdisp.o gemevlib.o gemflag.o \
	 gemfmalt.o gemfmlib.o gemfslib.o gemgraf.o gemgrlib.o gemgsxif.o \
	 geminit.o geminput.o gemmnlib.o gemobed.o gemobjop.o gemoblib.o \
	 gempd.o gemqueue.o gemrslib.o gemsclib.o gemshlib.o gemsuper.o \
	 gemwmlib.o gemwrect.o gsx2.o gem_rsc.o mforms.o
obj-$(CONF_WITH_LEGACY_RSC_LOAD) += rsload_legacy.o
obj-$(CONF_WITH_PORTABLE_RSC_LOAD) += rsload_portable.o
