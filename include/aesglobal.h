/* Public AES global state. Copyright (C) 2026 the pTOS development team.
 * GPL version 2 or later. May be included by GEM bindings without kernel types. */
#ifndef PTOS_AESGLOBAL_H
#define PTOS_AESGLOBAL_H

#include <stddef.h>
#include <stdint.h>

struct _object;
struct rshdr;

typedef struct aesglobal {
    int16_t ap_version;             /* AES version */
    int16_t ap_count;               /* maximum concurrent applications */
    int16_t ap_id;                  /* application id */
    int32_t ap_private;             /* owned by the application */
    struct _object **ap_ptree;      /* selected resource tree table */
    struct rshdr *ap_rscmem;        /* selected native resource image */
    uint16_t ap_rsclen;             /* resource length */
    int16_t ap_planes;              /* colour planes */
    void *ap_3resv;                 /* reserved for libaes context; kernel AES area D */
    int32_t ap_4resv;               /* reserved by AES 4.00 */
#if defined(__m68k__)
} __attribute__((packed, aligned(2))) AESGLOBAL;
#else
} AESGLOBAL;
#endif

/* C90-compatible assertions also apply to every consumer of this public ABI. */
#define AESGLOBAL_ASSERT(name, condition) \
    typedef char aesglobal_assert_##name[(condition) ? 1 : -1]
#define AESGLOBAL_OFFSET(member, offset) \
    AESGLOBAL_ASSERT(member, offsetof(AESGLOBAL, member) == (offset))
AESGLOBAL_ASSERT(word_width, sizeof(((AESGLOBAL *)0)->ap_version) == 2);
AESGLOBAL_ASSERT(count_width, sizeof(((AESGLOBAL *)0)->ap_count) == 2);
AESGLOBAL_ASSERT(id_width, sizeof(((AESGLOBAL *)0)->ap_id) == 2);
AESGLOBAL_ASSERT(length_width, sizeof(((AESGLOBAL *)0)->ap_rsclen) == 2);
AESGLOBAL_ASSERT(planes_width, sizeof(((AESGLOBAL *)0)->ap_planes) == 2);
AESGLOBAL_ASSERT(trees_width, sizeof(((AESGLOBAL *)0)->ap_ptree) == sizeof(void *));
AESGLOBAL_ASSERT(image_width, sizeof(((AESGLOBAL *)0)->ap_rscmem) == sizeof(void *));
AESGLOBAL_ASSERT(private_width, sizeof(((AESGLOBAL *)0)->ap_private) == 4);
AESGLOBAL_ASSERT(reserved_width, sizeof(((AESGLOBAL *)0)->ap_4resv) == 4);
AESGLOBAL_ASSERT(context_width, sizeof(((AESGLOBAL *)0)->ap_3resv) == sizeof(void *));
AESGLOBAL_OFFSET(ap_version, 0);
AESGLOBAL_OFFSET(ap_count, 2);
AESGLOBAL_OFFSET(ap_id, 4);
#if defined(__m68k__)
AESGLOBAL_ASSERT(pointer_width, sizeof(void *) == 4);
AESGLOBAL_ASSERT(size, sizeof(AESGLOBAL) == 30);
AESGLOBAL_ASSERT(alignment, __alignof__(AESGLOBAL) == 2);
AESGLOBAL_OFFSET(ap_private, 6);
AESGLOBAL_OFFSET(ap_ptree, 10);
AESGLOBAL_OFFSET(ap_rscmem, 14);
AESGLOBAL_OFFSET(ap_rsclen, 18);
AESGLOBAL_OFFSET(ap_planes, 20);
AESGLOBAL_OFFSET(ap_3resv, 22);
AESGLOBAL_OFFSET(ap_4resv, 26);
#elif UINTPTR_MAX == UINT32_MAX
AESGLOBAL_ASSERT(size, sizeof(AESGLOBAL) == 32);
AESGLOBAL_ASSERT(alignment, __alignof__(AESGLOBAL) == __alignof__(void *));
AESGLOBAL_OFFSET(ap_private, 8);
AESGLOBAL_OFFSET(ap_ptree, 12);
AESGLOBAL_OFFSET(ap_rscmem, 16);
AESGLOBAL_OFFSET(ap_rsclen, 20);
AESGLOBAL_OFFSET(ap_planes, 22);
AESGLOBAL_OFFSET(ap_3resv, 24);
AESGLOBAL_OFFSET(ap_4resv, 28);
#elif UINTPTR_MAX == UINT64_MAX
AESGLOBAL_ASSERT(size, sizeof(AESGLOBAL) == 56);
AESGLOBAL_ASSERT(alignment, __alignof__(AESGLOBAL) == __alignof__(void *));
AESGLOBAL_OFFSET(ap_private, 8);
AESGLOBAL_OFFSET(ap_ptree, 16);
AESGLOBAL_OFFSET(ap_rscmem, 24);
AESGLOBAL_OFFSET(ap_rsclen, 32);
AESGLOBAL_OFFSET(ap_planes, 34);
AESGLOBAL_OFFSET(ap_3resv, 40);
AESGLOBAL_OFFSET(ap_4resv, 48);
#else
#error Unsupported AESGLOBAL pointer representation
#endif
#undef AESGLOBAL_OFFSET
#undef AESGLOBAL_ASSERT
#endif
