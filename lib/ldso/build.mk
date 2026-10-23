# Freestanding x32 userspace for the initial #660 milestone.  When another
# backend is ready, select its flags here and widen the Kconfig dependency.
LDSO_CFLAGS-$(ARCH_X86_64) = -mx32
LDSO_CFLAGS = $(LDSO_CFLAGS-y) -std=gnu90 -Os -ffreestanding -fPIC \
              -fno-stack-protector -Wall -Wextra -Werror -Ilib/ldso

obj/ldso/dynamic.o: lib/ldso/dynamic.c lib/ldso/dynamic.h $(AUTOCONF_H)
	@mkdir -p $(@D)
	$(CC) $(LDSO_CFLAGS) -c $< -o $@

obj/ldso/libldso.a: obj/ldso/dynamic.o
	$(CROSS_COMPILE)ar rcs $@ $^

all: obj/ldso/libldso.a
