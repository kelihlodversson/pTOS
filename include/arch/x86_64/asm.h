/*
 * asm.h - Assembler help routines
 *
 * Copyright (C) 2001-2017 The EmuTOS development team
 * Copyright (C) 2018-2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * x86-64 counterpart of include/arch/arm/asm.h -- see that file's own
 * comment for the general shape of what belongs here. Only what this
 * port's shared (non-arch) code actually calls is implemented; m68k-only
 * macros (set_sr/get_sr, swpw/rolw1/...) are left as deliberately broken
 * placeholders, the same way ARM's own copy does, since no shared caller
 * should ever reach them here.
 */

#ifndef ASM_H
#define ASM_H

/*
 * values of 'mode' for Pexec()
 *
 * these were moved here because of the definition of trap1_pexec() below
 */
#define PE_LOADGO     0
#define PE_LOAD       3
#define PE_GO         4
#define PE_BASEPAGE   5
#define PE_GOTHENFREE 6
#define PE_BASEPAGEFLAGS 7
#define PE_RELOCATE   50    /* required for NatFeats support only, not in Atari TOS */

/* OS entry points implemented in util/arch/x86_64/miscasm.S */
extern long trap1(int, ...);
extern long trap1_pexec(short mode, const char * path,
  const char * tail, const char * env);

/* External function doing nothing */
extern void just_rts(void);

/*
 * WORD mul_div_round(WORD mult1, WORD mult2, WORD divisor);
 *   returns (mult1 * mult2 / divisor), rounded away from zero
 */
static __inline__ WORD mul_div_round(WORD mult1, WORD mult2, WORD divisor)
{
    LONG n = (LONG)mult1 * mult2 * 2;
    LONG q = n / divisor;

    return (WORD)((q >= 0) ? (q + 1) / 2 : (q - 1) / 2);
}

/* Wrapper around HLT. Only ever called from ring 0 (this milestone has
 * no ring-3 callers of it), where HLT is not privileged. */
static inline void stop_until_interrupt(void)
{
    __asm__ volatile ("hlt");
}

#define swpw(a) @swpw_not_supported
#define swpw2(a) @swpw2_not_supported
#define rolw1(x)    ((x)=((x)>>15)|((x)<<1))
#define rorw1(x)    ((x)=((x)>>1)|((x)<<15))

#define roll(x,n)   ((x)=(((x)>>(32-(n)))|((x)<<(n))))
#define rorl(x,n)   ((x)=(((x)<<(32-(n)))|((x)>>(n))))

/* No m68k SR here -- see disable_interrupts()/enable_interrupts() below,
 * this arch's actual equivalent (bios/arch/x86_64/intmask.c). */
#define set_sr(a) @USE_disable_interrupts_on_x86_64
#define get_sr(a) @USE_disable_interrupts_on_x86_64

/* bios/arch/x86_64/intmask.c: single, non-nesting save slot. */
extern ULONG disable_interrupts(void);
extern void enable_interrupts(void);

/* x86 has no software-managed cache/prefetch-buffer state analogous to
 * ARM's coprocessor barriers -- ordinary stores are globally visible
 * without an explicit barrier instruction on this arch (Intel SDM Vol 3A
 * 8.2, for normal write-back memory, which is everything this port uses
 * -- no device/UC mappings that would need one). */
#define flush_prefetch_buffer()     ((void) 0)
#define data_sync_barrier()         ((void) 0)
#define data_mem_barrier()          ((void) 0)
#define peripheral_begin()          ((void) 0)
#define peripheral_end()            ((void) 0)
#define instruction_sync_barrier()  ((void) 0)
#define instruction_mem_barrier()   ((void) 0)

/*
 * void regsafe_call(void *addr)
 *   Saves all registers to the stack, calls the function
 *   that addr points to, and restores the registers afterwards.
 */
/* not used on x86-64 currently, but we assume anything called follows
 * the SysV AMD64 ABI, so no registers need saving beyond what the
 * compiler already does for a normal call. */
#define regsafe_call(addr)                         \
  ((void (*)(void))addr)();

/*
 * Loops for the specified count; for a 1 millisecond delay on the
 * current system, use the value in the global 'loopcount_1_msec'.
 */
 #define delay_loop(count) __extension__ \
 ({                                      \
   ULONG _count = (count);             \
   while(_count) {_count--;}           \
 })
#endif /* ASM_H */
