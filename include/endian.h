#ifndef ENDIAN_H
#define ENDIAN_H
#define GCC_VERSION (__GNUC__ * 10000 \
                     + __GNUC_MINOR__ * 100 \
                     + __GNUC_PATCHLEVEL__)
                     
#define LITTLE_ENDIAN   1234    /* LSB first */
#define BIG_ENDIAN      4321    /* MSB first */

#if defined(__arm__) && ! defined(__armbe__)
#   define BYTE_ORDER   LITTLE_ENDIAN
#else
#   define BYTE_ORDER   BIG_ENDIAN
#endif



// Assuming GNUC
#if (GCC_VERSION >= 40800)
#   define bswap16              __builtin_bswap16
#else
#   define bswap16(x) ((((x)&0xff00)>>8)|(((x)&0xff)<<8))
#endif
/* bswap32/bswap64 are used unconditionally, unlike bswap16 above: both
 * __builtin_bswap32 and __builtin_bswap64 have been available since the
 * same GCC release (4.3), so guarding one without the other would protect
 * nothing -- a toolchain too old for __builtin_bswap64 is already too old
 * for the unconditional __builtin_bswap32 use below. */
#define bswap32         __builtin_bswap32
#define bswap64         __builtin_bswap64

#if BYTE_ORDER == BIG_ENDIAN
#   define le2cpu32(x) (bswap32(x))
#   define le2cpu16(x) (bswap16(x))
#   define cpu2le32(x) (bswap32(x))
#   define cpu2le16(x) (bswap16(x))
#   define le2cpu64(x) (bswap64(x))
#   define cpu2le64(x) (bswap64(x))
#   define be2cpu32(x) (x)
#   define be2cpu16(x) (x)
#   define cpu2be32(x) (x)
#   define cpu2be16(x) (x)
#elif BYTE_ORDER == LITTLE_ENDIAN
#   define be2cpu32(x) (bswap32(x))
#   define be2cpu16(x) (bswap16(x))
#   define cpu2be32(x) (bswap32(x))
#   define cpu2be16(x) (bswap16(x))
#   define le2cpu32(x) (x)
#   define le2cpu16(x) (x)
#   define cpu2le32(x) (x)
#   define cpu2le16(x) (x)
#   define le2cpu64(x) (x)
#   define cpu2le64(x) (x)
#else
# error unknown BYTE_ORDER
#endif

#endif // ENDIAN_H
