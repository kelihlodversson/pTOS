# Ordinary shared ELF, distinct from the historic fixed-image import stubs.
LIBAES_CFLAGS = $(LDSO_CFLAGS) -DOB_WALK_COORD=LONG -Iinclude -Iobj -Iutil
LIBAES_TRANSPORT-$(ARCH_X86_64) = lib/aes/arch/x32/transport.c
LIBAES_ARCH_HEADERS-$(ARCH_X86_64) = -Iinclude/arch/x86_64
obj/libaes/application.o: lib/aes/application.c lib/aes/desktop.h lib/aes/graphics.h lib/aes/menubar.h lib/aes/application.h include/gem.h lib/aes/transport.h include/libaes.h include/cuis.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/gemwindows.o: lib/aes/gemwindows.c lib/aes/desktop.h lib/aes/frame_colours.h lib/aes/graphics.h aes/shared/gemslidegeom.h aes/shared/gemsizegeom.h aes/shared/gemmovegeom.h lib/aes/resource_context.h lib/aes/frames.h lib/aes/layout.h aes/shared/gemframegeom.h lib/aes/application.h lib/aes/transport.h include/gem.h include/cuis.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes/shared -c $< -o $@
obj/libaes/draw_string.o: util/string.c $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -ffunction-sections -fdata-sections -c $< -o $@
LIBAES_MEMORY-$(ARCH_X86_64) = util/arch/x86_64/memmove.c
obj/libaes/draw_memory.o: $(LIBAES_MEMORY-y) $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -ffunction-sections -fdata-sections -c $< -o $@
# Compile shared C units normally, binding their GSX state to a private context.
LIBAES_RENDER_OBJECTS = obj/libaes/gemobjbounds.o obj/libaes/gemcicon.o obj/libaes/gemicon.o obj/libaes/gemrender.o obj/libaes/gemrender_graphics.o obj/libaes/gemgraphutil.o
$(LIBAES_RENDER_OBJECTS): obj/libaes/%.o: aes/shared/%.c aes/shared/gemobjbounds.h aes/shared/gemrender.h aes/shared/gemcicon.h aes/shared/gemrender_binding.h lib/aes/render_bindings.h lib/aes/render_context.h lib/aes/style.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Ilib/aes -Iaes -Iaes/shared -DAES_RENDER_BINDINGS='"render_bindings.h"' -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/gemobjwalk.o: aes/shared/gemobjwalk.c aes/gemobjop.h aes/shared/gemrender.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes -Iaes/shared -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/draw.o: lib/aes/draw.c aes/shared/gemslidegeom.h aes/shared/gemobjbounds.h aes/shared/gemsizegeom.h aes/shared/gemframegeom.h lib/aes/frames.h aes/shared/gemmovegeom.h aes/gemobjop.h lib/aes/style.h aes/shared/gemcicon.h include/vdi_colmap.h lib/aes/resource_context.h aes/shared/gemrender.h lib/aes/render_context.h include/gem.h include/vdi_service.h lib/aes/transport.h lib/aes/application.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes -Iaes/shared -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/events.o: lib/aes/events.c lib/aes/menubar.h lib/aes/application.h lib/aes/transport.h include/gem.h include/cuis.h $(AUTOCONF_H)
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
obj/libaes/objects.o: lib/aes/objects.c aes/shared/gemobjbounds.h aes/shared/gemobjtree.h include/libaes.h include/obdefs.h $(AUTOCONF_H)
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
LIBAES_FORM_OBJECTS = obj/libaes/gemformbutton.o obj/libaes/gemwatch.o
$(LIBAES_FORM_OBJECTS): obj/libaes/%.o: aes/shared/%.c aes/shared/gemwatch.h lib/aes/form_bindings.h lib/aes/form_context.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Ilib/aes -Iaes -Iaes/shared -DAES_FORM_BINDINGS='"form_bindings.h"' -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/formbutton.o: lib/aes/formbutton.c aes/shared/gemobjbounds.h aes/gemobjop.h lib/aes/render_context.h lib/aes/style.h lib/aes/form_context.h lib/aes/layout.h lib/aes/application.h include/gem.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes/shared -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/gemedit.o: aes/shared/gemedit.c lib/aes/edit_bindings.h lib/aes/render_context.h lib/aes/style.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Ilib/aes -Iaes -Iaes/shared -DAES_EDIT_BINDINGS='"edit_bindings.h"' -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/gemformdo.o: aes/shared/gemformdo.c lib/aes/modal_bindings.h lib/aes/modal_context.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Ilib/aes -Iaes -Iaes/shared -DAES_MODAL_BINDINGS='"modal_bindings.h"' -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/forms.o: lib/aes/forms.c lib/aes/modal_context.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes/shared -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/gemformanim.o: aes/shared/gemformanim.c lib/aes/anim_bindings.h lib/aes/render_context.h lib/aes/style.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Ilib/aes -Iaes -Iaes/shared -DAES_ANIM_BINDINGS='"anim_bindings.h"' -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/gemalert.o: aes/shared/gemalert.c lib/aes/alert_bindings.h lib/aes/alert_context.h $(AUTOCONF_H) aes/shared/gem_rsc.h
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Ilib/aes -Iaes -Iaes/shared -DAES_ALERT_BINDINGS='"alert_bindings.h"' -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/gemformerror.o: aes/shared/gemformerror.c lib/aes/error_bindings.h lib/aes/alert_context.h $(AUTOCONF_H) aes/shared/gem_rsc.h
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Ilib/aes -Iaes -Iaes/shared -DAES_ERROR_BINDINGS='"error_bindings.h"' -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/alerts.o: lib/aes/alerts.c lib/aes/alert_context.h $(AUTOCONF_H) aes/shared/gem_rsc.h
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes -Iaes/shared -ffunction-sections -fdata-sections -c $< -o $@
# Build just the original generated alert icons/strings, from the same source
# as the native resource objects. Never include a C implementation as text.
obj/libaes/form_resources.o: aes/shared/gem_rsc.c aes/shared/gem_rsc.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes -Iaes/shared -DAES_RESOURCE_FSEL -DAES_RESOURCE_STRINGS_LOCAL -ffunction-sections -fdata-sections -c $< -o $@
LIBAES_RESOURCE_OBJECTS = obj/libaes/gemrcfix.o obj/libaes/resource_loader.o obj/libaes/gemrsaddr.o obj/libaes/resources.o
obj/libaes/resource_loader.o: aes/shared/rsload_portable.c lib/aes/resource_bindings.h lib/aes/resource_context.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Ilib/aes -Iaes -Iaes/shared -DAES_RSC_BINDINGS='"resource_bindings.h"' -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/gemrsaddr.o: aes/shared/gemrsaddr.c lib/aes/resource_bindings.h lib/aes/resource_context.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Ilib/aes -Iaes -Iaes/shared -DAES_RSC_BINDINGS='"resource_bindings.h"' -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/gemrcfix.o: aes/shared/gemrcfix.c aes/shared/gemrcfix.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes/shared -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/resources.o: lib/aes/resources.c lib/ldso/launch.h aes/shared/gemrcfix.h lib/aes/resource_context.h lib/aes/application.h lib/aes/layout.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Ilib/ldso/client -Iaes -Iaes/shared -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/gemframe.o: aes/shared/gemframe.c aes/shared/gemframe.h aes/shared/gemframeids.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes/shared -DAES_FRAME_TEMPLATE -c $< -o $@
obj/libaes/gemslidegeom.o: aes/shared/gemslidegeom.c aes/shared/gemslidegeom.h aes/shared/gemframe.h aes/shared/gemframeids.h include/obdefs.h include/aesdefs.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes/shared -c $< -o $@
obj/libaes/gemsizegeom.o: aes/shared/gemsizegeom.c aes/shared/gemsizegeom.h aes/shared/gemframegeom.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes/shared -c $< -o $@
obj/libaes/gemmovegeom.o: aes/shared/gemmovegeom.c aes/shared/gemmovegeom.h aes/shared/gemframegeom.h include/obdefs.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes/shared -c $< -o $@
obj/libaes/frames.o: lib/aes/frames.c lib/aes/frame_colours.h aes/shared/gemframegeom.h aes/shared/gemobjbounds.h lib/aes/frames.h lib/aes/layout.h aes/shared/gemframe.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes/shared -c $< -o $@
obj/libaes/gemframegeom.o: aes/shared/gemframegeom.c aes/shared/gemframegeom.h aes/shared/gemframeids.h aes/shared/gemobjbounds.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes/shared -c $< -o $@
obj/libaes/libaes.so: obj/libaes/children.o obj/libaes/shell_metadata.o obj/libaes/shell_buffer.o obj/libaes/shell.o obj/libaes/gemshquery.o obj/libaes/fsel.o obj/libaes/gemfslib.o obj/libaes/selector_optimize.o obj/libaes/selector_miscutil.o obj/libaes/desktop.o obj/libaes/animations.o obj/libaes/tracking.o obj/libaes/gemgrtrack.o obj/libaes/graphics.o obj/libaes/mforms.o obj/libaes/gemslidegeom.o obj/libaes/gemsizegeom.o obj/libaes/gemmovegeom.o obj/libaes/frames.o obj/libaes/gemframe.o obj/libaes/gemframegeom.o obj/libaes/menubar.o obj/libaes/gemmenubar.o obj/libaes/popup.o obj/libaes/gemmenupopup.o obj/libaes/menus.o obj/libaes/gemmenuchange.o $(LIBAES_RESOURCE_OBJECTS) obj/ldso/libptos.so obj/libaes/form_resources.o obj/libaes/alerts.o obj/libaes/gemalert.o obj/libaes/gemformerror.o obj/libaes/gemformanim.o obj/libaes/gemformdo.o obj/libaes/forms.o obj/libaes/gemedit.o $(LIBAES_FORM_OBJECTS) obj/libaes/formbutton.o obj/libaes/formkeys.o obj/libaes/gemformkey.o obj/libaes/gemlayout.o obj/libaes/layout.o $(LIBAES_RENDER_OBJECTS) obj/libaes/gemobjwalk.o obj/libaes/draw_string.o obj/libaes/draw_memory.o obj/libaes/draw.o obj/libaes/gemwindows.o obj/libaes/events.o obj/libaes/application.o obj/libaes/windows.o obj/libaes/session.o obj/libaes/transport.o obj/libaes/rectfunc.o obj/libaes/objtree.o obj/libaes/objects.o lib/aes/exports.map
	$(CC) $(LDSO_LDFLAGS) $(LIBAES_CFLAGS) -nostdlib -shared \
	  -Wl,--gc-sections,--no-undefined,--hash-style=sysv,-z,now,-z,noexecstack,-soname,libaes.so,-init,aes_library_init \
	  -Wl,--version-script,lib/aes/exports.map -o $@ $(filter %.o,$^) -Lobj/ldso -lptos
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

