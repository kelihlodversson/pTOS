/* Validated ELF32 load plans for the userspace runtime linker (#660). */
#ifndef LDSO_IMAGE_H
#define LDSO_IMAGE_H

#include "dynamic.h"

#define DL_MAX_PHDRS 128
#define DL_MAX_LOADS 32
#define DL_MAX_INTERP 256
#define DL_IMAGE_DSO 1
#define DL_IMAGE_INTERPRETER 2
#define DL_IMAGE_EXECUTABLE 3
#define DL_PF_X 1
#define DL_PF_W 2
#define DL_PF_R 4

/* eflags_required are the e_flags bits every admitted object must have, and
 * eflags_allowed all those it may (a superset of the required ones). */
typedef struct {
    uint32_t machine, endian, page_size, max_span;
    uint32_t eflags_required, eflags_allowed;
} DL_IMAGE_ABI;

typedef struct {
    uint32_t offset, address, filesz, memsz, flags, align;
} DL_LOAD;

typedef struct {
    uint32_t base, size, entry, dynamic, dynamic_size;
    uint32_t interp_offset, interp_size, phdr, phnum, align;
    uint32_t relro, relro_size, nload, type;
    DL_LOAD loads[DL_MAX_LOADS];
} DL_IMAGE;

extern const DL_IMAGE_ABI dl_image_abi;

/* Inspect a complete immutable ELF file without section headers or allocation.
 * Output is cleared on error. DSO/interpreter roles require ET_DYN without
 * PT_INTERP; interpreter and executable entries must lie in executable bytes.
 * Executables require PT_INTERP and mapped program headers (ET_EXEC or PIE).
 * The initial subset rejects overlapping memory extents, conflicting page
 * permissions, executable stacks, TLS and pTOS-private relocation/imports.
 * Only readable segments are admitted. All spans/alignments are checked.
 * PT_DYNAMIC must have equal file/memory sizes, be file-backed and agree
 * with its containing PT_LOAD.
 * RELRO describes whole pages ending at floor(p_vaddr+p_memsz), as in ELF;
 * the caller applies it after binding, not before writing relocations.
 */
int dl_image_read(DL_IMAGE *out, const unsigned char *file, uint32_t length,
                    uint32_t role, const DL_IMAGE_ABI *abi);

/* Does one readable load segment contain this entire link-time range?
 * flags are required ELF PF_* bits, not GEMDOS PROT_* bits. */
int dl_image_range(const DL_IMAGE *plan, uint32_t address, uint32_t size,
                    uint32_t flags);

int dl_image_file_range(const DL_IMAGE *plan, uint32_t address, uint32_t size,
                            uint32_t flags);

/* Populate an already allocated private RW span, zeroing holes and BSS.
 * plan must be an unmodified successful dl_image_read result for this same
 * immutable file; image/plan/file/output must not alias. bias is the target
 * load bias, independent of the host test buffer, and must preserve alignment.
 * ET_EXEC requires bias zero. Destination capacity and runtime address bounds
 * are checked before writing. Then read dynamic metadata, checking that its
 * tables are actually in readable file-backed segments, not zero-filled gaps.
 * On failure output is cleared; caller discards the private mapping. This
 * does not allocate, seal pages, relocate, call user code or perform file I/O.
 */
int dl_image_copy(const DL_IMAGE *plan, const unsigned char *file, uint32_t length,
                    unsigned char *image, uint32_t capacity, uint32_t bias,
                    DL_OBJECT *object);

/* Read a validated, already populated private image (kernel-owned startup). */
int dl_image_view(const DL_IMAGE *plan, const unsigned char *image, uint32_t bias,
                    DL_OBJECT *object);

#endif
