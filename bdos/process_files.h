/* Canonical PROCESS descriptor owner. No user ABI and no fork-only table. */
#ifndef PROCESS_FILES_H
#define PROCESS_FILES_H
#include "fs.h"
#if CONF_WITH_PROCESS_FILES
struct kproc;
typedef struct kproc_file_snapshot KPROC_FILE_SNAPSHOT;
typedef struct {
    UQUAD generation, incarnation;
    WORD handle;
    UWORD state;
} PROCESS_FILE_BINDING;
typedef struct {
    struct kproc *owner; /* borrowed: this table is embedded in that identity */
    UQUAD number, epoch, snapshot;
    KPROC_FILE_SNAPSHOT *preparation; /* borrowed from this exact snapshot ticket */
    ULONG operations;
    BOOL retiring, exhausted, search_busy;
    UQUAD search_actor;
    SBYTE *standard; /* this KPROC's own UFT mirror, cleared at retirement */
    PROCESS_FILE_BINDING binding[NUMHANDLES];
} PROCESS_FILES;
/* Operation-owned exact binding; never a new descriptor or a raw close right.
 * No descriptor mutation admission is held during backend work. */
typedef struct {
    PROCESS_FILES *table;
    struct kproc *owner; /* retained across filesystem callbacks */
    UQUAD empty_generation;
    WORD descriptor;
    FS_FILE_REFERENCE reference;
    BOOL active;
} PROCESS_FILE_CLOSE;
#define PROCESS_FILE_CLOSE_INIT { NULL, NULL, 0, 0, FS_FILE_REFERENCE_INIT, FALSE }
/* Shared admission for descriptor and search-state operations. Identity
 * retention keeps this embedded table valid across retirement callbacks. */
typedef struct {
    PROCESS_FILES *table;
    struct kproc *owner;
    BOOL search;
} PROCESS_FILE_OPERATION;
#define PROCESS_FILE_OPERATION_INIT { NULL, NULL, FALSE }
LONG process_files_operation_begin(PD *,PROCESS_FILE_OPERATION *);
LONG process_files_search_begin(PD *,PROCESS_FILE_OPERATION *);
LONG process_files_cache_begin(PD *,PROCESS_FILE_OPERATION *);
void process_files_operation_end(PROCESS_FILE_OPERATION *);
void process_files_invalidate_epoch(PROCESS_FILES *);
void process_files_search_invalidate(PROCESS_FILES *,WORD);
#define PROCESS_FILES_STANDARD    1U /* descriptors 0..NUMSTD-1 only */
#define PROCESS_FILES_ALL         2U /* every descriptor and the DTA/search state (fork) */
#define PROCESS_FILES_DESCRIPTORS 3U /* every descriptor, no search state (Pexec) */
BOOL process_files_init(PROCESS_FILES *,struct kproc *,SBYTE *standard);
/* KPROC-owned accessors: no numeric PID/PD reincarnation grants authority. */
PROCESS_FILES *kproc_files(PD *pd);
PROCESS_FILES *kproc_ref_files(struct kproc *owner);
BOOL process_files_managed(PD *pd);
LONG process_files_change_admission(PD *pd);
LONG process_files_resolve(PD *,WORD descriptor,WORD *backend,UQUAD *incarnation);
LONG process_files_open_begin(PD *,WORD backend);
LONG process_files_open_end(PD *,WORD descriptor,WORD backend);
LONG process_files_bind_bios(PD *,WORD descriptor,WORD bios);
LONG process_files_force_bios(PD *,WORD descriptor,WORD bios);
LONG process_files_duplicate(PD *source,WORD descriptor,PD *target,WORD destination);
LONG process_files_close_begin(PD *,WORD descriptor,PROCESS_FILE_CLOSE *);
LONG process_files_close_end(PROCESS_FILE_CLOSE *,FS_CLOSE_REPORT *);
LONG process_files_close(PD *,WORD descriptor,BOOL dispose,FS_CLOSE_REPORT *);
void process_files_invalidate(PROCESS_FILES *,WORD backend,UQUAD incarnation);
void process_files_retire(PROCESS_FILES *);
void kproc_files_shutdown(void);
LONG kproc_files_test_count(UWORD field);
LONG process_files_inherit(PD *target,PD *source,UWORD policy);
LONG kproc_files_snapshot_begin(struct kproc *,KPROC_FILE_SNAPSHOT **);
BOOL kproc_files_snapshot_validate(const KPROC_FILE_SNAPSHOT *);
LONG kproc_files_snapshot_prepare(KPROC_FILE_SNAPSHOT *,struct kproc *target,UWORD policy);
LONG kproc_files_snapshot_seal(KPROC_FILE_SNAPSHOT *);
void kproc_files_snapshot_commit(KPROC_FILE_SNAPSHOT *,struct kproc *target);
void kproc_files_snapshot_end(KPROC_FILE_SNAPSHOT **);
#endif
#endif
