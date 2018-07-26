#ifndef ENDIAN_H
#define ENDIAN_H

#define LITTLE_ENDIAN	1234	/* LSB first */
#define BIG_ENDIAN	4321	/* MSB first */

#if defined(__arm__) && ! defined(__armbe__)
#   define BYTE_ORDER	LITTLE_ENDIAN
#else
#   define BYTE_ORDER	BIG_ENDIAN
#endif

// Assuming GNUC
#define bswap16		__builtin_bswap16
#define bswap32		__builtin_bswap32

#if BYTE_ORDER == BIG_ENDIAN
#   define le2cpu32(x) (bswap32(x))
#   define le2cpu16(x) (bswap16(x))
#   define cpu2le32(x) (bswap32(x))
#   define cpu2le16(x) (bswap16(x))
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
#else
# error unknown BYTE_ORDER
#endif

#endif // ENDIAN_H
