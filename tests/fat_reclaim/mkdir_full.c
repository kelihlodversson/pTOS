/* Full-disk mkdir cleanup must not reuse the temporary OFD after ixdel.
 * Exercise the production FAT path with the deleted descriptor poisoned. */
/* The unused drive vtable entries must not emit target BIOS trap assembly
 * in this host test. Neither binding participates in mkdir cleanup. */
#define BIOSBIND_H
#define Mediach(drive) ((void)(drive), 0L)
#define Drvmap() 0L
#include "../../fs/fatfs_pfs.c"

static DND parent, child;
static OFD temporary, directory, child_directory;
static FCB entry;
static int scans, deleted, released;
UWORD current_time, current_date;

BOOL contains_illegal_characters(const char *name) { (void)name; return FALSE; }
void *scan(DND *dn, const char *name, WORD attr, LONG *pos)
{
    (void)dn; (void)name; (void)attr;
    if (!scans++) return NULL;
    *pos = 32;
    return &entry;
}
void builds(const char *name, char *out) { (void)name; memcpy(out, "FULL       ", 11); }
long ixlseek(OFD *fd, long pos) { fd->o_bytnum = pos; return pos; }
long ixwrite(OFD *fd, long size, void *buf) { (void)fd; (void)buf; return size; }
long ixread(OFD *fd, long size, void *buf)
{
    (void)fd; (void)size; (void)buf;
    return (long)&entry;
}
long ixclose(OFD *fd, int part) { (void)fd; (void)part; return E_OK; }
long xclose(WORD handle) { (void)handle; return E_OK; }
long opnfil(FCB *f, DND *dn, int mode)
{
    (void)f; (void)mode;
    temporary.o_dnode = dn;
    temporary.o_dirfil = &directory;
    temporary.o_dfd = &temporary.o_disk;
    return NUMSTD;
}
OFD *getofd(int handle) { (void)handle; return &temporary; }
DND *makdnd(DND *dn, FCB *f)
{
    (void)f;
    dn->d_left = &child;
    child.d_parent = dn;
    return &child;
}
OFD *makofd(DND *dn)
{
    dn->d_ofd = &child_directory;
    child_directory.o_dfd = &child_directory.o_disk;
    return &child_directory;
}
int nextcl(OFD *fd, int writing) { (void)fd; (void)writing; return 1; }
FCB *dirinit(DND *dn) { (void)dn; return &entry; }
long ixdel(DND *dn, FCB *f, long pos)
{
    (void)f; (void)pos;
    if (dn != &parent) return EACCDN;
    deleted++;
    /* Model an OFD released/reused by the traditional deletion path. */
    memset(&temporary, 0, sizeof(temporary));
    return E_OK;
}
void freednd(DND *dn)
{
    if (dn == &child && parent.d_left == NULL) released++;
}

int main(void)
{
    PFSCOOKIE cookie;
    memset(&cookie, 0, sizeof(cookie));
    memcpy(entry.f_name, "FULL       ", 11);
    parent.d_ofd = &directory;
    DND_TO_COOKIE(&parent, &cookie);
    if (fat_mkdir(&cookie, "FULL") != EACCDN) return 1;
    if (deleted != 1 || released != 1 || parent.d_left) return 2;
    return 0;
}
