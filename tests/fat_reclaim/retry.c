/* Exercise the production reclaimer with FAT I/O failures and invalidation.
 * Include the implementation so its internal helper can be called directly;
 * unused entry points are discarded by the host test's section GC. */
#include "../../bdos/fsopnclo.c"

static void *escape[5];
static CLNO disk[2][8];
static CLNO pending_cluster;
static int fail_copy, fail_cluster, armed;
static BCB fat_buffer, directory_buffer;
static OFD pool[4];
static int allocated, freed, fail_directory, directory_erased;
FTAB sft[OPNFILES];
static DND *allocation_directory;
static int scavenged;
static void *directory_owned;
static int freed_directory;

void *xmgetblk(WORD type)
{
    OFD *fd = &pool[allocated++];
    (void)type;
    /* Model the pool's directory-scavenging opportunity before linking. */
    if (allocation_directory && !(allocation_directory->d_flag & DND_LOCKED))
        scavenged = 1;
    memset(fd, 0, sizeof(*fd));
    return fd;
}
void xmfreblk(void *memory)
{
    if (memory == directory_owned) freed_directory = 1;
    freed++;
}
long ixlseek(OFD *fd, long offset) { fd->o_bytnum = offset; return offset; }
long ixwrite(OFD *fd, long size, void *buffer)
{
    (void)fd; (void)buffer;
    directory_buffer.b_bufdrv = 0;
    directory_buffer.b_dirty = 1;
    return size;
}
long ixread(OFD *fd, long size, void *buffer)
{
    (void)fd; (void)size; (void)buffer;
    return 0;
}
BCB *bufl[2];

CLNO getrealcl(CLNO cluster, DMD *media)
{
    (void)media;
    return disk[1][cluster];
}

void clfix(CLNO cluster, CLNO value, DMD *media)
{
    (void)value;
    pending_cluster = cluster;
    fat_buffer.b_dm = media;
    fat_buffer.b_bufdrv = 0;
    fat_buffer.b_dirty = 1;
}

void flush(BCB *b)
{
    int copy;
    b->b_bufdrv = -1;
    if (b == &directory_buffer) {
        if (fail_directory != 1) directory_erased = 1;
        if (fail_directory) {
            fail_directory = 0;
            __builtin_longjmp(escape, 1);
        }
        b->b_bufdrv = 0; b->b_dirty = 0;
        return;
    }
    for (copy = 0; copy < 2; copy++) {
        if (armed && copy == fail_copy && pending_cluster == fail_cluster) {
            armed = 0;
            __builtin_longjmp(escape, 1);
        }
        disk[copy][pending_cluster] = FREECLUSTER;
    }
    b->b_bufdrv = 0;
    b->b_dirty = 0;
}

/* Drive the real unlink entry point, not just its FAT helper. */
static int unlink_retry(int directory_failure, int lose_media, int locked)
{
    static DMD media;
    static DND directory;
    static OFD dirfile;
    FCB entry;
    int copy, cluster;
    memset(&entry, 0, sizeof(entry));
    memset(&directory, 0, sizeof(directory));
    memset(&dirfile, 0, sizeof(dirfile));
    allocated = freed = directory_erased = scavenged = 0;
    allocation_directory = &directory;
    directory.d_flag = locked ? DND_LOCKED : 0;
    dirfile.o_dfd = &dirfile.o_disk;
    directory.d_ofd = &dirfile;
    directory.d_drv = &media;
    media.m_dtl = &directory;
    entry.f_clust = cpu2le16(2);
    for (copy = 0; copy < 2; copy++)
        for (cluster = 2; cluster <= 4; cluster++)
            disk[copy][cluster] = cluster == 4 ? 0xffff : cluster + 1;
    bufl[BI_DATA] = &directory_buffer;
    directory_buffer.b_link = NULL;
    directory_buffer.b_dirty = 0;
    fail_directory = directory_failure;
    fail_copy = 1; fail_cluster = 3; armed = !directory_failure;
    if (!__builtin_setjmp(escape)) {
        ixdel(&directory, &entry, 0);
        return 5;
    }
    if (scavenged || !!(directory.d_flag & DND_LOCKED) != locked) return 12;
    if (!pending_unlinks || allocated != 1 || freed) return 6;
    if (directory_failure &&
        (pending_unlinks->o_dfd->o_flag & (O_UNLINKED | O_CLOSING))) return 7;
    if (!directory_failure &&
        !(pending_unlinks->o_dfd->o_flag & O_CLOSING)) return 8;
    if (lose_media) {
        ofd_invalidate_drive(&media);
        if (pending_unlinks || freed != allocated) return 9;
        return 0;
    }
    ofd_reap();
    if (pending_unlinks || !directory_erased || freed != allocated) return 10;
    for (copy = 0; copy < 2; copy++)
        for (cluster = 2; cluster <= 4; cluster++)
            if (disk[copy][cluster] != FREECLUSTER) return 11;
    return 0;
}