obj/libaes/GEM.TOS: tests/cuis/gem.c obj/libaes/FIXED.PRG tests/cuis/gem_exit.c tests/cuis/gem_exit.h tests/cuis/gem_launch.c tests/cuis/gem_launch.h tests/cuis/gem_metadata.c tests/cuis/gem_metadata.h tests/cuis/gem_buffer.c tests/cuis/gem_buffer.h tests/cuis/gem_shell.c tests/cuis/gem_shell.h obj/ldso/dyncrt.o obj/libaes/libaes.so obj/ldso/libptos.so include/gem.h
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

obj/libaes/GEMFOC.TOS: tests/cuis/gem_focus.c obj/libaes/gem_draw_probe.o obj/ldso/dyncrt.o obj/libaes/libaes.so obj/ldso/libptos.so include/gem.h
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
obj/libaes/GEMDRAW.TOS: tests/cuis/gem_selector.c tests/cuis/gem_selector.h tests/cuis/box_animations.c tests/cuis/box_animations.h tests/cuis/gem_tracking.c tests/cuis/gem_tracking.h tests/cuis/gem_menubar.c tests/cuis/gem_menubar.h tests/cuis/popup_menu.c tests/cuis/popup_menu.h tests/cuis/menu_items.c tests/cuis/menu_items.h tests/cuis/three_d.c tests/cuis/three_d.h tests/cuis/colour_objects.c tests/cuis/colour_objects.h tests/cuis/gem_resources.c tests/cuis/gem_resources.h tests/cuis/gem_forms.c tests/cuis/gem_forms.h tests/cuis/gem_draw.c tests/cuis/gem_buttons.c tests/cuis/gem_buttons.h tests/cuis/form_keys.c tests/cuis/form_keys.h tests/cuis/gem_layout.c tests/cuis/gem_layout.h tests/cuis/mono_objects.c tests/cuis/mono_objects.h tests/cuis/gem_draw_probe.h obj/libaes/gem_draw_probe.o obj/ldso/dyncrt.o obj/libaes/libaes.so obj/ldso/libptos.so include/gem.h
	$(CC) $(LDSO_APP_LDFLAGS) $(LDSO_CLIENT_CFLAGS) -Iinclude -Iobj \
	  -o $@ $(filter %.c %.o,$^) -Lobj/libaes -laes -Lobj/ldso -lptos
