#ifndef RASPI_IO_H
#define RASPI_IO_H

#ifdef MACHINE_RPI

#define GPU_L2_CACHE_ENABLED

#define GPU_IO_BASE		    0x7E000000
#define GPU_CACHED_BASE		0x40000000
#define GPU_UNCACHED_BASE	0xC0000000

#if defined(TARGET_RPI2) || defined(TARGET_RPI3)
#   define ARM_IO_BASE		  0x3F000000
#   define GPU_MEM_BASE	      GPU_UNCACHED_BASE
#else
#   define ARM_IO_BASE		  0x20000000
#   ifdef GPU_L2_CACHE_ENABLED
#       define GPU_MEM_BASE   GPU_CACHED_BASE
#   else
#       define GPU_MEM_BASE   GPU_UNCACHED_BASE
#   endif
#endif


#endif /* MACHINE_RPI */
#endif /* RASPI_IO_H */
