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

void *xmgetblk(WORD type)
{
    OFD *fd = &pool[allocated++];
    (void)type;
    memset(fd, 0, sizeof(*fd));
    return fd;
}
void xmfreblk(void *memory) { (void)memory; freed++; }
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
static int unlink_retry(int directory_failure, int lose_media)
{
    static DMD media;
    static DND directory;
    static OFD dirfile;
    FCB entry;
    int copy, cluster;
    memset(&entry, 0, sizeof(entry));
    memset(&directory, 0, sizeof(directory));
    memset(&dirfile, 0, sizeof(dirfile));
    allocated = freed = directory_erased = 0;
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

int main(void)
{
    static DFD file;
    static DMD media;
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
            free_file_chain(&file, &media);
            if (file.o_strtcl || (file.o_flag & O_RECLAIM_PENDING)) return 3;
            for (copy = 0; copy < 2; copy++)
                for (cluster = 2; cluster <= 4; cluster++)
                    if (disk[copy][cluster] != FREECLUSTER) return 4;
        }
    }
    for (cluster = 0; cluster <= 2; cluster++) {
        failed = unlink_retry(cluster, 0);
        if (failed) return failed;
        failed = unlink_retry(cluster, 1);
        if (failed) return failed;
    }
    return 0;
}
