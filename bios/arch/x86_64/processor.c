/*
 * processor.c - x86-64 processor/cache primitives
 *
 * Copyright (C) 2018-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "processor.h"
#include "biosext.h"
#include "vectors.h"
#include "sound.h"
#include "tosvars.h"
#include "string.h"

/*
 * detect_cpu() (below) sets mcpu/mcpu_name; there is no ColdFire/m68k-style CPU
 * *variant* to configure here, and no SSE/FPU enable step either --
 * UEFI firmware already runs in long mode with CR0/CR4 set up for SSE,
 * and no floating point is used anywhere in this port yet (#329).
 */
void processor_init(void)
{
    detect_cpu();
}

/* width of the CPU type value on the welcome screen (initinfo.c) */
#define X86_CPU_NAME_MAX 20

static char x86_cpu_name[X86_CPU_NAME_MAX + 1];

static void cpuid(ULONG leaf, ULONG *r)
{
    __asm__ volatile ("cpuid"
              : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3])
              : "a"(leaf), "c"(0));
}

/* append the n-character word w to x86_cpu_name, space separated, if it fits */
static void cpu_name_add(const char *w, int n)
{
    int len = strlen(x86_cpu_name);

    if (len + (len != 0) + n > X86_CPU_NAME_MAX)
        return;
    if (len)
        x86_cpu_name[len++] = ' ';
    memcpy(&x86_cpu_name[len], w, n);
    x86_cpu_name[len + n] = '\0';
}

/*
 * Build a short name for the welcome screen from the processor brand
 * string (CPUID leaves 0x80000002-4), or the vendor string (leaf 0) if the
 * CPU has no brand string.  A brand string is up to 48 characters, e.g.
 * "Intel(R) Core(TM) i7-9700K CPU @ 3.60GHz", so drop the "(R)"/"(TM)"
 * marks, the "CPU"/"Processor"/"version"/"N-Core" filler words and everything from
 * the "@" on, and keep only the words that fit.
 */
static const char *cpu_display_name(void)
{
    ULONG r[4];
    char raw[49];
    char word[49];
    const char *p;
    int i, n, k;

    cpuid(0x80000000UL, r);
    if (r[0] >= 0x80000004UL) {
        for (i = 0; i < 3; i++) {
            cpuid(0x80000002UL + i, r);
            memcpy(&raw[16 * i], r, 16);
        }
        raw[48] = '\0';
    } else {
        cpuid(0, r);
        memcpy(&raw[0], &r[1], 4);      /* vendor is EBX, EDX, ECX */
        memcpy(&raw[4], &r[3], 4);
        memcpy(&raw[8], &r[2], 4);
        raw[12] = '\0';
    }

    x86_cpu_name[0] = '\0';
    p = raw;
    while (*p) {
        while (*p == ' ')
            p++;
        for (n = 0; p[n] && p[n] != ' '; n++)
            ;
        if (n == 1 && *p == '@')
            break;
        for (i = k = 0; i < n; i++) {
            if (p[i] == '(') {      /* "(R)" or "(TM)" */
                while (i < n && p[i] != ')')
                    i++;
                continue;
            }
            word[k++] = p[i];
        }
        word[k] = '\0';
        if (k && strcmp(word, "CPU") && strcmp(word, "Processor")
            && strcmp(word, "version")
            && !(k > 5 && !strcmp(&word[k - 5], "-Core")))
            cpu_name_add(word, k);
        p += n;
    }
    if (!x86_cpu_name[0])
        cpu_name_add("x86-64", 6);
    return x86_cpu_name;
}

/*
 * mcpu holds CPUID leaf 1's EAX (the family/model/stepping signature),
 * not an m68k-style small integer code -- the same trick ARM plays with
 * its own raw CPUID register (above): every other reader of mcpu
 * (kprint.c, delay.c, aros.c, ide.c) only ever compares it against
 * specific m68k CPU-type constants (0/10/20/30/40/60), which a real
 * CPUID signature is never going to coincidentally match, so those
 * m68k-only code paths stay correctly inert here without needing their
 * own #ifdef.
 */
void detect_cpu(void)
{
    ULONG r[4];

    cpuid(1, r);
    mcpu = (LONG)r[0];
    mcpu_name = cpu_display_name();
}

/*
 * No-ops: x86 (unlike ARM/m68k) does not require explicit cache
 * maintenance around ordinary program loading or DMA -- the CPU's
 * instruction cache is coherent with the data cache/memory for code a
 * normal store just wrote (Intel SDM Vol 3A 11.6), and there is no
 * software-managed, non-coherent DMA path active on this arch yet (the
 * bios/acsi.c/floppy.c/scsi.c/amiga.c call sites are all Atari-hardware
 * only, already excluded via CONF_ATARI_HARDWARE; the virtio_*.c ones
 * need CONF_WITH_VIRTIO, not yet enabled for this machine).
 */
void instruction_cache_kludge(void *start, long size)
{
    (void)start; (void)size;
}

void flush_data_cache(void *start, long size)
{
    (void)start; (void)size;
}

void invalidate_data_cache(void *start, long size)
{
    (void)start; (void)size;
}

void invalidate_instruction_cache(void *start, long size)
{
    (void)start; (void)size;
}

/*
 * Do-nothing placeholder handler (tosvars.c's OSHEADER.reseth, mouse.c/
 * chardev.c's "no handler installed" default), matching ARM's own
 * bios/arch/arm/vectorsasm.S::just_rts -- this arch has no equivalent
 * vector-table file to put it in, so it lives here instead.
 */
void just_rts(void)
{
}

/*
 * just_rte()/gemtrap()/biostrap()/xbiostrap()/default_etv_critic():
 * on m68k/ARM these are real trap-entry assembly stubs, installed at
 * fixed vector addresses (VEC_LEVEL1..7, VEC_DIVNULL, VEC_GEM, VEC_BIOS,
 * VEC_XBIOS -- bios.c's vecs_init()) and reached via a CPU-level
 * trap/exception instruction (m68k's "trap #N", ARM's "svc"). This
 * arch's GEMDOS/BIOS/XBIOS calls never go through that mechanism at all
 * -- bios/arch/x86_64/trap.c's syscall/sysretq dispatch reaches osif()/
 * the BIOS and XBIOS handlers directly -- so bios_init() writing their
 * addresses into VEC_GEM/VEC_BIOS/VEC_XBIOS is pure unread data on this
 * arch (see trap.c's own comment). Real, empty bodies are still needed
 * so vecs_init() has valid function pointers to store, and so a stray
 * read of one of those vector cells gets a harmless jump target rather
 * than an unrelated address.
 */
void just_rte(void)
{
}

void gemtrap(void)
{
}

void biostrap(void)
{
}

void xbiostrap(void)
{
}

LONG default_etv_critic(WORD err, WORD dev)
{
    (void)dev;
    return err;
}

/*
 * keyclick(): ARM/m68k implement this as a trampoline (vectorsasm.S/
 * vectors.S) that discards its own scancode argument and tail-jumps into
 * kcl_hook (tosvars.c's storage, set to sound.c's do_keyclick() -- which
 * itself takes no arguments). A real function call reproduces the same
 * effect in plain C.
 */
void keyclick(UBYTE scancode)
{
    (void)scancode;
    kcl_hook();
}
