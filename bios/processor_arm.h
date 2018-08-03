#ifndef PROCESSOR_ARM_H
#define PROCESSOR_ARM_H

#ifndef __stringify
#define __stringify(x) #x
#endif

typedef unsigned long uint32_t;

#define CPUID_ID	0
#define CPUID_CACHETYPE	1
#define CPUID_TCM	2
#define CPUID_TLBTYPE	3
#define CPUID_MPUIR	4
#define CPUID_MPIDR	5
#define CPUID_REVIDR	6

#define ARM_MIDR_IMP_ARM			0x41000000
#define ARM_MIDR_IMP_DEC			0x44000000
#define ARM_MIDR_IMP_QUALCOMM		0x51000000
#define ARM_MIDR_IMP_INTEL			0x69000000

/* ARM implemented processors */
#define ARM_MIDR_PART_ARM1136		0x4100b360
#define ARM_MIDR_PART_ARM1156		0x4100b560
#define ARM_MIDR_PART_ARM1176		0x4100b760
#define ARM_MIDR_PART_ARM11MPCORE	0x4100b020
#define ARM_MIDR_PART_CORTEX_A8		0x4100c080
#define ARM_MIDR_PART_CORTEX_A9		0x4100c090
#define ARM_MIDR_PART_CORTEX_A5		0x4100c050
#define ARM_MIDR_PART_CORTEX_A7		0x4100c070
#define ARM_MIDR_PART_CORTEX_A12	0x4100c0d0
#define ARM_MIDR_PART_CORTEX_A17	0x4100c0e0
#define ARM_MIDR_PART_CORTEX_A15	0x4100c0f0
#define ARM_MIDR_PART_MASK			0xff00fff0

/* DEC implemented cores */
#define ARM_MIDR_PART_SA1100		0x4400a110

/* Intel implemented cores */
#define ARM_MIDR_PART_SA1110		0x6900b110
#define ARM_MIDR_REV_SA1110_A0		0
#define ARM_MIDR_REV_SA1110_B0		4
#define ARM_MIDR_REV_SA1110_B1		5
#define ARM_MIDR_REV_SA1110_B2		6
#define ARM_MIDR_REV_SA1110_B4		8

#define ARM_MIDR_XSCALE_ARCH_MASK	0xe000
#define ARM_MIDR_XSCALE_ARCH_V1		0x2000
#define ARM_MIDR_XSCALE_ARCH_V2		0x4000
#define ARM_MIDR_XSCALE_ARCH_V3		0x6000

/* Qualcomm implemented cores */
#define ARM_CPU_PART_SCORPION		0x510002d0


#define read_cpuid(reg)						\
	({								\
		register uint32_t __val;			\
		__asm__ __volatile__("mrc	p15,0,%0,c0,c0," __stringify(reg)		\
		    : "=r" (__val)					\
		    :							\
		    : "memory");					\
		__val;							\
	})

static inline uint32_t __attribute__((__const__)) read_cpuid_id(void)
{
	return read_cpuid(CPUID_ID);
}

static inline uint32_t __attribute__((__const__)) read_cpuid_mputype(void)
{
	return read_cpuid(CPUID_MPUIR);
}

#ifndef TARGET_RPI1
void clean_data_cache(void);
void flush_data_cache_all(void);
void flush_branch_target_cache(void);
#endif

#endif /* PROCESSOR_ARM_H */
