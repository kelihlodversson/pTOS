# Ordinary shared ELF, distinct from the historic fixed-image import stubs.
LIBAES_CFLAGS = $(LDSO_CFLAGS) -Iinclude -Iobj -Iutil
LIBAES_TRANSPORT-$(ARCH_X86_64) = lib/aes/arch/x32/transport.c
obj/libaes/application.o: lib/aes/application.c lib/aes/application.h include/gem.h lib/aes/transport.h include/libaes.h include/cuis.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/gemwindows.o: lib/aes/gemwindows.c lib/aes/application.h lib/aes/transport.h include/gem.h include/cuis.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/draw.o: lib/aes/draw.c include/gem.h include/vdi_service.h lib/aes/transport.h lib/aes/application.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/events.o: lib/aes/events.c lib/aes/application.h lib/aes/transport.h include/gem.h include/cuis.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/session.o: lib/aes/session.c lib/aes/transport.h include/libaes.h include/cuis.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/windows.o: lib/aes/windows.c lib/aes/transport.h include/libaes.h include/cuis.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/transport.o: $(LIBAES_TRANSPORT-y) lib/aes/transport.h include/cuis.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/rectfunc.o: util/rectfunc.c include/rectfunc.h include/grect.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/objtree.o: aes/gemobjtree.c aes/gemobjtree.h include/obdefs.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes -c $< -o $@
obj/libaes/objects.o: lib/aes/objects.c aes/gemobjtree.h include/libaes.h include/obdefs.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/libaes.so: obj/libaes/draw.o obj/libaes/gemwindows.o obj/libaes/events.o obj/libaes/application.o obj/libaes/windows.o obj/libaes/session.o obj/libaes/transport.o obj/libaes/rectfunc.o obj/libaes/objtree.o obj/libaes/objects.o lib/aes/exports.map
	$(CC) $(LDSO_LDFLAGS) $(LIBAES_CFLAGS) -nostdlib -shared \
	  -Wl,--no-undefined,--hash-style=sysv,-z,now,-z,noexecstack,-soname,libaes.so \
	  -Wl,--version-script,lib/aes/exports.map -o $@ $(filter %.o,$^)
$(LDSO_RUNTIME_DIR)/LIB/X32/LIBAES.SO: obj/libaes/libaes.so
	@mkdir -p $(@D)
	cp $< $@
LDSO_RUNTIME_FILES += $(LDSO_RUNTIME_DIR)/LIB/X32/LIBAES.SO
$(LDSO_RUNTIME_DIR): $(LDSO_RUNTIME_DIR)/LIB/X32/LIBAES.SO
all: $(LDSO_RUNTIME_DIR)/LIB/X32/LIBAES.SO

obj/libaes/CUIS.TOS: tests/cuis/program.c tests/cuis/objects.c tests/cuis/drawing.c tests/ldso_client/nested.S obj/ldso/dyncrt.o \
                     obj/libaes/libaes.so obj/ldso/libptos.so include/libaes.h include/cuis.h
	$(CC) $(LDSO_APP_LDFLAGS) $(LDSO_CLIENT_CFLAGS) -Iinclude -Iobj \
	  -o $@ $(filter %.c %.S %.o,$^) -Lobj/libaes -laes -Lobj/ldso -lptos
ifeq ($(CONF_WITH_X86_64_MEMTEST),y)
all: obj/libaes/CUIS.TOS
endif

obj/libaes/CUISCHED.TOS: tests/cuis/scheduled.c tests/ldso_client/nested.S obj/ldso/dyncrt.o \
                         obj/libaes/libaes.so obj/ldso/libptos.so include/libaes.h include/cuis.h
	$(CC) $(LDSO_APP_LDFLAGS) $(LDSO_CLIENT_CFLAGS) -Iinclude -Iobj \
	  -o $@ $(filter %.c %.S %.o,$^) -Lobj/libaes -laes -Lobj/ldso -lptos
ifeq ($(CONF_WITH_X86_64_MEMTEST),y)
all: obj/libaes/CUISCHED.TOS
endif

obj/libaes/ASYNC.TOS: tests/cuis/async.c obj/ldso/dyncrt.o obj/libaes/libaes.so obj/ldso/libptos.so include/libaes.h
	$(CC) $(LDSO_APP_LDFLAGS) $(LDSO_CLIENT_CFLAGS) -Iinclude -Iobj \
	  -o $@ $(filter %.c %.o,$^) -Lobj/libaes -laes -Lobj/ldso -lptos
ifeq ($(CONF_WITH_X86_64_MEMTEST),y)
all: obj/libaes/ASYNC.TOS
endif

