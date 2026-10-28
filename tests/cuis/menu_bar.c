/* Bounded, transactional menu-bar indexing before ownership and drawing. */
#include "gemmenubar.h"
#define VERIFY(c) do { if (!(c)) return __LINE__; } while (0)
static void setup(OBJECT *tree)
{
    int i;
    static const OBJECT empty = { 0 };
    for (i = 0; i < 12; i++) {
        tree[i] = empty;
        tree[i].ob_next = tree[i].ob_head = tree[i].ob_tail = NIL;
        tree[i].ob_type = G_IBOX;
    }
    tree[0].ob_head = 1; tree[0].ob_tail = 7;
    tree[1].ob_head = tree[1].ob_tail = 2; tree[1].ob_next = 7;
    tree[2].ob_head = 5; tree[2].ob_tail = 3; tree[2].ob_next = 1;
    tree[5].ob_type = G_TITLE; tree[5].ob_next = 3; tree[5].ob_spec.free_string = "First";
    tree[3].ob_type = G_TITLE; tree[3].ob_next = 2; tree[3].ob_spec.free_string = "Second";
    tree[7].ob_head = 11; tree[7].ob_tail = 8; tree[7].ob_next = 0;
    tree[11].ob_next = 8; tree[8].ob_next = 7;
    tree[11].ob_head = tree[11].ob_tail = 4;
    tree[8].ob_head = tree[8].ob_tail = 6;
    tree[4].ob_type = tree[6].ob_type = G_STRING;
    tree[4].ob_spec.free_string = "Item 1"; tree[6].ob_spec.free_string = "Item 2";
    tree[4].ob_next = 11; tree[6].ob_next = 8;
}
static BOOL unchanged(const MENU_BAR_ENTRY *entries)
{
    return entries[0].title == 123 && entries[0].menu == 456 &&
        entries[1].title == 789 && entries[1].menu == 1011;
}
int main(void)
{
    OBJECT tree[12];
    MENU_BAR_ENTRY entries[2] = { { 123,456 }, { 789,1011 } };
    int mode;
    setup(tree);
    VERIFY(mn_bar_index(tree,12,NULL,0) == 2);
    VERIFY(mn_bar_index(tree,12,entries,1) == 0 && unchanged(entries));
    VERIFY(mn_bar_index(tree,12,entries,-1) == 0 && unchanged(entries));
    VERIFY(mn_bar_index(tree,12,entries,2) == 2);
    VERIFY(entries[0].title == 5 && entries[0].menu == 11 && entries[1].title == 3 && entries[1].menu == 8);
    /* Disabled and hidden titles keep their ordinal dropdown pairing. */
    tree[5].ob_state = DISABLED; tree[3].ob_flags = HIDETREE;
    VERIFY(mn_bar_index(tree,12,entries,2) == 2 && entries[1].menu == 8);
    for (mode = 0; mode < 12; mode++) {
        setup(tree);
        entries[0].title = 123; entries[0].menu = 456;
        entries[1].title = 789; entries[1].menu = 1011;
        switch (mode) {
        case 0: tree[0].ob_tail = -1; break;
        case 1: tree[7].ob_head = 12; break;
        case 2: tree[5].ob_next = 5; break;
        case 3: tree[11].ob_next = 11; break;
        case 4: tree[3].ob_next = 5; break;
        case 5: tree[8].ob_next = 11; break;
        case 6: tree[2].ob_tail = 5; break;
        case 7: tree[7].ob_tail = 11; break;
        case 8: tree[11].ob_next = 7; break;
        case 9: tree[5].ob_type = G_STRING; break;
        case 10: tree[8].ob_type = G_TITLE; break;
        default: tree[1].ob_tail = 3; break;
        }
        VERIFY(mn_bar_index(tree,12,entries,2) == 0 && unchanged(entries));
    }
    VERIFY(mn_bar_index(NULL,12,entries,2) == 0 && unchanged(entries));
    VERIFY(mn_bar_index(tree,2,entries,2) == 0 && unchanged(entries));
    {
        UBYTE seen[2] = { 123,234 };
        OBSPEC indirect;
        char long_text[129];
        int i;
        setup(tree);
        VERIFY(mn_bar_admit(tree,12,seen,1) == 0 && seen[0] == 123 && seen[1] == 234);
        VERIFY(mn_bar_admit(tree,12,NULL,2) == 0 && mn_bar_admit(NULL,12,seen,2) == 0);
        VERIFY(mn_bar_admit(tree,12,seen,2) == 2);
        tree[4].ob_state = DISABLED; tree[6].ob_flags = HIDETREE;
        tree[6].ob_spec.free_string = NULL;
        VERIFY(mn_bar_admit(tree,12,seen,2) == 2);
        indirect.free_string = "Indirect";
        tree[5].ob_flags = INDIRECT; tree[5].ob_spec.indirect = &indirect;
        VERIFY(mn_bar_admit(tree,12,seen,2) == 2);
        VERIFY(tree[5].ob_spec.indirect == &indirect && tree[4].ob_state == DISABLED);
        tree[5].ob_spec.indirect = NULL;
        VERIFY(mn_bar_admit(tree,12,seen,2) == 0);
        for (i = 0; i < 128; i++) long_text[i] = 'x';
        long_text[128] = 0;
        setup(tree); tree[4].ob_spec.free_string = long_text;
        VERIFY(mn_bar_admit(tree,12,seen,2) == 0);
        for (mode = 0; mode < 10; mode++) {
            setup(tree);
            switch (mode) {
            case 0: tree[8].ob_head = tree[8].ob_tail = 4; break;
            case 1: tree[4].ob_next = 4; break;
            case 2: tree[4].ob_next = 6; break;
            case 3: tree[4].ob_next = NIL; break;
            case 4: tree[11].ob_tail = NIL; break;
            case 5: tree[11].ob_head = 12; break;
            case 6: tree[4].ob_type = G_USERDEF; break;
            case 7: tree[4].ob_flags = SUBMENU; break;
            case 8: tree[4].ob_head = 9; break;
            default: tree[11].ob_head = 11; tree[11].ob_tail = NIL; break;
            }
            VERIFY(mn_bar_admit(tree,12,seen,2) == 0);
        }
        setup(tree); tree[11].ob_head = tree[11].ob_tail = NIL;
        VERIFY(mn_bar_admit(tree,12,seen,2) == 2);
    }
    return 0;
}
