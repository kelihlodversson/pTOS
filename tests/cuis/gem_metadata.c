/* Default desktop and clipboard state across isolated standard GEM clients. */
#include <stddef.h>
#include <gem.h>
#include "gem_metadata.h"
#define VERIFY(c) do { if (!(c)) return 0x8900 | __LINE__; } while (0)
static char command[13], directory[114], scrap[128];
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static void parent_state(short *global)
{
    mt_shel_wdef("DESKTOP.PRG","C:\\PARENT\\",global);
    mt_scrp_write("C:\\PARENT\\CLIP\\",global);
}
int gem_metadata_after_child(short *global)
{
    mt_shel_rdef(command,directory,global);
    VERIFY(equal(command,"CHILD.PRG") && equal(directory,"Z:\\MISSING"));
    VERIFY(mt_scrp_read(scrap,global) == 1 && equal(scrap,"relative\\scrap.txt"));
    parent_state(global);
    return 0;
}
int gem_metadata_checks(short *global, int child)
{
    short a, b, c, d;
    VERIFY(appl_getinfo(AES_PCGEM,&a,&b,&c,&d) == 1 && !a && !b && !c && d == 1);
    if (!child) {
        shel_rdef(command,directory);
        VERIFY(!*command && !*directory && scrp_read(scrap) == 1 && !*scrap);
        VERIFY(wind_update(BEG_UPDATE) == 1 && mt_wind_update(BEG_UPDATE,global) == 1);
        parent_state(global);
        VERIFY(wind_update(END_UPDATE) == 1 && wind_update(END_UPDATE) == 1 && !wind_update(END_UPDATE));
    }
    shel_rdef(command,directory);
    VERIFY(equal(command,"DESKTOP.PRG") && equal(directory,"C:\\PARENT\\"));
    VERIFY(scrp_read(scrap) == 1 && equal(scrap,"C:\\PARENT\\CLIP\\"));
    if (child) {
        shel_wdef("CHILD.PRG","Z:\\MISSING");
        VERIFY(scrp_write("relative\\scrap.txt") == 1);
    }
    command[0] = directory[0] = scrap[0] = 99;
    mt_shel_rdef(command,directory,NULL);
    VERIFY(command[0] == 99 && directory[0] == 99 && !mt_scrp_read(scrap,NULL) && scrap[0] == 99);
    return 0;
}
