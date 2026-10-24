# Ordinary shared ELF, distinct from the historic fixed-image import stubs.
LIBAES_CFLAGS = $(LDSO_CFLAGS) -Iinclude -Iobj -Iutil
LIBAES_TRANSPORT-$(ARCH_X86_64) = lib/aes/arch/x32/transport.c
obj/libaes/session.o: lib/aes/session.c lib/aes/transport.h include/libaes.h include/cuis.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/transport.o: $(LIBAES_TRANSPORT-y) lib/aes/transport.h include/cuis.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/rectfunc.o: util/rectfunc.c include/rectfunc.h include/grect.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LIBAES_CFLAGS) -c $< -o $@
obj/libaes/libaes.so: obj/libaes/session.o obj/libaes/transport.o obj/libaes/rectfunc.o lib/aes/exports.map
	$(CC) $(LDSO_LDFLAGS) $(LIBAES_CFLAGS) -nostdlib -shared \
	  -Wl,--no-undefined,--hash-style=sysv,-z,now,-z,noexecstack,-soname,libaes.so \
	  -Wl,--version-script,lib/aes/exports.map -o $@ $(filter %.o,$^)
$(LDSO_RUNTIME_DIR)/LIB/X32/LIBAES.SO: obj/libaes/libaes.so
	@mkdir -p $(@D)
	cp $< $@
LDSO_RUNTIME_FILES += $(LDSO_RUNTIME_DIR)/LIB/X32/LIBAES.SO
$(LDSO_RUNTIME_DIR): $(LDSO_RUNTIME_DIR)/LIB/X32/LIBAES.SO
all: $(LDSO_RUNTIME_DIR)/LIB/X32/LIBAES.SO