obj/libaes/GEM.TOS: tests/cuis/gem.c obj/ldso/dyncrt.o obj/libaes/libaes.so obj/ldso/libptos.so include/gem.h
	$(CC) $(LDSO_APP_LDFLAGS) $(LDSO_CLIENT_CFLAGS) -Iinclude -Iobj \
	  -o $@ $(filter %.c %.o,$^) -Lobj/libaes -laes -Lobj/ldso -lptos
ifeq ($(CONF_WITH_X86_64_MEMTEST),y)
all: obj/libaes/GEM.TOS
endif

obj/libaes/GEMMSG.TOS: tests/cuis/gem_messages.c obj/ldso/dyncrt.o obj/libaes/libaes.so obj/ldso/libptos.so include/gem.h
	$(CC) $(LDSO_APP_LDFLAGS) $(LDSO_CLIENT_CFLAGS) -Iinclude -Iobj \
	  -o $@ $(filter %.c %.o,$^) -Lobj/libaes -laes -Lobj/ldso -lptos
ifeq ($(CONF_WITH_X86_64_MEMTEST),y)
all: obj/libaes/GEMMSG.TOS
endif

obj/libaes/GEMEVNT.TOS: tests/cuis/gem_events.c obj/ldso/dyncrt.o obj/libaes/libaes.so obj/ldso/libptos.so include/gem.h
	$(CC) $(LDSO_APP_LDFLAGS) $(LDSO_CLIENT_CFLAGS) -Iinclude -Iobj \
	  -o $@ $(filter %.c %.o,$^) -Lobj/libaes -laes -Lobj/ldso -lptos
ifeq ($(CONF_WITH_X86_64_MEMTEST),y)
all: obj/libaes/GEMEVNT.TOS
endif

obj/libaes/GEMWIN.TOS: tests/cuis/control_message.h tests/cuis/gem_windows.c tests/cuis/window_api.c obj/ldso/dyncrt.o obj/libaes/libaes.so obj/ldso/libptos.so include/gem.h
	$(CC) $(LDSO_APP_LDFLAGS) $(LDSO_CLIENT_CFLAGS) -Iinclude -Iobj \
	  -o $@ $(filter-out tests/cuis/window_api.c,$(filter %.c %.o,$^)) -Lobj/libaes -laes -Lobj/ldso -lptos
ifeq ($(CONF_WITH_X86_64_MEMTEST),y)
all: obj/libaes/GEMWIN.TOS
endif

obj/libaes/GEMUPD.TOS: tests/cuis/control_message.h tests/cuis/gem_update.c obj/ldso/dyncrt.o obj/libaes/libaes.so obj/ldso/libptos.so include/gem.h
	$(CC) $(LDSO_APP_LDFLAGS) $(LDSO_CLIENT_CFLAGS) -Iinclude -Iobj \
	  -o $@ $(filter %.c %.o,$^) -Lobj/libaes -laes -Lobj/ldso -lptos
ifeq ($(CONF_WITH_X86_64_MEMTEST),y)
all: obj/libaes/GEMUPD.TOS
endif

obj/libaes/GEMFOC.TOS: tests/cuis/gem_focus.c obj/ldso/dyncrt.o obj/libaes/libaes.so obj/ldso/libptos.so include/gem.h
	$(CC) $(LDSO_APP_LDFLAGS) $(LDSO_CLIENT_CFLAGS) -Iinclude -Iobj \
	  -o $@ $(filter %.c %.o,$^) -Lobj/libaes -laes -Lobj/ldso -lptos
ifeq ($(CONF_WITH_X86_64_MEMTEST),y)
all: obj/libaes/GEMFOC.TOS
endif

obj/libaes/GEMINP.TOS: tests/cuis/gem_input.c obj/ldso/dyncrt.o obj/libaes/libaes.so obj/ldso/libptos.so include/gem.h
	$(CC) $(LDSO_APP_LDFLAGS) $(LDSO_CLIENT_CFLAGS) -Iinclude -Iobj \
	  -o $@ $(filter %.c %.o,$^) -Lobj/libaes -laes -Lobj/ldso -lptos
ifeq ($(CONF_WITH_X86_64_MEMTEST),y)
all: obj/libaes/GEMINP.TOS
endif

obj/libaes/GEMMCTL.TOS: tests/cuis/control_message.h tests/cuis/gem_mouse.c obj/ldso/dyncrt.o obj/libaes/libaes.so obj/ldso/libptos.so include/gem.h
	$(CC) $(LDSO_APP_LDFLAGS) $(LDSO_CLIENT_CFLAGS) -Iinclude -Iobj \
	  -o $@ $(filter %.c %.o,$^) -Lobj/libaes -laes -Lobj/ldso -lptos
ifeq ($(CONF_WITH_X86_64_MEMTEST),y)
all: obj/libaes/GEMMCTL.TOS
endif