ifeq ($(CONF_WITH_X86_64_MEMTEST),y)
all: obj/libaes/GEMDRAW.TOS
endif

obj/libaes/GEMTEST.RSC: tools/make-cuis-resource.py
	@mkdir -p $(@D)
	python3 $< $@
ifeq ($(CONF_WITH_X86_64_MEMTEST),y)
all: obj/libaes/GEMTEST.RSC
endif

obj/libaes/menus.o: lib/aes/menus.c lib/aes/menu_bindings.h lib/aes/application.h include/gem.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/gemmenuchange.o: aes/shared/gemmenuchange.c lib/aes/menu_bindings.h include/gem.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Ilib/aes -Iaes -DAES_MENU_BINDINGS='"menu_bindings.h"' -c $< -o $@

obj/libaes/popup.o: lib/aes/popup.c include/gemmenu.h include/gem.h aes/shared/gemmenupopup.h aes/shared/gemobjbounds.h lib/aes/resource_context.h lib/aes/application.h lib/aes/layout.h lib/aes/transport.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes -Iaes/shared -c $< -o $@
obj/libaes/gemmenupopup.o: aes/shared/gemmenupopup.c aes/shared/gemmenupopup.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes/shared -c $< -o $@

obj/libaes/menubar.o: lib/aes/menubar.c lib/aes/menubar.h lib/aes/application.h lib/aes/transport.h lib/aes/layout.h lib/aes/resource_context.h aes/shared/gemobjtree.h aes/shared/gemobjbounds.h aes/shared/gemmenubar.h include/vdi_service.h include/gem.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes -Iaes/shared -c $< -o $@
obj/libaes/gemmenubar.o: aes/shared/gemmenubar.c aes/shared/gemmenubar.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes/shared -c $< -o $@

