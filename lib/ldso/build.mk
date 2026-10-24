# Freestanding x32 userspace for the initial #660 milestone.  When another
# backend is ready, select its flags here and widen the Kconfig dependency.
LDSO_CFLAGS-$(ARCH_X86_64) = -mx32
LDSO_LDFLAGS-$(ARCH_X86_64) = -Wl,-m,elf32_x86_64
LDSO_LDFLAGS = $(LDSO_LDFLAGS-y)
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

obj/ldso/image_plan.o: lib/ldso/image_plan.c lib/ldso/image.h lib/ldso/dynamic.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LDSO_CFLAGS) -c $< -o $@

obj/ldso/startup.o: lib/ldso/startup.c lib/ldso/startup.h lib/ldso/dynamic.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LDSO_CFLAGS) -c $< -o $@

obj/ldso/image.o: lib/ldso/image.c lib/ldso/image.h lib/ldso/dynamic.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LDSO_CFLAGS) -c $< -o $@

obj/ldso/libldso.a: obj/ldso/dynamic.o obj/ldso/relocate.o obj/ldso/backend.o obj/ldso/graph.o obj/ldso/image.o obj/ldso/image_abi.o obj/ldso/image_plan.o obj/ldso/startup.o
	$(CROSS_COMPILE)ar rcs $@ $^

all: obj/ldso/libldso.a

# Compiler-produced DSOs embedded only in the ring-3 self-test.
X32_PROBE_LDSO_HEADERS = lib/ldso/image.h lib/ldso/graph.h lib/ldso/relocate.h lib/ldso/dynamic.h
X32_PROBE_LDSO = tests/ldso_reloc/probe.c tests/ldso_reloc/blobs.S \
                 obj/ldso/test/libleaf.so obj/ldso/test/libtest.so obj/ldso/libldso.a

obj/ldso/test/libleaf.so: tests/ldso_reloc/leaf.c
	@mkdir -p $(@D)
	$(CC) $(LDSO_LDFLAGS) $(LDSO_CFLAGS) -nostdlib -shared -Wl,--hash-style=sysv,-soname,libleaf.so $< -o $@

obj/ldso/test/libtest.so: tests/ldso_reloc/library.c obj/ldso/test/libleaf.so
	$(CC) $(LDSO_LDFLAGS) $(LDSO_CFLAGS) -nostdlib -shared -Wl,--hash-style=sysv,-soname,libtest.so $< -Lobj/ldso/test -lleaf -o $@

obj/ldso/test/probe.so: tests/interp_probe/start.S tests/interp_probe/check.c
	@mkdir -p $(@D)
	$(CC) $(LDSO_LDFLAGS) $(LDSO_CFLAGS) -fvisibility=hidden -nostdlib -shared -Wl,--hash-style=sysv,-e,_start -o $@ $^

obj/ldso/test/dyntest.elf: tests/interp_probe/program.S obj/ldso/test/libtest.so
	$(CC) $(LDSO_LDFLAGS) -mx32 -nostdlib -no-pie -Wl,--no-as-needed,--hash-style=sysv,-e,_start \
	  -Wl,--dynamic-linker,/GEMSYS/LIB/X32/PROBE.SO -o $@ $< \
	  -Lobj/ldso/test -ltest -Wl,-rpath-link,obj/ldso/test

ifeq ($(CONF_WITH_X86_64_MEMTEST),y)
all: obj/ldso/test/probe.so obj/ldso/test/dyntest.elf
endif

obj/ldso/test/interp-fixtures.stamp: tools/make-interp-fixtures.py obj/ldso/test/probe.so obj/ldso/test/dyntest.elf x32hello.elf
	python3 tools/make-interp-fixtures.py obj/ldso/test
	@touch $@
ifeq ($(CONF_WITH_X86_64_MEMTEST),y)
all: obj/ldso/test/interp-fixtures.stamp
endif

obj/ldso/test/dynpie.elf: tests/interp_probe/program.S obj/ldso/test/libtest.so
	$(CC) $(LDSO_LDFLAGS) -mx32 -nostdlib -pie -Wl,--no-as-needed,--hash-style=sysv,-e,_start \
	  -Wl,--dynamic-linker,/GEMSYS/LIB/X32/PROBE.SO -o $@ $< \
	  -Lobj/ldso/test -ltest -Wl,-rpath-link,obj/ldso/test
