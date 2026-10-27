# Ordinary shared ELF, distinct from the historic fixed-image import stubs.
LIBAES_CFLAGS = $(LDSO_CFLAGS) -Iinclude -Iobj -Iutil
LIBAES_TRANSPORT-$(ARCH_X86_64) = lib/aes/arch/x32/transport.c
obj/libaes/application.o: lib/aes/application.c lib/aes/application.h include/gem.h lib/aes/transport.h include/libaes.h include/cuis.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/gemwindows.o: lib/aes/gemwindows.c lib/aes/application.h lib/aes/transport.h include/gem.h include/cuis.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/draw_string.o: util/string.c $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -ffunction-sections -fdata-sections -c $< -o $@
LIBAES_MEMORY-$(ARCH_X86_64) = util/arch/x86_64/memmove.c
obj/libaes/draw_memory.o: $(LIBAES_MEMORY-y) $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -ffunction-sections -fdata-sections -c $< -o $@
# Compile shared C units normally, binding their GSX state to a private context.
LIBAES_RENDER_OBJECTS = obj/libaes/gemicon.o obj/libaes/gemrender.o obj/libaes/gemrender_graphics.o obj/libaes/gemgraphutil.o
$(LIBAES_RENDER_OBJECTS): obj/libaes/%.o: aes/shared/%.c aes/shared/gemrender.h aes/shared/gemrender_binding.h lib/aes/render_bindings.h lib/aes/render_context.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Ilib/aes -Iaes -Iaes/shared -DAES_RENDER_BINDINGS='"render_bindings.h"' -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/gemobjwalk.o: aes/shared/gemobjwalk.c aes/shared/gemrender.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes -Iaes/shared -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/draw.o: lib/aes/draw.c aes/shared/gemrender.h lib/aes/render_context.h include/gem.h include/vdi_service.h lib/aes/transport.h lib/aes/application.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes -Iaes/shared -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/events.o: lib/aes/events.c lib/aes/application.h lib/aes/transport.h include/gem.h include/cuis.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/session.o: lib/aes/session.c lib/aes/transport.h include/libaes.h include/cuis.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/windows.o: lib/aes/windows.c lib/aes/transport.h include/libaes.h include/cuis.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/transport.o: $(LIBAES_TRANSPORT-y) lib/aes/transport.h include/vdi_service.h include/cuis.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/rectfunc.o: util/rectfunc.c include/rectfunc.h include/grect.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/objtree.o: aes/shared/gemobjtree.c aes/shared/gemobjtree.h include/obdefs.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes -Iaes/shared -c $< -o $@
obj/libaes/objects.o: lib/aes/objects.c aes/shared/gemobjtree.h include/libaes.h include/obdefs.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/gemlayout.o: aes/shared/gemlayout.c aes/shared/gemlayout.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes -Iaes/shared -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/layout.o: lib/aes/layout.c lib/aes/layout.h aes/shared/gemlayout.h include/gem.h include/vdi_service.h lib/aes/transport.h lib/aes/application.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes -Iaes/shared -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/gemformkey.o: aes/shared/gemformkey.c aes/shared/gemformkey.h lib/aes/formkey_bindings.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Ilib/aes -Iaes -Iaes/shared -DAES_FORMKEY_BINDINGS='"formkey_bindings.h"' -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/formkeys.o: lib/aes/formkeys.c lib/aes/layout.h lib/aes/application.h aes/shared/gemformkey.h include/gem.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes/shared -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/libaes.so: obj/libaes/formkeys.o obj/libaes/gemformkey.o obj/libaes/gemlayout.o obj/libaes/layout.o $(LIBAES_RENDER_OBJECTS) obj/libaes/gemobjwalk.o obj/libaes/draw_string.o obj/libaes/draw_memory.o obj/libaes/draw.o obj/libaes/gemwindows.o obj/libaes/events.o obj/libaes/application.o obj/libaes/windows.o obj/libaes/session.o obj/libaes/transport.o obj/libaes/rectfunc.o obj/libaes/objtree.o obj/libaes/objects.o lib/aes/exports.map
	$(CC) $(LDSO_LDFLAGS) $(LIBAES_CFLAGS) -nostdlib -shared \
	  -Wl,--gc-sections,--no-undefined,--hash-style=sysv,-z,now,-z,noexecstack,-soname,libaes.so \
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

obj/libaes/gem_draw_probe.o: tests/cuis/gem_draw_probe.c tests/cuis/gem_draw_probe.h include/libaes.h include/vdi_service.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/GEMDRAW.TOS: tests/cuis/gem_draw.c tests/cuis/form_keys.c tests/cuis/form_keys.h tests/cuis/gem_layout.c tests/cuis/gem_layout.h tests/cuis/mono_objects.c tests/cuis/mono_objects.h tests/cuis/gem_draw_probe.h obj/libaes/gem_draw_probe.o obj/ldso/dyncrt.o obj/libaes/libaes.so obj/ldso/libptos.so include/gem.h
	$(CC) $(LDSO_APP_LDFLAGS) $(LDSO_CLIENT_CFLAGS) -Iinclude -Iobj \
	  -o $@ $(filter %.c %.o,$^) -Lobj/libaes -laes -Lobj/ldso -lptos
ifeq ($(CONF_WITH_X86_64_MEMTEST),y)
all: obj/libaes/GEMDRAW.TOS
endif