obj/libaes/mforms.c obj/libaes/mforms.h &: mrd aes/mform.rsc aes/mform.def
	@mkdir -p obj/libaes
	./mrd -pmform aes/mform obj/libaes/mforms
obj/libaes/mforms.o: obj/libaes/mforms.c obj/libaes/mforms.h include/gem.h include/mform.h $(AUTOCONF_H)
	$(CC) $(LIBAES_CFLAGS) -Iobj/libaes -include gem.h -c $< -o $@
obj/libaes/graphics.o: lib/aes/graphics.c lib/aes/graphics.h lib/aes/layout.h lib/aes/application.h lib/aes/transport.h include/gem.h include/mform.h include/vdi_service.h include/cuis.h obj/libaes/mforms.h $(AUTOCONF_H)
	$(CC) $(LIBAES_CFLAGS) -Iobj/libaes -c $< -o $@

obj/libaes/tracking.o: lib/aes/tracking.c lib/aes/tracking.h include/gem.h aes/shared/gemobjbounds.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes -Iaes/shared -c $< -o $@
obj/libaes/gemgrtrack.o: aes/shared/gemgrtrack.c lib/aes/track_bindings.h lib/aes/tracking.h lib/aes/render_bindings.h lib/aes/render_context.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Ilib/aes -Iaes -Iaes/shared -DAES_GRTRACK_BINDINGS='"track_bindings.h"' -c $< -o $@
obj/libaes/draw.o: lib/aes/tracking.h