ifeq ($(CONF_WITH_X86_64_MEMTEST),y)
all: obj/ldso/test/dynpie.elf
endif

# Standalone interpreter, independent of libc and the kernel link engine.
LDSO_START-$(ARCH_X86_64) = lib/ldso/arch/x32/start.S lib/ldso/arch/x32/bootstrap.c lib/ldso/arch/x32/syscall.c
obj/ldso/ld.so: lib/ldso/runtime.c $(LDSO_START-y) obj/ldso/libldso.a \
                lib/ldso/runtime.h lib/ldso/exports.map $(X32_PROBE_LDSO_HEADERS) lib/ldso/startup.h
	$(CC) $(LDSO_LDFLAGS) $(LDSO_CFLAGS) -fvisibility=hidden -nostdlib -shared \
	  -Wl,--no-undefined,--hash-style=sysv,-z,now,-e,_start,--version-script,lib/ldso/exports.map \
	  -o $@ $(filter %.c %.S,$^) $(filter %.a,$^)
all: obj/ldso/ld.so

obj/ldso/test/librt.so: tests/ldso_runtime/leaf.c tools/make-partial-bss.py
	@mkdir -p $(@D)
	$(CC) $(LDSO_LDFLAGS) $(LDSO_CFLAGS) -nostdlib -shared -Wl,--hash-style=sysv,-soname,librt.so,--section-start,.partial_bss=0x20000 $< -o $@
	python3 tools/make-partial-bss.py $@
obj/ldso/test/libdyn.so: tests/ldso_runtime/library.c obj/ldso/test/librt.so
	$(CC) $(LDSO_LDFLAGS) $(LDSO_CFLAGS) -nostdlib -shared -Wl,--hash-style=sysv,-soname,libdyn.so $< -Lobj/ldso/test -lrt -o $@
obj/ldso/test/soapp.elf: LDSO_RUNTIME_APP_LINK = -no-pie
obj/ldso/test/soapp-pie.elf: LDSO_RUNTIME_APP_LINK = -pie
obj/ldso/test/soapp.elf obj/ldso/test/soapp-pie.elf: tests/ldso_runtime/start.S tests/ldso_runtime/program.c lib/ldso/arch/x32/syscall.c obj/ldso/test/libdyn.so
	$(CC) $(LDSO_LDFLAGS) $(LDSO_CFLAGS) -nostdlib $(LDSO_RUNTIME_APP_LINK) \
	  -Wl,--export-dynamic,--hash-style=sysv,-e,_start,--dynamic-linker,/GEMSYS/LIB/X32/LD.SO \
	  -o $@ $(filter %.c %.S,$^) -Lobj/ldso/test -ldyn -Wl,-rpath-link,obj/ldso/test
ifeq ($(CONF_WITH_X86_64_MEMTEST),y)
all: obj/ldso/test/soapp.elf
endif

obj/ldso/test/runtime-fixtures.stamp: tools/make-ldso-fixtures.py obj/ldso/ld.so obj/ldso/test/soapp.elf obj/ldso/test/soapp-pie.elf
	python3 tools/make-ldso-fixtures.py obj/ldso
	@touch $@
ifeq ($(CONF_WITH_X86_64_MEMTEST),y)
all: obj/ldso/test/runtime-fixtures.stamp
endif

# Separate dynamic CRT/client subset. Reuse libcmini environment handling and
# pTOS utilities without changing the legacy CRT or claiming a complete libc.
LDSO_CLIENT_CFLAGS = $(LDSO_CFLAGS) -fno-builtin -Ilib/ldso/client \
                     -Ilib/libcmini/include -Ilib/libcmini/sources
LDSO_CLIENT_MINI = lib/libcmini/sources/getenv.c
LDSO_UTIL_CFLAGS = $(LDSO_CFLAGS) -fno-builtin -fno-tree-loop-distribute-patterns \
                  -Iinclude -Iobj -Iutil
