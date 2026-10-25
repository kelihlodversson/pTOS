#
# bdos/build.mk - objects making up GEMDOS
#

obj-y += bdosmain.o console.o fsbuf.o fsdir.o fsdrive.o fsfat.o fsglob.o \
	 fshand.o fsio.o fsmain.o fsopnclo.o iumem.o kpgmld.o kproc.o osmem.o \
	 proc.o time.o umem.o rwa.o launch.o
obj-$(CONF_WITH_ELF_LOADER) += elfld.o
obj-$(CONF_WITH_USER_COPY) += uaccess.o
obj-$(CONF_WITH_USER_CALLBACKS) += user_callback.o callback_entry.o

# Ssystem() (bdos/ssystem.c) is needed by every non-m68k/ColdFire arch --
# see the comment in osif() (bdos/bdosmain.c).
obj-$(CONF_WITH_SSYSTEM) += ssystem.o
obj-$(CONF_WITH_KCALL) += kcall.o

# the file calls of kernel code (the scalable fonts' font files; Fopen() & co.
# of x86-64 kernel code)
ifneq ($(CONF_WITH_KCALL)$(CONF_WITH_VDI_SCALABLE_FONTS)$(CONF_WITH_ELF_SHARED),)
obj-y += kfile.o
endif
obj-$(CONF_WITH_KPROC) += mintproc.o
obj-$(CONF_WITH_GEMDOS_SHUTDOWN) += xshutdown.o

# The kernel shares only load-plan validation and stack construction with
# ld.so, never its metadata resolver, relocation engine or dependency graph.
obj-$(CONF_WITH_ELF_SHARED) += elfinterp.o elfimage.o elfimageabi.o elfstartup.o

obj/elfimage.o: lib/ldso/image_plan.c lib/ldso/image.h $(AUTOCONF_H)
	$(CC) $(CFLAGS) -Ilib/ldso -MMD -MP -c $< -o $@
ELFIMAGE_ABI-$(ARCH_X86_64) = lib/ldso/arch/x32/image_abi.c
obj/elfimageabi.o: $(ELFIMAGE_ABI-y) lib/ldso/image.h $(AUTOCONF_H)
	$(CC) $(CFLAGS) -Ilib/ldso -MMD -MP -c $< -o $@
obj/elfstartup.o: lib/ldso/startup.c lib/ldso/startup.h $(AUTOCONF_H)
	$(CC) $(CFLAGS) -Ilib/ldso -MMD -MP -c $< -o $@

obj-$(CONF_WITH_MMAP) += fileback.o

# Native kernel tasks for external libaes, independent of kernel AES.
obj-$(CONF_WITH_CUIS) += native_sched.o native_sched_entry.o
