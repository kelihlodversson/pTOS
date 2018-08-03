#ifndef RASPI_IO_H
#define RASPI_IO_H

#ifdef MACHINE_RPI

#define GPU_L2_CACHE_ENABLED

#define GPU_IO_BASE		    0x7e000000
#define GPU_CACHED_BASE		0x40000000
#define GPU_UNCACHED_BASE	0xc0000000

#if defined(TARGET_RPI2) || defined(TARGET_RPI3)
#   define ARM_IO_BASE		  0x3f000000
#   define GPU_MEM_BASE	      GPU_UNCACHED_BASE
#else
#   define ARM_IO_BASE		  0x20000000
#   ifdef GPU_L2_CACHE_ENABLED
#       define GPU_MEM_BASE   GPU_CACHED_BASE
#   else
#       define GPU_MEM_BASE   GPU_UNCACHED_BASE
#   endif
#endif

static inline ULONG phys_to_bus(ULONG phys)
{
	return GPU_MEM_BASE | phys;
}

static inline ULONG bus_to_phys(ULONG bus)
{
	return bus & ~GPU_MEM_BASE;
}

#endif /* MACHINE_RPI */
#endif /* RASPI_IO_H */