obj/ldso/sharedutil.o: lib/ldso/client/sharedutil.c util/string.c $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LDSO_UTIL_CFLAGS) -c $< -o $@
obj/ldso/sharedprint.o: util/doprintf.c $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LDSO_UTIL_CFLAGS) -c $< -o $@
LDSO_MEMORY-$(ARCH_X86_64) = util/arch/x86_64/memmove.c
obj/ldso/sharedmemory.o: $(LDSO_MEMORY-y) $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LDSO_UTIL_CFLAGS) -c $< -o $@
LDSO_DYNCRT-$(ARCH_X86_64) = lib/ldso/arch/x32/dyncrt.S
obj/ldso/dyncrt.o: $(LDSO_DYNCRT-y) $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LDSO_CFLAGS) -c $< -o $@
obj/ldso/libptos.so: lib/ldso/client/client.c lib/ldso/client/ptos.h \
                    lib/ldso/client/exports.map $(LDSO_CLIENT_MINI) lib/ldso/arch/x32/syscall.c \
                    obj/ldso/sharedutil.o obj/ldso/sharedprint.o obj/ldso/sharedmemory.o \
                    lib/ldso/runtime.h lib/ldso/startup.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LDSO_LDFLAGS) $(LDSO_CLIENT_CFLAGS) -nostdlib -shared \
	  -Wl,--no-undefined,--hash-style=sysv,-z,now,-soname,libptos.so,-init,__ptos_client_init \
	  -Wl,--version-script,lib/ldso/client/exports.map -o $@ $(filter %.c %.o,$^)
all: obj/ldso/dyncrt.o obj/ldso/libptos.so

obj/ldso/test/libuse.so: tests/ldso_client/library.c obj/ldso/libptos.so lib/ldso/client/ptos.h
	@mkdir -p $(@D)
	$(CC) $(LDSO_LDFLAGS) $(LDSO_CLIENT_CFLAGS) -nostdlib -shared -Wl,--hash-style=sysv,-z,now,-soname,libuse.so \
	  -o $@ $< -Lobj/ldso -lptos
obj/ldso/test/client.elf: LDSO_CLIENT_LINK = -no-pie
obj/ldso/test/clientpie.elf: LDSO_CLIENT_LINK = -pie
obj/ldso/test/client.elf obj/ldso/test/clientpie.elf: tests/ldso_client/program.c tests/ldso_client/nested.S obj/ldso/dyncrt.o \
                        obj/ldso/test/libuse.so obj/ldso/libptos.so lib/ldso/client/ptos.h
	$(CC) $(LDSO_LDFLAGS) $(LDSO_CLIENT_CFLAGS) -nostdlib $(LDSO_CLIENT_LINK) \
	  -Wl,--export-dynamic,--hash-style=sysv,-z,now,-e,_start,--dynamic-linker,/GEMSYS/LIB/X32/LD.SO \
	  -o $@ $(filter %.c %.S %.o,$^) -Lobj/ldso/test -luse -Lobj/ldso -lptos
ifeq ($(CONF_WITH_X86_64_MEMTEST),y)
all: obj/ldso/test/client.elf obj/ldso/test/clientpie.elf
endif

# Native consumers use the ordinary ELF interpreter and shared runtime.
LDSO_APP_CFLAGS = -fPIC
LDSO_APP_LDFLAGS = $(LDSO_CFLAGS-y) $(LDSO_LDFLAGS) -nostdlib -no-pie \
                  -Wl,--hash-style=sysv,-z,now,-z,noexecstack,-e,_start \
                  -Wl,--dynamic-linker,/GEMSYS/LIB/X32/LD.SO \
                  -Wl,-Ttext-segment=0x400000
LDSO_RUNTIME_DIR = obj/ldso/runtime/GEMSYS
LDSO_RUNTIME_FILES = $(LDSO_RUNTIME_DIR)/LIB/X32/LD.SO $(LDSO_RUNTIME_DIR)/LIB/X32/LIBPTOS.SO
$(LDSO_RUNTIME_DIR)/LIB/X32/LD.SO: obj/ldso/ld.so
	@mkdir -p $(@D)
	cp $< $@
$(LDSO_RUNTIME_DIR)/LIB/X32/LIBPTOS.SO: obj/ldso/libptos.so
	@mkdir -p $(@D)
	cp $< $@
$(LDSO_RUNTIME_DIR): $(LDSO_RUNTIME_FILES)
	@true
all: $(LDSO_RUNTIME_FILES)
