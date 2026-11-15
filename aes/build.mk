#
# aes/build.mk - objects making up the AES
#

obj-$(CONF_WITH_CUIS) += cuis_session.o cuis_region.o cuis_input_wait.o


ifeq ($(CONF_WITH_AES),y)
obj-y += gemasm.o gemstart.o gemdosif.o
obj-$(ARCH_ARM) += gemdosifc.o

obj-y += gemaplib.o gemasync.o gemctrl.o gemdisp.o gemevlib.o gemflag.o \
	 gemfmalt.o gemalert.o gemfmerror.o gemformerror.o gemfmlib.o gemformdo.o gemformbutton.o gemwatch.o gemformkey.o gemfslib.o gemgraf.o gemlayout.o gemicon.o gemrender_graphics.o gemgraphutil.o gemgrlib.o gemgrtrack.o gemformanim.o gemgsxif.o \
	 geminit.o geminput.o gemmnlib.o gemmenuchange.o gemobed.o gemedit.o gemobjop.o gemobjwalk.o gemoblib.o gemrender.o \
	 gempd.o gemqueue.o gemrslib.o gemrsaddr.o gemsclib.o gemshlib.o gemshquery.o gemsuper.o \
	 gemwmlib.o gemwrect.o gsx2.o gem_rsc.o mforms.o
obj-$(CONF_WITH_LIBAES) += gemobjtree.o
obj-$(CONF_WITH_LEGACY_RSC_LOAD) += rsload_legacy.o
obj-$(CONF_WITH_PORTABLE_RSC_LOAD) += rsload_portable.o
obj-$(CONF_WITH_MENU_EXTENSION) += gemmnext.o
obj-$(CONF_WITH_3D_OBJECTS) += gemframe.o
obj-$(CONF_WITH_COLOUR_ICONS) += gemcicon.o

endif

# Classic builds expand the optional userspace context arguments away.
obj/gemrender.o obj/gemrender_graphics.o obj/gemgraphutil.o obj/gemicon.o obj/gemedit.o obj/gemgrtrack.o obj/gemformanim.o obj/gemobjwalk.o obj/gemformbutton.o obj/gemwatch.o obj/gemformdo.o obj/gemalert.o obj/gemformerror.o obj/gemfslib.o obj/gemshquery.o obj/gemmenuchange.o obj/gemrsaddr.o obj/rsload_portable.o: include/aescontext.h
