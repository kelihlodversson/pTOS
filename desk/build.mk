#
# desk/build.mk - objects making up the EmuDesk desktop
#

# the desktop: in the kernel image, or (CONF_EXTERNAL_DESK) a program of its
# own, emudesk.prg, which desk/program.mk builds from desk-prog-y
desk-objs := deskmain.o gembind.o deskact.o deskapp.o deskdir.o deskfpd.o \
	     deskfun.o deskglob.o deskinf.o deskins.o deskobj.o deskpro.o \
	     deskrez.o deskrsrc.o desksupp.o deskwin.o desk_rsc.o icons.o

ifeq ($(CONF_EXTERNAL_DESK),y)

# its startup and runtime, and the helpers of util/ it calls
desk-prog-y := deskstart.o deskrt.o $(desk-objs)
desk-prog-y += doprintf.o intmath.o langs.o memmove.o string.o nls.o miscutil.o \
	       gemdos.o optimize.o rectfunc.o version.o
desk-prog-y += $(if $(ARCH_M68K),pexec.o memset.o stringasm.o optimopt.o)
desk-prog-y += $(if $(ARCH_ARM),miscasm.o)
ifdef ARCH_X86_64
desk-prog-y := $(filter-out deskstart.o,$(desk-prog-y)) deskentry.o deskuser.o \
               deskaes.o gemlayout.o deskwind.o desktree.o deskfsel.o gemrslib.o rsload_portable.o \
               gemfslib.o gemformbutton.o gemwatch.o gemformkey.o gem_rsc.o gemobjop.o gemobjwalk.o gemobed.o gemgrlib.o \
               gemgraphutil.o gemfmerror.o gemfmalt.o
desk-prog-y += $(if $(CONF_WITH_LIBAES),gemobjtree.o)
ifeq ($(CONF_WITH_DYNAMIC_DESK),y)
desk-prog-y := $(filter-out deskentry.o doprintf.o memmove.o string.o,$(desk-prog-y))
endif
endif

else

obj-y += deskstart.o
obj-y += $(desk-objs)
obj-$(CONF_WITH_VDI_CICON_TEST) += cicontest_rsc.o

endif

# This one must be the last GEM object.
obj-y += endgem.o
