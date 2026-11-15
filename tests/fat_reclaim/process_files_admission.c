/* Production driver create/mkdir rejects snapshot admission before the first
 * directory write. The canonical descriptor admission state is modeled. */
/* The unused drive vtable entries must not emit target BIOS trap assembly
 * in this host test. Neither binding participates in mkdir cleanup. */
#define BIOSBIND_H
#define Mediach(drive) ((void)(drive), 0L)
#define Drvmap() 0L
#include "../../fs/fatfs_pfs.c"
#include "../../bdos/process_files.h"
static PD source_pd;
PD *run=&source_pd;
static PROCESS_FILES source_files;
PROCESS_FILES *kproc_files(PD *pd) { return pd==run?&source_files:NULL; }
BOOL kproc_file_references_allowed(PD *pd) { (void)pd;return TRUE; }

static DND parent, child;
static OFD temporary, directory, child_directory;
static FCB entry;
static int scans, deleted, released, writes, late_opens;
/* Keep both ordinary return paths visible to GCC13 flow analysis. Runtime
 * admission must reject before reaching this late failure at all. */
static volatile LONG late_open_error=EACCDN;
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
long ixwrite(OFD *fd, long size, void *buf) { (void)fd; (void)buf; writes++; return size; }
long ixread(OFD *fd, long size, void *buf)
{
    (void)fd; (void)size; (void)buf;
    return (long)&entry;
}
long ixclose(OFD *fd, int part) { (void)fd; (void)part; return E_OK; }
long xclose(WORD handle) { (void)handle; return E_OK; }
long opnfil(FCB *f,DND *dn,int mode)
{ (void)f;(void)dn;(void)mode;late_opens++;return late_open_error; }

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

char *packit(char *name,char *out) { (void)name;scans++;return out; }
OFD *ofd_find(DND *dn,long position) { (void)dn;(void)position;return NULL; }
DND *getdnd(char *name,DND *dn) { (void)name;(void)dn;return NULL; }
void xmfreblk(void *p) { (void)p;released++; }
FTAB sft[OPNFILES];
BOOL fs_sft_accessible(int n) { (void)n;return FALSE; }
void fs_sft_invalidated(int n) { (void)n;released++; }
void panic(const char *format,...) { (void)format;__builtin_trap(); }
extern int printf(const char *,...);
int main(void)
{
    PFSCOOKIE cookie,destination;
    LONG result;
    memset(&cookie,0,sizeof(cookie));
    parent.d_ofd=&directory;
    DND_TO_COOKIE(&parent,&cookie);
    source_files.snapshot=1;
    DND_TO_COOKIE(&child,&destination);
    result=fat_mkdir(&cookie,"DENIED");
    printf("fat_mkdir: return=%ld, directory writes before descriptor admission rejection=%d\n",(long)result,writes);
    if (result!=EACCDN || writes || scans || deleted || released || late_opens) return 1;
    result=fat_rename(&cookie,"SOURCE",&destination,"DEST");
    printf("fat_rename: return=%ld, directory writes/scans before admission rejection=%d/%d\n",(long)result,writes,scans);
    return result==EACCDN && writes==0 && !scans && !deleted && !released && !late_opens ? 0 : 1;
}
