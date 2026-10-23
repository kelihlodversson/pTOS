/* User-side file selector for the typed x32 desktop. All directory and form
 * state belongs to this process. GPL version 2 or later. */
#include "emutos.h"
#include "obdefs.h"
#include "aesdefs.h"
#include "aesbind.h"
#include "aesext.h"
#include "gsxdefs.h"
#include "bdosbind.h"
#include "string.h"
#include "intmath.h"
#include "optimize.h"
#include "deskaes.h"

#define ROWS 10
#define TITLE 1
#define DIRECTORY 2
#define FILENAME 3
#define UP 4
#define PREVIOUS 5
#define NEXT 6
#define ACCEPT 7
#define CANCEL 8
#define LIST 9
#define PATH_LABEL 10
#define NAME_LABEL 11
#define FIRST 12
#define OBJECTS (FIRST + ROWS)

WORD desk_file_select(char *path, char *selection, WORD *button, const char *title);

static void object(OBJECT *tree, WORD index, UWORD type, UWORD flags,
                    WORD x, WORD y, WORD w, WORD h, const char *text)
{
    tree[index].ob_type = type; tree[index].ob_flags = flags;
    tree[index].ob_spec.const_free_string = text;
    tree[index].ob_x = x * gl_wchar; tree[index].ob_y = y * gl_hchar;
    tree[index].ob_width = w * gl_wchar; tree[index].ob_height = h * gl_hchar;
    tree[index].ob_head = tree[index].ob_tail = -1;
    tree[index].ob_next = index == OBJECTS - 1 ? 0 : index + 1;
}

static char *separator(char *path)
{
    char *last = NULL;

    for (; *path; path++)
        if (*path == '\\')
            last = path;
    return last;
}

static WORD entries(const char *directory, const char *mask, LONG skip, WORD rows,
                     char names[ROWS][14], UBYTE *attributes, BOOL *more)
{
    DTA dta;
    void *saved = (void *)Fgetdta();
    char search[128];
    LONG rc, found = 0;
    WORD count = 0, pass;

    *more = FALSE;
    if (strlen(directory) + 4 >= sizeof(search))
        return 0;
    strcpy(search, directory); strcat(search, "*.*");
    Fsetdta(&dta);
    /* Directories precede matching files on every page. Do not keep an
     * unbounded directory listing in memory just to present one page. */
    for (pass = 0; pass < 2 && !*more; pass++) {
        rc = Fsfirst(search, FA_SUBDIR | FA_RO | FA_HIDDEN | FA_SYSTEM);
        while (!rc) {
            if (strcmp(dta.d_fname, ".") && strcmp(dta.d_fname, "..") &&
                ((pass == 0 && (dta.d_attrib & FA_SUBDIR)) ||
                 (pass == 1 && !(dta.d_attrib & FA_SUBDIR) && wildcmp(mask, dta.d_fname)))) {
                if (found++ >= skip) {
                    if (count == rows) {
                        *more = TRUE;
                        break;
                    }
                    strlcpy(names[count], dta.d_fname, sizeof(names[count]));
                    attributes[count++] = dta.d_attrib;
                }
            }
            rc = Fsnext();
        }
    }
    Fsetdta(saved);
    return count;
}

