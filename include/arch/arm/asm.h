/*
 * asm.h - Assembler help routines
 *
 * Copyright (C) 2001-2017 The EmuTOS development team
 *
 * Authors:
 *  LVL   Laurent Vogel
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * This file contains utility routines (macros) to
 * perform functions not directly available from C.
 *
 * available macros:
 *
 * WORD set_sr(WORD new);
 *   sets sr to the new value, and return the old sr value
 * WORD get_sr(void);
 *   returns the current value of sr. the CCR bits are not meaningful.
 * void regsafe_call(void *addr);
 *   Do a subroutine call with saving/restoring the CPU registers
 * void delay_loop(ULONG loopcount);
 *   Loops for the specified loopcount.
 *
 * For clarity, please add such two lines above when adding
 * new macros below.
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

/* OS entry points implemented in util/miscasm.S */
extern long trap1(int, ...);
extern long trap1_pexec(short mode, const char * path,
  const void * tail, const char * env);

/* Wrapper around the STOP instruction. This preserves SR. */
static inline void stop_until_interrupt(void)
{
    __asm__ volatile ("wfi");
}

#define swpw(a) @swpw_not_supported
#define swpw2(a) @swpw2_not_supported
#define rolw1(x)    ((x)=((x)>>15)|((x)<<1))
#define rorw1(x)    ((x)=((x)>>1)|((x)<<15))

#define set_sr(a) @USE_set_cpsr_on_ARM
#define get_sr(a) @USE_get_cpsr_on_ARM

#define cpsr_ie() __asm__ volatile ("cpsie i")
#define cpsr_id() __asm__ volatile ("cpsid i")

#define set_cpsr(a)                       \
__extension__                             \
({int _r, _a = (a);                       \
  __asm__ volatile                        \
  ("mrs %0, cpsr\n\t"                     \
   "msr cpsr_cfxs, %1"                    \
  : "=r"(_r)        /* outputs */         \
  : "r"(_a)          /* inputs  */        \
  : "cc", "memory"   /* clobbered */      \
  );                                      \
  _r;                                     \
})

#define get_cpsr()                        \
 __extension__                            \
({int _r;                                 \
  __asm__ volatile                        \
  ("mrs %0, cpsr\n\t"                     \
  : "=r"(_r)        /* outputs */         \
  :                  /* inputs  */        \
  : "cc", "memory"   /* clobbered */      \
  );                                      \
  _r;                                     \
})

extern ULONG disable_interrupts(void);
extern void enable_interrupts(void);

#ifdef TARGET_RPI1
#define flush_prefetch_buffer()	    __asm__ volatile ("mcr p15, 0, %0, c7, c5,  4" : : "r" (0) : "memory")

#define data_sync_barrier()         __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4" : : "r" (0) : "memory")
#define data_mem_barrier()          __asm__ volatile ("mcr p15, 0, %0, c7, c10, 5" : : "r" (0) : "memory")

#define peripheral_begin()          data_sync_barrier()	/* ignored here */
#define peripheral_end()            data_mem_barrier()
#else
#define flush_prefetch_buffer()     __asm__ volatile ("isb" ::: "memory")

#define data_sync_barrier()         __asm__ volatile ("dsb" ::: "memory")
#define data_mem_barrier()          __asm__ volatile ("dmb" ::: "memory")

#define peripheral_begin()  ((void) 0)	/* ignored here */
#define peripheral_end()    ((void) 0)
#endif

#define instruction_sync_barrier()  flush_prefetch_buffer()
#define instruction_mem_barrier()   flush_prefetch_buffer()


/*
 * void regsafe_call(void *addr)
 *   Saves all registers to the stack, calls the function
 *   that addr points to, and restores the registers afterwards.
 */
/* not used on arm curently, but we assume anything called follows the abi, so */
/* no registers need to be saved apart from what the compiler already does */
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
