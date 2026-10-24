/* Exercise the production reclaimer with FAT I/O failures and invalidation.
 * Include the implementation so its internal helper can be called directly;
 * unused entry points are discarded by the host test's section GC. */
#include "../../bdos/fsopnclo.c"

static void *escape[5];
static CLNO disk[2][8];
static CLNO pending_cluster;
static int fail_copy, fail_cluster, armed;
static BCB fat_buffer;
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
    return 0;
}
