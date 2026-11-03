/* Query the actual active hardware maps without exposing kernel pointers. */
#include "../../bios/ikbd.c"
static UBYTE maps[3][128];
int main(void)
{
    unsigned scan, table;
    if (keyboard_map_lookup(0,0) != EINVFN) return 1;
    current_keytbl.norm = maps[0]; current_keytbl.shft = maps[1]; current_keytbl.caps = maps[2];
    for (table = 0; table < 3; table++)
        for (scan = 0; scan < 128; scan++) {
            maps[table][scan] = (UBYTE)(scan+table*64);
            if (keyboard_map_lookup(scan,table) != maps[table][scan]) return 2;
        }
    if (keyboard_map_lookup(128,0) != EINVAL || keyboard_map_lookup(0,3) != EINVAL ||
        keyboard_map_lookup(1ULL << 32,0) != EINVAL || keyboard_map_lookup(0,1ULL << 32) != EINVAL ||
        keyboard_map_lookup(~0ULL,0) != EINVAL) return 3;
    maps[1][46] = 'C';
    if (keyboard_map_lookup(46,1) != keyboard_shifted(46)) return 4;
    maps[1][46] = 'Q'; /* A changed keyboard layout is observed immediately. */
    return keyboard_map_lookup(46,1) != 'Q';
}