WORD desk_file_select(char *path, char *selection, WORD *button, const char *title)
{
    OBJECT tree[OBJECTS];
    TEDINFO text;
    char directory[128], mask[14], name[13], display[81], heading[81];
    char names[ROWS][14], labels[ROWS][18];
    UBYTE attributes[ROWS];
    char *p;
    WORD i, count, answer, x, y, w, h, done = FALSE;
    WORD columns, rows, pitch, list_height, name_y, buttons_y, nav_width;
    LONG skip = 0;
    BOOL more, double_click;

    *button = 0;
    if (strlen(path) >= sizeof(directory))
        return FALSE;
    strcpy(directory, path);
    p = separator(directory);
    if (!p)
        return FALSE;
    strlcpy(mask, p + 1, sizeof(mask));
    if (!mask[0])
        strcpy(mask, "*.*");
    p[1] = 0;
    strlcpy(name, selection, sizeof(name));
    columns = min(62, gl_width / gl_wchar - 4);
    pitch = gl_hchar + 2;
    rows = min(ROWS, (gl_height - 12 * gl_hchar - 12) / pitch);
    if (columns < 44 || rows < 1)
        return FALSE;
    list_height = rows * pitch + 4;
    name_y = 7 * gl_hchar + list_height;
    buttons_y = name_y + 2 * gl_hchar;
    nav_width = (columns - 8) / 3;
    memset(tree, 0, sizeof(tree));
    memset(&text, 0, sizeof(text));
    tree[0].ob_type = G_BOX; tree[0].ob_spec.index = 0x00021100L;
    tree[0].ob_next = -1; tree[0].ob_head = 1; tree[0].ob_tail = OBJECTS - 1;
    tree[0].ob_width = columns * gl_wchar;
    tree[0].ob_height = buttons_y + 2 * gl_hchar + 4;
    strlcpy(heading, title ? title : "Select file", min(sizeof(heading), (ULONG)columns - 3));
    object(tree, TITLE, G_STRING, 0, 2, 1, columns - 4, 1, heading);
    object(tree, PATH_LABEL, G_STRING, 0, 2, 3, 11, 1, "Directory:");
    object(tree, DIRECTORY, G_STRING, 0, 14, 3, columns - 16, 1, display);
    object(tree, NAME_LABEL, G_STRING, 0, 2, 0, 11, 1, "Filename:");
    tree[NAME_LABEL].ob_y = name_y;
    object(tree, FILENAME, G_FBOXTEXT, EDITABLE, 14, 0, 16, 1, NULL);
    tree[FILENAME].ob_y = name_y - 2;
    tree[FILENAME].ob_height += 4;
    text.te_ptext = name; text.te_ptmplt = "____________"; text.te_pvalid = "X";
    text.te_txtlen = sizeof(name); text.te_tmplen = 13;
    text.te_font = IBM; text.te_just = TE_LEFT; text.te_color = 0x1180;
    text.te_thickness = -1;
    tree[FILENAME].ob_spec.tedinfo = &text;
    object(tree, UP, G_BUTTON, SELECTABLE | TOUCHEXIT, 2, 4, nav_width, 1, "Parent");
    object(tree, PREVIOUS, G_BUTTON, SELECTABLE | TOUCHEXIT, nav_width + 4, 4, nav_width, 1, "Previous");
    object(tree, NEXT, G_BUTTON, SELECTABLE | TOUCHEXIT, 2 * nav_width + 6, 4, nav_width, 1, "Next");
    object(tree, ACCEPT, G_BUTTON, SELECTABLE | EXIT | DEFAULT, columns - 28, 0, 12, 1, "OK");
    object(tree, CANCEL, G_BUTTON, SELECTABLE | EXIT, columns - 14, 0, 12, 1, "Cancel");
    tree[ACCEPT].ob_y = tree[CANCEL].ob_y = buttons_y;
    for (i = UP; i <= CANCEL; i++) {
        tree[i].ob_height += 4;
        if (i <= NEXT)
            tree[i].ob_y += 4;
    }
    object(tree, LIST, G_BOX, 0, 2, 6, columns - 4, 1, NULL);
    tree[LIST].ob_spec.index = 0x00ff1100L;
    tree[LIST].ob_height = list_height;
    for (i = 0; i < ROWS; i++) {
        /* Borderless, left-aligned rows leave room around the font. */
        object(tree, FIRST + i, G_STRING, SELECTABLE | TOUCHEXIT, 3, 0, columns - 6, 1, labels[i]);
        tree[FIRST + i].ob_y = tree[LIST].ob_y + 2 + i * pitch;
        tree[FIRST + i].ob_height = pitch;
    }
    tree[OBJECTS - 1].ob_flags |= LASTOB;
    form_center(tree, &x, &y, &w, &h);
    form_dial(FMD_START, 0, 0, 0, 0, x, y, w, h);
    while (!done) {
        count = entries(directory, mask, skip, rows, names, attributes, &more);
        p = directory;
        if (strlen(p) > (ULONG)columns - 16) {
            strcpy(display, "...");
            p += strlen(p) - (columns - 19);
            strlcpy(display + 3, p, sizeof(display) - 3);
        } else
            strlcpy(display, p, sizeof(display));
        tree[UP].ob_state = strlen(directory) > 3 ? NORMAL : DISABLED;
        tree[PREVIOUS].ob_state = skip ? NORMAL : DISABLED;
        tree[NEXT].ob_state = more ? NORMAL : DISABLED;
        tree[ACCEPT].ob_state = tree[CANCEL].ob_state = NORMAL;
        for (i = 0; i < ROWS; i++) {
            tree[FIRST + i].ob_state = NORMAL;
            tree[FIRST + i].ob_flags &= ~HIDETREE;
            if (i < count)
                sprintf(labels[i], "%s%s", attributes[i] & FA_SUBDIR ? "\\ " : "  ", names[i]);
            else {
                labels[i][0] = 0;
                tree[FIRST + i].ob_flags |= HIDETREE;
            }
            if (i < count && !(attributes[i] & FA_SUBDIR) && !strcmp(name, names[i]))
                tree[FIRST + i].ob_state = SELECTED;
        }
        objc_draw(tree, 0, MAX_DEPTH, x, y, w, h);
#if CONF_WITH_X32_DESKTOP_TEST
        {
            char message[200];

            sprintf(message, "x32 desktop selector READY %d %d\r\n",
                    tree[0].ob_x + tree[CANCEL].ob_x + tree[CANCEL].ob_width / 2,
                    tree[0].ob_y + tree[CANCEL].ob_y + tree[CANCEL].ob_height / 2);
            desk_test_record(message);
            sprintf(message, "x32 desktop selector PATH %s%s\r\n", directory, mask);
            desk_test_record(message);
            sprintf(message, "x32 desktop selector NAME %s\r\n", name);
            desk_test_record(message);
            for (i = UP; i <= CANCEL; i++) {
                sprintf(message, "x32 desktop selector CONTROL %d %d %d\r\n", i,
                        tree[0].ob_x + tree[i].ob_x + tree[i].ob_width / 2,
                        tree[0].ob_y + tree[i].ob_y + tree[i].ob_height / 2);
                desk_test_record(message);
            }
            for (i = 0; i < count; i++) {
                sprintf(message, "x32 desktop selector ROW %s %d %d\r\n", names[i],
                        tree[0].ob_x + tree[FIRST + i].ob_x + tree[FIRST + i].ob_width / 2,
                        tree[0].ob_y + tree[FIRST + i].ob_y + tree[FIRST + i].ob_height / 2);
                desk_test_record(message);
            }
            desk_test_record("x32 desktop selector WAIT\r\n");
        }
#endif
        answer = form_do(tree, FILENAME);
        double_click = (answer & 0x8000) != 0;
        answer &= 0x7fff;
        if (answer >= FIRST && answer < FIRST + count) {
            i = answer - FIRST;
            if (attributes[i] & FA_SUBDIR) {
                if (strlen(directory) + strlen(names[i]) + strlen(mask) + 1 < sizeof(directory)) {
                    strcat(directory, names[i]); strcat(directory, "\\"); skip = 0;
                }
            } else {
                strlcpy(name, names[i], sizeof(name));
                if (double_click) {
                    *button = 1;
                    done = TRUE;
                }
            }
        } else if (answer == UP && strlen(directory) > 3) {
            directory[strlen(directory) - 1] = 0;
            p = separator(directory);
            if (p)
                p[1] = 0;
            skip = 0;
        } else if (answer == PREVIOUS)
            skip = skip > rows ? skip - rows : 0;
        else if (answer == NEXT && more)
            skip += rows;
        else if (answer == UP || answer == NEXT)
            ; /* Disabled TOUCHEXIT controls must not change the path/page. */
        else if (answer == ACCEPT || answer == CANCEL) {
            *button = answer == ACCEPT;
            done = TRUE;
        } else
            break;
        /* TOUCHEXIT returns on press. Consume its release before replacing
         * the snapshot, so the next form cannot activate another row. */
        if (!done) {
            WORD mx, my, buttons, modifiers;
            evnt_button(1, 1, 0, &mx, &my, &buttons, &modifiers);
        }
    }
#if CONF_WITH_X32_DESKTOP_TEST
    if (*button) {
        char message[200];

        sprintf(message, "x32 desktop selector ACCEPT %s%s\r\n", directory, name);
        desk_test_record(message);
    }
#endif
    form_dial(FMD_FINISH, 0, 0, 0, 0, x, y, w, h);
    strcpy(path, directory); strcat(path, mask);
    strlcpy(selection, name, 13);
    return TRUE;
}
