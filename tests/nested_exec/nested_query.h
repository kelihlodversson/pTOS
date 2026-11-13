/* Shared by the nested Pexec suite and its child: the regression kernels'
 * scheduler query channel (bdos/clone.c, CONF_WITH_PRIVATE_CLONE_TEST). */
#ifndef NESTED_QUERY_H
#define NESTED_QUERY_H
#include "../../include/pclone.h"
#include "../../include/threadcalls.h"
#include <mint/osbind.h>

#define NQ_TID          0x103   /* TID of the calling THREAD, EINVFN without one */
#define NQ_DEPTH        0x700   /* nested executions associated with this carrier */
#define NQ_PREPARED     0x701
#define NQ_ENTERED      0x702
#define NQ_STOPPED      0x703
#define NQ_RECLAIMED    0x704
#define NQ_CANCELLED    0x705
#define NQ_REFUSED      0x706
#define NQ_LIVE         0x709   /* prepared and neither reclaimed nor cancelled */
#define NQ_LAST_PID     0x70a   /* the most recently entered nested execution */
#define NQ_LAST_TID     0x70b
#define NQ_LAST_DEPTH   0x70c
#define NQ_LAST_PD      0x70d   /* its basepage address */
#define NQ_FAIL_STAGE   0x710   /* + stage 1..6: one-shot construction failure */
#define NQ_PROCESSES    0x780
#define NQ_THREADS      0x781
#define NQ_SPACES       0x782
#define NQ_KSTACKS      0x783

#define NESTED_EINVFN   (-32L)
#define NESTED_ENSMEM   (-39L)
#define NESTED_CHILD    "C:\\NESTCH.TOS"

static inline long nested_trap2(short number, long first, long second)
{
#if defined(__mc68000__)
    register long result __asm__("d0");
    __asm__ volatile (
        "move.l %2,-(%%sp)\n\t"
        "move.l %1,-(%%sp)\n\t"
        "move.w %3,-(%%sp)\n\t"
        "trap #1\n\t"
        "lea 10(%%sp),%%sp"
        : "=d"(result) : "r"(first), "r"(second), "r"(number)
        : "d1", "d2", "a0", "a1", "a2", "memory", "cc");
    return result;
#elif defined(__arm__)
    register long r0 __asm__("r0") = number;
    register long r1 __asm__("r1") = first;
    register long r2 __asm__("r2") = second;
    __asm__ volatile ("svc 1" : "+r"(r0) : "r"(r1), "r"(r2)
                      : "r3", "r12", "lr", "memory", "cc");
    return r0;
#else
    return trap_1_wll(number, first, second);
#endif
}

#if defined(TEST_WITH_PRIVATE_CLONE_TEST)
static inline long nested_query(unsigned selector)
{
    return nested_trap2(GEMDOS_PCLONE, 0L, (long)(0xc8920000UL + selector));
}
#else
static inline long nested_query(unsigned selector) { (void)selector; return NESTED_EINVFN; }
#endif

/* Fixed-width upper-case hexadecimal, the only number format of the tails. */
static inline void nested_put_hex(char *out, unsigned long value, int digits)
{
    while (digits--) {
        unsigned nibble = (unsigned)(value & 15);
        out[digits] = (char)(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
        value >>= 4;
    }
}
static inline unsigned long nested_get_hex(const char *in, int digits)
{
    unsigned long value = 0;
    while (digits--) {
        char c = *in++;
        value = (value << 4) | (unsigned long)(c <= '9' ? c - '0' : c - 'A' + 10);
    }
    return value;
}

/* Chain tail after the length byte:
 *   'c' remaining mode flag tracked  ppid:4 ptid:8 depth:2 level:2
 *       processes:4 threads:4 spaces:4 kstacks:4
 * mode: leaf exit 'r' return, 't' Pterm, '0' Pterm0, 'f' fault.
 * flag: 'x' exact global counts, 'p' wait for the peer's marker, '-' neither. */
#define NESTED_TAIL_BYTES 43
struct nested_chain {
    int remaining, depth, level, tracked;
    char mode, flag;
    long ppid, ptid;
    long processes, threads, spaces, kstacks;
};
static inline void nested_chain_tail(char *tail, const struct nested_chain *chain)
{
    tail[1] = 'c';
    tail[2] = (char)('0' + chain->remaining);
    tail[3] = chain->mode;
    tail[4] = chain->flag;
    tail[5] = chain->tracked ? 'T' : 'U';
    nested_put_hex(tail + 6, (unsigned long)chain->ppid, 4);
    nested_put_hex(tail + 10, (unsigned long)chain->ptid, 8);
    nested_put_hex(tail + 18, (unsigned long)chain->depth, 2);
    nested_put_hex(tail + 20, (unsigned long)chain->level, 2);
    nested_put_hex(tail + 22, (unsigned long)chain->processes, 4);
    nested_put_hex(tail + 26, (unsigned long)chain->threads, 4);
    nested_put_hex(tail + 30, (unsigned long)chain->spaces, 4);
    nested_put_hex(tail + 34, (unsigned long)chain->kstacks, 4);
    tail[38] = 0;
    tail[0] = 37;
}
static inline long nested_leaf_status(char mode)
{
    return mode == 'r' ? 23 : mode == 't' ? 24 : mode == '0' ? 0 : 0xffffL;
}
#define NESTED_LEVEL_STATUS(remaining) (30L + (remaining))

#endif
