# Freestanding x32 userspace for the initial #660 milestone.  When another
# backend is ready, select its flags here and widen the Kconfig dependency.
LDSO_CFLAGS-$(ARCH_X86_64) = -mx32
LDSO_CFLAGS = $(LDSO_CFLAGS-y) -std=gnu90 -Os -ffreestanding -fPIC \
              -fno-stack-protector -Wall -Wextra -Werror -Ilib/ldso

obj/ldso/dynamic.o: lib/ldso/dynamic.c lib/ldso/dynamic.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LDSO_CFLAGS) -c $< -o $@

obj/ldso/relocate.o: lib/ldso/relocate.c lib/ldso/relocate.h lib/ldso/dynamic.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LDSO_CFLAGS) -c $< -o $@

LDSO_BACKEND-$(ARCH_X86_64) = lib/ldso/arch/x32/reloc.c
obj/ldso/backend.o: $(LDSO_BACKEND-y) lib/ldso/relocate.h lib/ldso/dynamic.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LDSO_CFLAGS) -c $< -o $@

obj/ldso/graph.o: lib/ldso/graph.c lib/ldso/graph.h lib/ldso/dynamic.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LDSO_CFLAGS) -c $< -o $@

LDSO_IMAGE_ABI-$(ARCH_X86_64) = lib/ldso/arch/x32/image_abi.c
obj/ldso/image_abi.o: $(LDSO_IMAGE_ABI-y) lib/ldso/image.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LDSO_CFLAGS) -c $< -o $@

obj/ldso/image.o: lib/ldso/image.c lib/ldso/image.h lib/ldso/dynamic.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LDSO_CFLAGS) -c $< -o $@

obj/ldso/libldso.a: obj/ldso/dynamic.o obj/ldso/relocate.o obj/ldso/backend.o obj/ldso/graph.o obj/ldso/image.o obj/ldso/image_abi.o
	$(CROSS_COMPILE)ar rcs $@ $^

all: obj/ldso/libldso.a

# Compiler-produced DSOs embedded only in the ring-3 self-test.
X32_PROBE_LDSO_HEADERS = lib/ldso/image.h lib/ldso/graph.h lib/ldso/relocate.h lib/ldso/dynamic.h
X32_PROBE_LDSO = tests/ldso_reloc/probe.c tests/ldso_reloc/blobs.S \
                 obj/ldso/test/libleaf.so obj/ldso/test/libtest.so obj/ldso/libldso.a

obj/ldso/test/libleaf.so: tests/ldso_reloc/leaf.c
	@mkdir -p $(@D)
	$(CC) $(LDSO_CFLAGS) -nostdlib -shared -Wl,--hash-style=sysv,-soname,libleaf.so $< -o $@

obj/ldso/test/libtest.so: tests/ldso_reloc/library.c obj/ldso/test/libleaf.so
	$(CC) $(LDSO_CFLAGS) -nostdlib -shared -Wl,--hash-style=sysv,-soname,libtest.so $< -Lobj/ldso/test -lleaf -o $@