obj/libaes/animations.o: lib/aes/animations.c lib/aes/animation.h lib/aes/application.h lib/aes/transport.h include/gem.h include/cuis.h include/gemerror.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/draw.o obj/libaes/forms.o obj/libaes/gemformanim.o: lib/aes/animation.h
obj/libaes/gemformanim.o: lib/aes/render_bindings.h

obj/libaes/desktop.o: lib/aes/desktop.c lib/aes/desktop.h lib/aes/application.h lib/aes/transport.h lib/aes/resource_context.h lib/aes/layout.h include/gem.h include/cuis.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes -Iaes/shared -ffunction-sections -fdata-sections -c $< -o $@

obj/libaes/fsel.o: lib/aes/fsel.c lib/aes/fsel_context.h lib/aes/transport.h aes/shared/gem_rsc.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -DAES_RESOURCE_FSEL -Iaes -Iaes/shared -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/gemfslib.o: aes/shared/gemfslib.c lib/aes/fsel_bindings.h lib/aes/fsel_context.h aes/shared/gem_rsc.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) $(LIBAES_ARCH_HEADERS-y) -DAES_RESOURCE_FSEL -Ilib/aes -Iaes -Iaes/shared -DAES_FSEL_BINDINGS='"fsel_bindings.h"' -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/selector_optimize.o: util/optimize.c $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/selector_miscutil.o: util/miscutil.c include/i18nconf.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -ffunction-sections -fdata-sections -c $< -o $@

obj/libaes/shell.o: lib/aes/shell.c lib/ldso/launch.h lib/aes/shell_context.h lib/aes/resource_context.h lib/aes/transport.h lib/aes/application.h lib/ldso/client/ptos.h include/gem.h include/bdosdefs.h include/userptr.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Ilib/ldso/client -Iaes -Iaes/shared -ffunction-sections -fdata-sections -c $< -o $@
obj/libaes/gemshquery.o: aes/shared/gemshquery.c aes/shared/gemshquery.h lib/aes/shell_bindings.h lib/aes/shell_context.h lib/aes/resource_context.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Ilib/aes -Iaes -Iaes/shared -DAES_SHELL_BINDINGS='"shell_bindings.h"' -ffunction-sections -fdata-sections -c $< -o $@

obj/libaes/shell_buffer.o: lib/aes/shell_buffer.c lib/aes/application.h lib/aes/transport.h lib/aes/resource_context.h include/cuis.h include/gem.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -Iaes -Iaes/shared -c $< -o $@

obj/libaes/shell_metadata.o: lib/aes/shell_metadata.c lib/aes/application.h lib/aes/transport.h include/cuis.h include/gem.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@

obj/libaes/children.o: lib/aes/children.c lib/aes/application.h lib/aes/transport.h include/cuis.h include/gem.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@

obj/libaes/FIXED.PRG: tests/cuis/fixed_child.c
	@mkdir -p $(@D)
	$(X32_CC) $(X32_CFLAGS) $(X32_LDFLAGS_NOQ) -Os -fno-stack-protector -Wl,-e,_start -o $@ $<
