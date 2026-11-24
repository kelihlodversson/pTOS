/* Production FAT cache scavengers, with counted cache allocations. */
#include <ctype.h>
#include "emutos.h"
#include "../include/endian.h"
#include "../bdos/fsdir.c"
#define CHECK(c) do { if (!(c)) { __builtin_printf("cache:%d: %s\n",__LINE__,#c); __builtin_abort(); } } while (0)
DIRTBL_ENTRY dirtbl[NCURDIR];
static DND directory_root, held, pool[3];
static OFD root_file, held_file;
static unsigned allocated, released;
void *xmgetblk(WORD type)
{
    CHECK(type == MEMTYPE_DND && allocated < 3);
    return &pool[allocated++];
}
void xmfreblk(void *p)
{
    CHECK(p == &held || p == &held_file || p == &pool[0]);
    released++;
}
int main(void)
{
    FCB entry;
    DND *fresh, *reused;
    memset(&entry,0,sizeof(entry));
    directory_root.d_left=&held; directory_root.d_ofd=&root_file;
    held.d_parent=&directory_root; held.d_ofd=&held_file; held.d_cookie_refs=1;
    process_dnd_tree(&directory_root);
    CHECK(!released && directory_root.d_left == &held);
    fresh=makdnd(&directory_root,&entry);
    CHECK(fresh != &held && allocated == 1 && held.d_cookie_refs == 1 &&
          held.d_ofd == &held_file && directory_root.d_left == fresh);
    fresh->d_cookie_refs=1;
    held.d_cookie_refs=0;
    process_dnd_tree(&directory_root);
    CHECK(released == 2 && directory_root.d_left == fresh && !fresh->d_right);
    fresh->d_cookie_refs=0;
    reused=makdnd(&directory_root,&entry);
    CHECK(reused == fresh && allocated == 1 && !reused->d_cookie_refs);
    process_dnd_tree(&directory_root);
    CHECK(released == 3 && !directory_root.d_left);
    __builtin_printf("Production FAT directory-cookie cache hold/scavenge/reuse: PASS\n");
    return 0;
}
