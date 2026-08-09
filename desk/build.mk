#
# desk/build.mk - objects making up the EmuDesk desktop
#

obj-y += deskstart.o

obj-y += deskmain.o gembind.o deskact.o deskapp.o deskdir.o deskfpd.o \
	 deskfun.o deskglob.o deskinf.o deskins.o deskobj.o deskpro.o \
	 deskrez.o deskrsrc.o desksupp.o deskwin.o desk_rsc.o icons.o

obj-$(CONF_WITH_VDI_CICON_TEST) += cicontest_rsc.o

# This one must be the last GEM object.
obj-y += endgem.o
