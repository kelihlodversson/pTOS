/* Production PFS resolver/table; driver cookies have real counted ownership. */
#include <ctype.h>
#include "emutos.h"
#include "../fs/pfs.c"
#define CHECK(c) do { if (!(c)) { __builtin_printf("pfs:%d: %s\n",__LINE__,#c); __builtin_abort(); } } while (0)
static PD parent, child, grandchild;
PD *run = &parent;
LONG drvbits;
static UBYTE parent_dirs[NUMCURDIR], child_dirs[NUMCURDIR], grandchild_dirs[NUMCURDIR];
static unsigned char live[4096];
static BOOL release_reentry;
static unsigned acquired, released;
static LONG lookup_error;
static PD *retiring;
BOOL kproc_file_references_allowed(PD *pd) { return pd != retiring; }
struct pfs_ops fat_pfs_ops;
UBYTE *kproc_curdir(PD *process)
{
    CHECK(process == &parent || process == &child || process == &grandchild);
    return process == &parent ? parent_dirs : process == &child ? child_dirs : grandchild_dirs;
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
    out->fs=fs; out->index=++acquired; live[acquired]=1; out->aux=drive; out->pos=0;
    return E_OK;
}
static LONG lookup(PFSCOOKIE *dir, const char *path, PFSCOOKIE *out)
{
    CHECK(run == &parent && path && dir->index > 0);
    if (lookup_error) return lookup_error;
    *out=*dir; out->index=++acquired; live[acquired]=1;
    return E_OK;
}
static void release(PFSCOOKIE *cookie)
{
    CHECK(cookie->index > 0 && released < acquired && live[cookie->index]);
    live[cookie->index]=0; cookie->index=0; released++;
    if (release_reentry) {
        WORD i;
        CHECK(retiring == &child);
        for(i=0;i<NUMCURDIR;i++) CHECK(!child_dirs[i]);
        CHECK(pfs_set_process_directory("C:\\REENTER",&child) == EACCDN);
        CHECK(pfs_cwd_inherit(&child,&parent) == EACCDN);
        pfs_proc_exit(&child); /* duplicate retirement cannot consume again */
    }
}
int main(void)
{
    WORD old, i;
    PFSCOOKIE cookie;
    char path[LEN_ZPATH];
    unsigned baseline;
    fat_pfs_ops.root=test_root; fat_pfs_ops.lookup=lookup; fat_pfs_ops.release=release;
    parent.p_curdrv=child.p_curdrv=2;
    CHECK(pfs_do_chdir("C:\\PARENT") == E_OK);
    old=parent_dirs[2]; CHECK(pfs_do_getdir(path,3) == E_OK && !strcmp(path,"PARENT"));
    CHECK(pfs_cwd_inherit(&child,&parent) == E_OK);
    pfs_dirtbl[old].use=0x7fff;
    CHECK(pfs_cwd_inherit(&grandchild,&parent) == ENSMEM && !grandchild_dirs[2]);
    pfs_dirtbl[old].use=2;
    parent_dirs[4]=old; /* valid earlier drive must not partially increment */
    CHECK(pfs_cwd_inherit(&grandchild,&parent) == EIMBA &&
          !grandchild_dirs[2] && pfs_dirtbl[old].use == 2);
    parent_dirs[4]=0;
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
    memset(parent_dirs,0,sizeof(parent_dirs)); memset(child_dirs,0,sizeof(child_dirs));
    CHECK(acquired == released && run == &parent);
    /* A stale empty small index must not consume its freshly reused slot. */
    parent_dirs[2]=1;
    CHECK(pfs_do_chdir("C:\\STALE") == E_OK && parent_dirs[2] == 1 &&
          pfs_dirtbl[1].use == 1 && acquired == released+1);
    pfs_proc_exit(&parent); CHECK(acquired == released);
    /* Parent-first release and nested inheritance never release a foreign slot. */
    CHECK(pfs_do_chdir("C:\\PARENT") == E_OK);
    CHECK(pfs_cwd_inherit(&child,&parent) == E_OK);
    CHECK(pfs_cwd_inherit(&grandchild,&child) == E_OK);
    old=child_dirs[2]; CHECK(pfs_dirtbl[old].use == 3);
    pfs_proc_exit(&parent); CHECK(pfs_dirtbl[old].use == 2 && !parent_dirs[2]);
    CHECK(pfs_set_process_directory("C:\\CHILD",&child) == E_OK);
    CHECK(pfs_dirtbl[old].use == 1 && grandchild_dirs[2] == old);
    retiring=&child; release_reentry=TRUE;
    pfs_proc_exit(&child);
    release_reentry=FALSE; retiring=NULL;
    CHECK(!child_dirs[2] && grandchild_dirs[2] == old);
    pfs_proc_exit(&grandchild); CHECK(acquired == released);
    /* Media change retains tombstones until all old owners settle. A reused
     * PD and a newly installed directory cannot steal an old owner's slot. */
    CHECK(pfs_do_chdir("C:\\OLDMEDIA") == E_OK);
    CHECK(pfs_cwd_inherit(&child,&parent) == E_OK);
    old=parent_dirs[2]; baseline=released;
    pfs_invalidate_fat_drive(2);
    CHECK(released == baseline+1 && pfs_dirtbl[old].use == 2 && !pfs_dirtbl[old].fs);
    CHECK(pfs_cwd_inherit(&grandchild,&parent) == E_OK && !grandchild_dirs[2]);
    CHECK(pfs_do_chdir("C:\\NEWMEDIA") == E_OK && parent_dirs[2] != old);
    CHECK(pfs_dirtbl[old].use == 1);
    pfs_proc_exit(&child); CHECK(!pfs_dirtbl[old].use);
    CHECK(pfs_cwd_inherit(&child,&parent) == E_OK);
    pfs_proc_exit(&parent); pfs_proc_exit(&child); pfs_proc_exit(&grandchild);
    CHECK(acquired == released);
    __builtin_printf("Production PFS child CWD: isolation, shared ownership, full-table rollback and cookie reclamation: PASS");
    return 0;
}
