/* Persistent shell context through standard GEM headers in separate clients. */
#include <stddef.h>
#include <gem.h>
#include "gem_buffer.h"
#define VERIFY(c) do { if (!(c)) return 0x8800 | __LINE__; } while (0)
static char bytes[6001], expected[6000];
int gem_buffer_after_child(short *global)
{
    int i;
    VERIFY(mt_shel_get(NULL,SHEL_BUFSIZE,global) == 6000);
    VERIFY(mt_shel_get(bytes,6000,global) == 1);
    for (i = 0; i < 4; i++) VERIFY(bytes[i] == "CHLD"[i]);
    for (i = 4; i < 6000; i++) VERIFY(bytes[i] == expected[i]);
    return 0;
}
int gem_buffer_checks(short *global, int child)
{
    short a, b, c, d;
    int i;
    for (i = 0; i < 6000; i++) expected[i] = (char)(i*13+5);
    VERIFY(appl_getinfo(AES_INQUIRE,&a,&b,&c,&d) == 1 && a == 1 && b == 1);
    VERIFY(shel_put(NULL,0) == 1 && shel_get(NULL,0) == 1);
    if (!child) {
        VERIFY(shel_get(NULL,SHEL_BUFSIZE) == 4192);
        VERIFY(mt_shel_put(expected,6000,global) == 1 && shel_get(NULL,SHEL_BUFSIZE) == 6000);
        VERIFY(wind_update(BEG_UPDATE) == 1 && mt_wind_update(BEG_UPDATE,global) == 1);
        VERIFY(shel_put("INIT",4) == 1 && mt_shel_get(bytes,6000,global) == 1);
        VERIFY(wind_update(END_UPDATE) == 1 && wind_update(END_UPDATE) == 1 && !wind_update(END_UPDATE));
    } else {
        VERIFY(shel_get(NULL,SHEL_BUFSIZE) == 6000 && shel_get(bytes,6000) == 1);
        for (i = 4; i < 6000; i++) VERIFY(bytes[i] == expected[i]);
        VERIFY(mt_shel_put("CHLD",4,global) == 1);
    }
    bytes[0] = 99;
    VERIFY(!shel_get(bytes,-2) && bytes[0] == 99 && !shel_put("x",-1));
    VERIFY(!mt_shel_get(bytes,1,NULL) && bytes[0] == 99);
    return 0;
}
