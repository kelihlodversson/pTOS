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
#ifdef __arm__
static inline void stop_until_interrupt(void)
{
    __asm__ volatile ("wfi");
}
#else
extern void stop_until_interrupt(void);
#endif

/*
 * WORD swpw(WORD val);
 *   swap endianess of val, 16 bits only.
 */

#ifdef __mcoldfire__
#define swpw(a)                           \
  __extension__                           \
  ({long _tmp;                            \
    __asm__ volatile                      \
    ("move.w  %0,%1\n\t"                  \
     "lsl.l   #8,%0\n\t"                  \
     "lsr.l   #8,%1\n\t"                  \
     "move.b  %1,%0"                      \
    : "=d"(a), "=d"(_tmp) /* outputs */   \
    : "0"(a)     /* inputs  */            \
    : "cc"       /* clobbered */          \
    );                                    \
  })
#elif defined(__m68k__)
#define swpw(a)                           \
  __asm__ volatile                        \
  ("ror   #8,%0"                          \
  : "=d"(a)          /* outputs */        \
  : "0"(a)           /* inputs  */        \
  : "cc"             /* clobbered */      \
  )
#else
#define swpw(a) @swpw_not_supported
#endif

/*
 * WORD swpw2(ULONG val);
 *   swap endianness of val, treated as two 16-bit words.
 *   e.g. ABCD => BADC
 */

#ifdef __mcoldfire__
#define swpw2(a)                          \
  __extension__                           \
  ({unsigned long _tmp;                   \
    __asm__ volatile                      \
    ("move.b  (%1),%0\n\t"                \
     "move.b  1(%1),(%1)\n\t"             \
     "move.b  %0,1(%1)\n\t"               \
     "move.b  2(%1),%0\n\t"               \
     "move.b  3(%1),2(%1)\n\t"            \
     "move.b  %0,3(%1)"                   \
    : "=d"(_tmp)     /* outputs */        \
    : "a"(&a)        /* inputs  */        \
    : "cc", "memory" /* clobbered */      \
    );                                    \
  })
#elif defined(__m68k__)
#define swpw2(a)                          \
  __asm__ volatile                        \
  ("ror   #8,%0\n\t"                      \
   "swap  %0\n\t"                         \
   "ror   #8,%0\n\t"                      \
   "swap  %0"                             \
  : "=d"(a)          /* outputs */        \
  : "0"(a)           /* inputs  */        \
  : "cc"             /* clobbered */      \
  )
#else
#define swpw2(a) @swpw2_not_supported
#endif


/*
 * rolw1(WORD x);
 *  rotates x leftwards by 1 bit
 */
#if defined(__m68k__) && !defined(__mcoldfire__)
#define rolw1(x)                    \
    __asm__ volatile                \
    ("rol.w #1,%1"                  \
    : "=d"(x)       /* outputs */   \
    : "0"(x)        /* inputs */    \
    : "cc"          /* clobbered */ \
    )
#else
#define rolw1(x)    ((x)=((x)>>15)|((x)<<1))
#endif


/*
 * rorw1(WORD x);
 *  rotates x rightwards by 1 bit
 */
#if defined(__m68k__) && !defined(__mcoldfire__)
#define rorw1(x)                    \
    __asm__ volatile                \
    ("ror.w #1,%1"                  \
    : "=d" (x)      /* outputs */   \
    : "0" (x)       /* inputs */    \
    : "cc"          /* clobbered */ \
    )
#else
#define rorw1(x)    ((x)=((x)>>1)|((x)<<15))
#endif


/*
 * Warning: The following macros use "memory" in the clobber list,
 * even if the memory is not modified. On ColdFire, this is necessary
 * to prevent these instructions being reordered by the compiler.
 *
 * Apparently, this is standard GCC behaviour (RFB 2012).
 */


/*
 * WORD set_sr(WORD new);
 *   sets sr to the new value, and return the old sr value
 */

#if defined(__mcoldfire__) || defined(__m68k__)

extern void disable_interrupts(void);
extern void enable_interrupts(void);

#define set_sr(a)                         \
__extension__                             \
({short _r, _a = (a);                     \
  __asm__ volatile                        \
  ("move.w sr,%0\n\t"                     \
   "move.w %1,sr"                         \
  : "=&d"(_r)        /* outputs */        \
  : "nd"(_a)         /* inputs  */        \
  : "cc", "memory"   /* clobbered */      \
  );                                      \
  _r;                                     \
})


/*
 * WORD get_sr(void);
 *   returns the current value of sr.
 */

#define get_sr()                          \
__extension__                             \
({short _r;                               \
  __asm__ volatile                        \
  ("move.w sr,%0"                         \
  : "=dm"(_r)        /* outputs */        \
  :                  /* inputs  */        \
  : "cc", "memory"   /* clobbered */      \
  );                                      \
  _r;                                     \
})
#elif defined(__arm__)
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

#endif

/*
 * void regsafe_call(void *addr)
 *   Saves all registers to the stack, calls the function
 *   that addr points to, and restores the registers afterwards.
 */
#ifdef __mcoldfire__
#define regsafe_call(addr)                         \
__extension__                                      \
({__asm__ volatile ("lea     -60(sp),sp\n\t"       \
                    "movem.l d0-d7/a0-a6,(sp)");   \
  ((void (*)(void))addr)();                        \
  __asm__ volatile ("movem.l (sp),d0-d7/a0-a6\n\t" \
                    "lea     60(sp),sp");          \
})
#elif defined(__m68k__)
#define regsafe_call(addr)                         \
__extension__                                      \
({__asm__ volatile ("movem.l d0-d7/a0-a6,-(sp)");  \
  ((void (*)(void))addr)();                        \
  __asm__ volatile ("movem.l (sp)+,d0-d7/a0-a6");  \
})
#else
/* not used on arm curently, but we assume anything called follows the abi, so */
/* no registers need to be saved apart from what the compiler already does */
#define regsafe_call(addr)                         \
  ((void (*)(void))addr)();
#endif



/*
 * Loops for the specified count; for a 1 millisecond delay on the
 * current system, use the value in the global 'loopcount_1_msec'.
 */
#if defined(__m68k__)
#define delay_loop(count)                   \
  __extension__                             \
  ({ULONG _count = (count);                 \
    __asm__ volatile                        \
    ("0:\n\t"                               \
     "subq.l #1,%0\n\t"                     \
     "jpl    0b"                            \
    :                   /* outputs */       \
    : "d"(_count)       /* inputs  */       \
    : "cc", "memory"    /* clobbered */     \
    );                                      \
  })
#else
    #define delay_loop(count) __extension__ \
    ({                                      \
        ULONG _count = (count);             \
        while(_count) {_count--;}           \
    })
#endif

#endif /* ASM_H */
