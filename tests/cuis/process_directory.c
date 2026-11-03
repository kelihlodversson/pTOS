/* Real FAT directory setup: retain references and leave the caller untouched. */
#define BIOSBIND_H
#define Mediach(drive) ((void)(drive), 0L)
#define Drvmap() 0L
#include "../../fs/fatfs_pfs.c"
#define CHECK(c) do { if (!(c)) { __builtin_printf("directory:%d: %s\n",__LINE__,#c); __builtin_abort(); } } while (0)
static PD parent, child;
PD *run = &parent;
static UBYTE parent_dirs[NUMCURDIR], child_dirs[NUMCURDIR];
static unsigned refs[3] = { 0,2,0 };
static DND directory;
static LONG lookup_error;
static int new_slot = 2;
UBYTE *kproc_curdir(PD *process)
{
    CHECK(process == &parent || process == &child);
    return process == &parent ? parent_dirs : child_dirs;
}
DND *findit(char *name, const char **rest, int flag)
{
    CHECK(run == &parent && parent.p_curdrv == 2 && parent_dirs[2] == 1);
    CHECK(flag == 1 && *name);
    *rest = name;
    return lookup_error ? (DND *)(long)lookup_error : &directory;
}
int incr_curdir_usage(DND *entry)
{
    CHECK(entry == &directory);
    if (new_slot < 0) return new_slot;
    refs[new_slot]++; return new_slot;
}
void decr_curdir_usage(int index)
{
    CHECK(index > 0 && index < 3 && refs[index]); refs[index]--;
}
int main(void)
{
    parent.p_curdrv = child.p_curdrv = 2;
    parent_dirs[2] = child_dirs[2] = 1;
    CHECK(fat_set_process_directory("C:\\APPS",&child) == E_OK);
    CHECK(child_dirs[2] == 2 && refs[1] == 1 && refs[2] == 1);
    CHECK(child.p_curdrv == 2 && parent.p_curdrv == 2 && parent_dirs[2] == 1);
    lookup_error = EPTHNF;
    CHECK(fat_set_process_directory("D:\\MISSING",&child) == EPTHNF);
    CHECK(child.p_curdrv == 2 && !child_dirs[3] && refs[1] == 1 && refs[2] == 1);
    lookup_error = 0; new_slot = -1;
    CHECK(fat_set_process_directory("D:\\FULL",&child) == EPTHNF);
    CHECK(child.p_curdrv == 2 && !child_dirs[3]);
    new_slot = 2;
    CHECK(fat_set_process_directory("d:RELATIVE",&child) == E_OK && child.p_curdrv == 3);
    CHECK(child_dirs[3] == 2 && refs[2] == 2 && !parent_dirs[3] && parent.p_curdrv == 2);
    CHECK(fat_set_process_directory("!:INVALID",&child) == EDRIVE);
    CHECK(fat_set_process_directory("D:\\*",&child) == EPTHNF && refs[2] == 2);
    /* Ordinary Dsetpath still changes only that drive's path. */
    CHECK(fat_chdir_path("D:\\PATH") == E_OK && parent.p_curdrv == 2);
    CHECK(parent_dirs[2] == 1 && parent_dirs[3] == 2 && refs[2] == 3);
    __builtin_puts("FAT child directory isolation, drive selection and failed setup: PASS");
    return 0;
}
