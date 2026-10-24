/*
 * Native FAT backing identity and lifetime, independent of user file handles.
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "emutos.h"
#include "string.h"
#include "fs.h"
#include "gemerror.h"
#include "kheap.h"
#include "fileback.h"

struct file_backing {
    struct file_backing *next;
    OFD *file;
    ULONG references, size;
    BOOL current;
};
static FILE_BACKING *backings;

/* A failed final close retains its OFD pin for retry, not a dangling pointer.
 * A later mapping operation retries it; no new mapping reuses a retired object. */
void fileback_reap(void)
{
    FILE_BACKING **link = &backings, *b;
    while ((b = *link)) {
        if (!b->references && DISK_CALL(ofd_put(b->file)) == E_OK) {
            *link = b->next; kfree(b);
        } else link = &b->next;
    }
}

LONG fileback_get(LONG handle, FILE_BACKING **out)
{
    FILE_BACKING *b;
    OFD *fd;
    *out = NULL;
    fileback_reap();
    if (handle < 0 || handle >= NUMHANDLES) return EIHNDL;
#if CONF_WITH_PLUGGABLE_FS
    if (getpfsslot(handle)) return EOPNOTSUPP;
#endif
    fd = getofd(handle);
    if (!fd || IS_BIOS_HANDLE(fd)) return EIHNDL;
    if ((fd->o_mod & MODE_FAC) == WO_MODE || (fd->o_dfd->o_flag & (O_MEDIA_LOST | O_CLOSING))) return EACCDN;
    if (fd->o_dfd->o_fileln < 0) return EOPNOTSUPP;
    for (b = backings; b; b = b->next)
        if (b->references && b->current && b->file->o_dfd == fd->o_dfd) {
            b->references++; *out = b; return E_OK;
        }
    b = kalloc(sizeof(*b));
    if (!b) return ENSMEM;
    if (!ofd_hold(fd)) { kfree(b); return EACCDN; }
    b->file = fd; b->references = 1; b->size = fd->o_dfd->o_fileln; b->current = TRUE;
    b->next = backings; backings = b; *out = b;
    return E_OK;
}
void fileback_hold(FILE_BACKING *b) { if (b) b->references++; }
void fileback_put(FILE_BACKING *b)
{
    if (!b) return;
    if (!--b->references) b->current = FALSE;
    fileback_reap();
}
ULONG fileback_size(const FILE_BACKING *b) { return b->size; }

/* A mutation starts a new cache generation. Existing MAP_PRIVATE views need
 * not reflect subsequent writes; they never write modifications to the file. */
void fileback_changed(void *dfd)
{
    FILE_BACKING *b;
    for (b = backings; b; b = b->next)
        if (b->file->o_dfd == dfd) b->current = FALSE;
}
LONG fileback_read(FILE_BACKING *b, ULONG offset, void *buffer, ULONG size)
{
    OFD *fd = b->file;
    long position = fd->o_bytnum, rc;
    CLNO cluster = fd->o_curcl;
    RECNO record = fd->o_currec;
    UWORD byte = fd->o_curbyt;
    ULONG count;
    if ((fd->o_dfd->o_flag & (O_MEDIA_LOST | O_CLOSING)) || offset >= b->size) return EREADF;
    count = b->size - offset < size ? b->size - offset : size;
    memset(buffer, 0, size);
    rc = DISK_CALL(__extension__ ({
        long r = ixlseek(fd, offset);
        r < 0 ? r : ixread(fd, count, buffer);
    }));
    /* Faults and kernel buffer accesses must never move the user handle. */
    fd->o_bytnum = position; fd->o_curcl = cluster; fd->o_currec = record; fd->o_curbyt = byte;
    return rc == count ? E_OK : EREADF;
}
