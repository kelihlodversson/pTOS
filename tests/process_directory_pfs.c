/* Production PFS resolver/table; driver cookies have real counted ownership. */
#include <ctype.h>
#include "emutos.h"
#include "../fs/pfs.c"
#define CHECK(c) do { if (!(c)) { __builtin_printf("pfs:%d: %s\n",__LINE__,#c); __builtin_abort(); } } while (0)
static PD parent, child;
PD *run = &parent;
LONG drvbits;
static UBYTE parent_dirs[NUMCURDIR], child_dirs[NUMCURDIR];
static unsigned acquired, released;
static LONG lookup_error;
struct pfs_ops fat_pfs_ops;
UBYTE *kproc_curdir(PD *process)
{
    CHECK(process == &parent || process == &child);
    return process == &parent ? parent_dirs : child_dirs;
}
size_t strlcpy(char *out, const char *in, size_t cap)
{
    size_t n = strlen(in);
    if (cap) { size_t copied = n < cap-1 ? n : cap-1; memcpy(out,in,copied); out[copied]=0; }
    return n;
}
static LONG test_root(struct pfs_ops *fs, WORD drive, PFSCOOKIE *out)
{
    CHECK(run == &parent);
    out->fs=fs; out->index=++acquired; out->aux=drive; out->pos=0;
    return E_OK;
}
static LONG lookup(PFSCOOKIE *dir, const char *path, PFSCOOKIE *out)
{
    CHECK(run == &parent && path && dir->index > 0);
    if (lookup_error) return lookup_error;
    *out=*dir; out->index=++acquired;
    return E_OK;
}
static void release(PFSCOOKIE *cookie)
{
    CHECK(cookie->index > 0 && released < acquired);
    cookie->index=0; released++;
}
int main(void)
{
    WORD old, i;
    PFSCOOKIE cookie;
    unsigned baseline;
    fat_pfs_ops.root=test_root; fat_pfs_ops.lookup=lookup; fat_pfs_ops.release=release;
    parent.p_curdrv=child.p_curdrv=2;
    CHECK(pfs_do_chdir("C:\\PARENT") == E_OK);
    old=parent_dirs[2]; child_dirs[2]=old; pfs_cwd_addref(old);
    CHECK(pfs_set_process_directory("D:\\CHILD",&child) == E_OK);
    CHECK(child.p_curdrv == 3 && parent.p_curdrv == 2 && !parent_dirs[3]);
    CHECK(parent_dirs[2] == old && pfs_dirtbl[old].use == 2);
    CHECK(!strcmp(pfs_dirtbl[child_dirs[3]].path,"CHILD"));
    lookup_error=EPTHNF; baseline=acquired-released;
    CHECK(pfs_set_process_directory("E:\\BAD",&child) == EPTHNF);
    CHECK(child.p_curdrv == 3 && !child_dirs[4] && acquired-released == baseline);
    lookup_error=0;
    CHECK(pfs_set_process_directory("E:\\*",&child) == EPTHNF);
    CHECK(pfs_set_process_directory("!:BAD",&child) == EDRIVE);
    CHECK(pfs_set_process_directory("",&child) == E_OK);
    CHECK(acquired-released == baseline); /* borrowed CWD is never freed/re-adopted */
    /* Exhaust the real table with independent driver references. */
    for (i=1; i<PFS_MAX_CWD; i++) if (!pfs_dirtbl[i].use) {
        CHECK(test_root(&fat_pfs_ops,2,&cookie) == E_OK);
        pfs_dirtbl[i].use=1; pfs_dirtbl[i].fs=&fat_pfs_ops; pfs_dirtbl[i].cwd=cookie;
    }
    baseline=acquired-released;
    CHECK(pfs_set_process_directory("C:\\REPLACE",&child) == ENSMEM);
    CHECK(child_dirs[2] == old && pfs_dirtbl[old].use == 2 && acquired-released == baseline);
    CHECK(child.p_curdrv == 3 && parent_dirs[2] == old);
    /* Its unique old D: slot may be replaced without a spare; root needs none. */
    CHECK(pfs_set_process_directory("D:\\REPLACE",&child) == E_OK);
    CHECK(acquired-released == baseline);
    CHECK(pfs_set_process_directory("D:\\",&child) == E_OK && !child_dirs[3]);
    CHECK(acquired-released == baseline-1);
    for (i=1; i<PFS_MAX_CWD; i++) while (pfs_dirtbl[i].use) pfs_dirtbl_release(i);
    CHECK(acquired == released && run == &parent);
    __builtin_printf("Production PFS child CWD: isolation, shared ownership, full-table rollback and cookie reclamation: PASS");
    return 0;
}
