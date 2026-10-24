/* Userspace eager ELF32 RELA relocation planning (#660). */
#ifndef LDSO_RELOCATE_H
#define LDSO_RELOCATE_H

#include "dynamic.h"

#define DL_MAX_RELOCATIONS 4096
#define DL_MAX_WRITE_RANGES 32
#define DL_RELOC_SYMBOL   1
#define DL_RELOC_RELATIVE 2
#define DL_RELOC_PLT      4

typedef struct {
    uint32_t address, size;           /* link-time virtual addresses */
} DL_WRITE_RANGE;

typedef struct {
    uint32_t offset, width;           /* byte offset in image, 4 or 8 bytes */
    uint64_t value;
} DL_RELOC_WRITE;

typedef struct {
    uint32_t width, flags;
} DL_RELOC_KIND;

typedef struct {
    int (*kind)(uint32_t type, DL_RELOC_KIND *kind);
    int (*value)(uint32_t type, uint32_t bias, uint32_t symbol,
                 int32_t addend, uint32_t place, uint64_t *value);
} DL_RELOC_BACKEND;

extern const DL_RELOC_BACKEND dl_x32_reloc_backend;

/* Eagerly process DT_RELA and DT_JMPREL without section headers or libc.
 * image must be the mutable backing of object->image. ranges must come from
 * validated private writable segments, never shared text. Caller-owned scratch
 * must be disjoint from every loaded image, scope and metadata. All views,
 * strings, ranges and scope must remain stable for the duration of this call.
 *
 * Validate and plan every write before committing any. Failure leaves images
 * unchanged and sets *written to zero; scratch may be overwritten. Reject
 * overlapping destinations and writes into ELF metadata/relocation tables.
 * A DT_JMPREL subrange of DT_RELA is processed once, eagerly. Capacity and
 * resource-limit failures are explicit. The caller later applies RELRO/cache
 * operations and transfers control; this function does not call constructors.
 */
int dl_relocate(const DL_OBJECT *object, unsigned char *image,
                const DL_OBJECT *const *scope, uint32_t scope_count,
                const DL_WRITE_RANGE *ranges, uint32_t range_count,
                DL_RELOC_WRITE *scratch, uint32_t capacity, uint32_t *written,
                const DL_RELOC_BACKEND *backend);

#endif