/* Directory metadata and the mkdir handle must retain separate ownership;
 * media cleanup must also find a handle detached before a failed flush. */
static int invalidate_mkdir(int detached)
{
    static DMD media, other_media;
    static DND directory;
    static OFD dirfile, temporary, unrelated;
    memset(&directory, 0, sizeof(directory));
    memset(&dirfile, 0, sizeof(dirfile));
    memset(&temporary, 0, sizeof(temporary));
    memset(&unrelated, 0, sizeof(unrelated));
    memset(sft, 0, sizeof(sft));
    directory.d_ofd = &dirfile;
    directory.d_files = detached ? NULL : &temporary;
    media.m_dtl = &directory;
    dirfile.o_dfd = &dirfile.o_disk;
    dirfile.o_disk.o_usecnt = 1;
    temporary.o_dfd = &temporary.o_disk;
    temporary.o_disk.o_usecnt = 1;
#if CONF_WITH_FILE_PINS
    temporary.o_refs = 1;
#endif
    temporary.o_dmd = &media;
    temporary.o_dnode = &directory;
    unrelated.o_dmd = &other_media;
    sft[0].f_ofd = sft[1].f_ofd = &temporary;
    sft[0].f_use = sft[1].f_use = 1;
    sft[2].f_ofd = &unrelated;
    sft[3].f_ofd = (OFD *)-3L;
    directory_owned = &dirfile;
    freed = freed_directory = 0;
    ofd_invalidate_drive(&media);
    directory_owned = NULL;
    if (freed != 1 || freed_directory || dirfile.o_disk.o_usecnt != 1)
        return 16;
    if (sft[0].f_ofd || sft[1].f_ofd || sft[0].f_use || sft[1].f_use ||
        sft[2].f_ofd != &unrelated || sft[3].f_ofd != (OFD *)-3L ||
        directory.d_files) return 17;
    memset(sft, 0, sizeof(sft));
    return 0;
}

int main(void)
{
    static DFD file;
    static DMD media;
    static DND root, child, sibling;
    static OFD handle;
    int copy, cluster, failed;
    for (fail_copy = 0; fail_copy < 2; fail_copy++) {
        for (fail_cluster = 2; fail_cluster <= 4; fail_cluster++) {
            for (copy = 0; copy < 2; copy++)
                for (cluster = 2; cluster <= 4; cluster++)
                    disk[copy][cluster] = cluster == 4 ? 0xffff : cluster + 1;
            file.o_flag = O_UNLINKED;
            file.o_strtcl = 2;
            bufl[BI_FAT] = &fat_buffer;
            fat_buffer.b_link = NULL;
            armed = 1;
            failed = __builtin_setjmp(escape);
            if (!failed) {
                free_file_chain(&file, &media);
                return 1;               /* injection must interrupt reclaim */
            }
            if (file.o_strtcl != fail_cluster ||
                !(file.o_flag & O_RECLAIM_PENDING) ||
                file.o_reclaim_next != (fail_cluster == 4 ? 0xffff : fail_cluster + 1))
                return 2;
            /* Model a failed handle-owned final close, outside both retry
             * lists. It remains visible even with mapping support disabled. */
            root.d_left = &child;
            child.d_right = &sibling;
            sibling.d_files = &handle;
            handle.o_dfd = &file;
            media.m_dtl = &root;
            if (!ofd_cluster_pending(media.m_dtl, fail_cluster) ||
                ofd_cluster_pending(media.m_dtl, fail_cluster + 1) ||
                ofd_cluster_pending(NULL, fail_cluster)) return 13;
            if (fail_copy == 1 && disk[0][fail_cluster] != FREECLUSTER)
                return 14;
            free_file_chain(&file, &media);
            if (ofd_cluster_pending(media.m_dtl, fail_cluster)) return 15;
            if (file.o_strtcl || (file.o_flag & O_RECLAIM_PENDING)) return 3;
            for (copy = 0; copy < 2; copy++)
                for (cluster = 2; cluster <= 4; cluster++)
                    if (disk[copy][cluster] != FREECLUSTER) return 4;
        }
    }
    for (cluster = 0; cluster <= 2; cluster++) {
        for (copy = 0; copy < 2; copy++) {
            failed = unlink_retry(cluster, 0, copy);
            if (failed) return failed;
            failed = unlink_retry(cluster, 1, copy);
            if (failed) return failed;
        }
    }
    for (cluster = 0; cluster < 2; cluster++) {
        failed = invalidate_mkdir(cluster);
        if (failed) return failed;
    }
    return 0;
}
