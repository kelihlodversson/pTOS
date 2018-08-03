/* Alignment macros taken from linux an u-boot headers */

#ifndef __USB_IO_H__
#define __USB_IO_H__

#include "portab.h"
#include "asm.h"
#include "endian.h"
#include "../bios/processor.h"

#define roundup(x, y)   (((x) + ((y) - 1)) & ~((y) - 1))
#define __pure               __attribute__((pure))
#define __aligned(x)         __attribute__((aligned(x)))
#define __printf(a, b)       __attribute__((format(printf, a, b)))
#define __scanf(a, b)        __attribute__((format(scanf, a, b)))
#define __attribute_const__  __attribute__((__const__))
#define __maybe_unused       __attribute__((unused))
#define __always_unused      __attribute__((unused))

#define ALIGN(x,a)        __ALIGN_MASK((x),(typeof(x))(a)-1)
#define __ALIGN_MASK(x,mask)    (((x)+(mask))&~(mask))
#define DEFINE_ALIGN_BUFFER(type, name, size, align)            \
    static char __##name[ALIGN(size * sizeof(type), align)]     \
            __aligned(align);                                   \
                                                                \
    static type *name = (type *)__##name

#define __arch_get(size,a)            (*(volatile uint ## size ## _t *)(a))
#define __arch_put(size,a,v)        (*(volatile uint ## size ## _t *)(a) = (v))

#define out_arch(size,endian,a,v)    __arch_put(size,a,cpu2##endian##size(v))
#define in_arch(size,endian,a)        endian##2cpu##size(__arch_get(size, a))

#define out_le32(a,v)    out_arch(32,le,a,v)
#define out_le16(a,v)    out_arch(16,le,a,v)

#define in_le32(a)    in_arch(32,le,a)
#define in_le16(a)    in_arch(16,le,a)

#define out_be32(a,v)    out_arch(32,be,a,v)
#define out_be16(a,v)    out_arch(16,be,a,v)

#define in_be32(a)    in_arch(32,be,a)
#define in_be16(a)    in_arch(16,be,a)

#define out_8(a,v)    __arch_put(8,a,v)
#define in_8(a)        __arch_get(8,a)

#define setbits(esz, addr, set) \
    out_##esz((addr), in_##esz(addr) | (set))

#define clrsetbits(esz, addr, clear, set) \
    out_##esz((addr), (in_##esz(addr) & ~(clear)) | (set))

#define clrbits(esz, addr, clear) \
    out_##esz((addr), (in_##esz(addr) & ~(clear)))

#define clrbits_be32(addr, clear) clrbits(be32, addr, clear)
#define setbits_be32(addr, set) setbits(be32, addr, set)
#define clrsetbits_be32(addr, clear, set) clrsetbits(be32, addr, clear, set)

#define clrbits_le32(addr, clear) clrbits(le32, addr, clear)
#define setbits_le32(addr, set) setbits(le32, addr, set)
#define clrsetbits_le32(addr, clear, set) clrsetbits(le32, addr, clear, set)

#define clrbits_be16(addr, clear) clrbits(be16, addr, clear)
#define setbits_be16(addr, set) setbits(be16, addr, set)
#define clrsetbits_be16(addr, clear, set) clrsetbits(be16, addr, clear, set)

#define clrbits_le16(addr, clear) clrbits(le16, addr, clear)
#define setbits_le16(addr, set) setbits(le16, addr, set)
#define clrsetbits_le16(addr, clear, set) clrsetbits(le16, addr, clear, set)

#define clrbits_8(addr, clear) clrbits(8, addr, clear)
#define setbits_8(addr, set) setbits(8, addr, set)
#define clrsetbits_8(addr, clear, set) clrsetbits(8, addr, clear, set)

#define __write(size,v,c) ({ uint ## size ## _t  __v = v; data_mem_barrier(); __arch_put(size, c, __v); __v; })
#define __read(size,c)    ({  uint ## size ## _t  __v = __arch_get(size,c); data_mem_barrier(); __v; })

#define writeb(v,c)    __write(8,v,c)
#define writew(v,c)    __write(16,v,c)
#define writel(v,c)     __write(32,v,c)
#define writeq(v,c)    __write(64,v,c)

#define readb(c)    __read(8,c)
#define readw(c)    __read(16,c)
#define readl(c)    __read(32,c)
#define readq(c)    __read(64,c)

#endif /* __USB_IO_H__ */
