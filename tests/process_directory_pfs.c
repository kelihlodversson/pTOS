/* Production PFS resolver/table; driver cookies have real counted ownership. */
#include <ctype.h>
#include <stdio.h>
#include "emutos.h"
#include "../fs/pfs.c"
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"pfs:%d: %s\n",__LINE__,#c); __builtin_abort(); } } while (0)
static PD parent, child, grandchild;
PD *run = &parent;
#if CONF_WITH_NATIVE_SCHED
static FS_ERROR_STATE test_disk_error;
FS_ERROR_STATE *fs_error_current(void) { return &test_disk_error; }
#else
FS_ERROR_STATE fs_legacy_error;
#endif
LONG drvbits;
static UBYTE parent_dirs[NUMCURDIR], child_dirs[NUMCURDIR], grandchild_dirs[NUMCURDIR];
static unsigned char live[4096];
static BOOL release_reentry, removing;
static unsigned acquired, released;
static LONG lookup_error;
static unsigned fault_kind, root_calls;
static LONG fault_code;
FTAB sft[OPNFILES];
BOOL fs_sft_claim(int slot) { (void)slot; __builtin_abort(); }
static void disk_fault(void)
{
    errcode=fault_code; rwerr=0x4567; errdrv=2;
    longjmp(errbuf,1);
}
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
    root_calls++;
    if (fault_kind == 11 && root_calls == 2) disk_fault();
    out->fs=fs; out->index=++acquired; live[acquired]=1; out->aux=drive; out->pos=0;
    return E_OK;
}
static LONG lookup(PFSCOOKIE *dir, const char *path, PFSCOOKIE *out)
{
    CHECK(run == &parent && path && dir->index > 0);
    if (fault_kind == 1 || fault_kind == 13 || (fault_kind == 2 && !strcmp(path,"SECOND"))) disk_fault();
    if (removing) CHECK(strcmp(path,"EMPTY")); /* remove via parent/name, not target cookie */
    if (lookup_error) return lookup_error;
    *out=*dir; out->index=++acquired; live[acquired]=1;
    return E_OK;
}
static LONG remove_directory(PFSCOOKIE *dir, const char *name)
{
    CHECK(dir->fs == &fat_pfs_ops && live[dir->index]);
    if (!name[0]) return EACCDN; /* drive root */
    CHECK(!strcmp(name,"EMPTY"));
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
static LONG fault_directory(PFSCOOKIE *dir, const char *name)
{ CHECK(dir->fs == &fat_pfs_ops && name); disk_fault(); return EINTRN; }
static LONG fault_open(PFSCOOKIE *dir, const char *name, WORD mode, PFSCOOKIE *out)
{ (void)mode; (void)out; return fault_directory(dir,name); }
static LONG fault_create(PFSCOOKIE *dir, const char *name, UWORD attr, PFSCOOKIE *out)
{ (void)attr; (void)out; return fault_directory(dir,name); }
static LONG fault_chattr(PFSCOOKIE *dir, const char *name, BOOL set, UWORD *attr)
{ (void)set; (void)attr; return fault_directory(dir,name); }
static LONG fault_rename(PFSCOOKIE *dir, const char *name, PFSCOOKIE *other, const char *newname)
{ CHECK(other->fs == &fat_pfs_ops && newname); return fault_directory(dir,name); }
static LONG fault_readdir(PFSCOOKIE *dir, LONG *cursor, char *name, int cap, PFSATTR *attr)
{
    if (!fault_kind) {
        *cursor=1; strlcpy(name,"A.TXT",cap); memset(attr,0,sizeof(*attr));
        return E_OK;
    }
    return fault_directory(dir,name);
}
static void check_disk_cleanup(unsigned kind, LONG code)
{
    const unsigned baseline=acquired-released;
    UBYTE directories[NUMCURDIR];
    FS_ERROR_SCOPE enclosing;
    DTAINFO dta;
    fs_error_scope_save(enclosing);
    memcpy(directories,parent_dirs,sizeof(directories));
    fault_kind=kind; fault_code=code; root_calls=0;
    if (!setjmp(errbuf)) {
        switch(kind) {
        case 1: (void)pfs_do_chdir("C:\\FAULT"); break;
        case 2: case 11: (void)pfs_do_rename("C:\\FIRST\\A","C:\\SECOND\\B"); break;
        case 3: (void)pfs_do_open("C:\\A",0); break;
        case 4: (void)pfs_do_create("C:\\A",0); break;
        case 5: (void)pfs_do_mkdir("C:\\A"); break;
        case 6: (void)pfs_do_rmdir("C:\\A\\"); break;
        case 7: (void)pfs_do_unlink("C:\\A"); break;
        case 8: (void)pfs_do_chmod("C:\\A",1,0); break;
        case 9: (void)pfs_do_rename("C:\\FIRST\\A","C:\\SECOND\\B"); break;
        case 10: (void)pfs_do_sfirst_at("C:\\*.*",0,&dta); break;
        case 12: (void)pfs_do_open("A",0); break; /* borrowed PROCESS CWD */
        case 13: (void)pfs_do_chdir("FAULT"); break;
        case 14: (void)pfs_do_snext_at((DTAINFO *)pfs_searches[0].owner); break;
        default: CHECK(FALSE);
        }
        CHECK(FALSE); /* Disk exception must reach the original continuation. */
    }
    CHECK(errcode == code && rwerr == 0x4567 && errdrv == 2);
    CHECK(acquired-released == baseline-(kind==14 ? 1U:0U) &&
          !memcmp(directories,parent_dirs,sizeof(directories)));
    CHECK(!pfs_searches[0].owner && !pfs_searches[0].proc);
    fault_kind=0;
    fs_error_scope_restore(enclosing);
}
int main(void)
{
    WORD old, i;
    PFSCOOKIE cookie;
    char path[LEN_ZPATH];
    unsigned baseline;
    DTAINFO search_dta;
    fat_pfs_ops.root=test_root; fat_pfs_ops.lookup=lookup; fat_pfs_ops.release=release;
    fat_pfs_ops.rmdir=remove_directory;
    parent.p_curdrv=child.p_curdrv=2;
    removing=TRUE;
    CHECK(pfs_do_rmdir("C:\\EMPTY\\") == E_OK && acquired == released);
    CHECK(pfs_do_rmdir("C:\\EMPTY\\\\") == E_OK && acquired == released);
    CHECK(pfs_do_rmdir("C:\\") == EACCDN && acquired == released);
    removing=FALSE;
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
    /* Real filesystem longjmp, correct hosted jmp_buf ABI, and production
     * guards: unwind every temporary hold before the original outer handler. */
    fat_pfs_ops.open=fault_open; fat_pfs_ops.create=fault_create;
    fat_pfs_ops.mkdir=fault_directory; fat_pfs_ops.rmdir=fault_directory;
    fat_pfs_ops.remove=fault_directory; fat_pfs_ops.chattr=fault_chattr;
    fat_pfs_ops.rename=fault_rename; fat_pfs_ops.readdir=fault_readdir;
    for (i=1;i<=11;i++) check_disk_cleanup(i,i==11 ? E_CHNG : EWRITF);
    CHECK(pfs_do_chdir("C:\\RETAINED") == E_OK);
    old=parent_dirs[2]; baseline=acquired-released;
    check_disk_cleanup(12,EWRITF);
    check_disk_cleanup(13,EWRITF);
    CHECK(pfs_do_sfirst_at("C:\\*.*",0,&search_dta) == E_OK);
    CHECK(pfs_searches[0].owner == (DTA *)&search_dta);
    check_disk_cleanup(14,EWRITF);
    CHECK(parent_dirs[2] == old && pfs_dirtbl[old].use == 1 && acquired-released == baseline);
    pfs_proc_exit(&parent); CHECK(acquired == released);
    __builtin_printf("Production PFS child CWD: isolation, shared ownership, full-table rollback cookie reclamation and disk-error unwind: PASS\n");
    return 0;
}
