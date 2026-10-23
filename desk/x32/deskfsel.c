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
#define FIRST 9
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

static WORD entries(const char *directory, const char *mask, LONG skip,
                     char names[ROWS][14], UBYTE *attributes, BOOL *more)
{
    DTA dta;
    void *saved = (void *)Fgetdta();
    char search[128];
    LONG rc, found = 0;
    WORD count = 0;

    *more = FALSE;
    if (strlen(directory) + 4 >= sizeof(search))
        return 0;
    strcpy(search, directory); strcat(search, "*.*");
    Fsetdta(&dta);
    rc = Fsfirst(search, FA_SUBDIR | FA_RO | FA_HIDDEN | FA_SYSTEM);
    while (!rc) {
        if (strcmp(dta.d_fname, ".") && strcmp(dta.d_fname, "..") &&
            ((dta.d_attrib & FA_SUBDIR) || wildcmp(mask, dta.d_fname))) {
            if (found++ >= skip) {
                if (count == ROWS) {
                    *more = TRUE;
                    break;
                }
                strlcpy(names[count], dta.d_fname, sizeof(names[count]));
                attributes[count++] = dta.d_attrib;
            }
        }
        rc = Fsnext();
    }
    Fsetdta(saved);
    return count;
}

WORD desk_file_select(char *path, char *selection, WORD *button, const char *title)
{
    OBJECT tree[OBJECTS];
    TEDINFO text;
    char directory[128], mask[14], name[13], display[81];
    char names[ROWS][14], labels[ROWS][18];
    UBYTE attributes[ROWS];
    char *p;
    WORD i, count, answer, x, y, w, h, done = FALSE;
    LONG skip = 0;
    BOOL more;

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
    memset(tree, 0, sizeof(tree));
    memset(&text, 0, sizeof(text));
    tree[0].ob_type = G_BOX; tree[0].ob_spec.index = 0x00021100L;
    tree[0].ob_next = -1; tree[0].ob_head = 1; tree[0].ob_tail = OBJECTS - 1;
    tree[0].ob_width = min(62 * gl_wchar, gl_width - 16);
    tree[0].ob_height = 20 * gl_hchar;
    object(tree, TITLE, G_STRING, 0, 2, 1, 56, 1, title ? title : "Select file");
    object(tree, DIRECTORY, G_STRING, 0, 2, 3, 56, 1, display);
    object(tree, FILENAME, G_FBOXTEXT, EDITABLE, 18, 16, 16, 1, NULL);
    text.te_ptext = name; text.te_ptmplt = "____________"; text.te_pvalid = "X";
    text.te_txtlen = sizeof(name); text.te_tmplen = 13;
    text.te_font = IBM; text.te_just = TE_LEFT; text.te_color = 0x1180;
    text.te_thickness = -1;
    tree[FILENAME].ob_spec.tedinfo = &text;
    object(tree, UP, G_BUTTON, SELECTABLE | TOUCHEXIT, 2, 5, 12, 1, "Parent");
    object(tree, PREVIOUS, G_BUTTON, SELECTABLE | TOUCHEXIT, 16, 5, 12, 1, "Previous");
    object(tree, NEXT, G_BUTTON, SELECTABLE | TOUCHEXIT, 30, 5, 12, 1, "Next");
    object(tree, ACCEPT, G_BUTTON, SELECTABLE | EXIT | DEFAULT, 34, 18, 10, 1, "OK");
    object(tree, CANCEL, G_BUTTON, SELECTABLE | EXIT, 46, 18, 10, 1, "Cancel");
    for (i = 0; i < ROWS; i++)
        object(tree, FIRST + i, G_BUTTON, SELECTABLE | TOUCHEXIT, 3, 6 + i, 52, 1, labels[i]);
    tree[OBJECTS - 1].ob_flags |= LASTOB;
    form_center(tree, &x, &y, &w, &h);
    form_dial(FMD_START, 0, 0, 0, 0, x, y, w, h);
    while (!done) {
        count = entries(directory, mask, skip, names, attributes, &more);
        p = directory;
        if (strlen(p) > 76)
            p += strlen(p) - 76;
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
        }
        objc_draw(tree, 0, MAX_DEPTH, x, y, w, h);
#if CONF_WITH_X32_DESKTOP_TEST
        {
            char message[100];

            sprintf(message, "x32 desktop selector READY %d %d\r\n",
                    tree[0].ob_x + tree[CANCEL].ob_x + tree[CANCEL].ob_width / 2,
                    tree[0].ob_y + tree[CANCEL].ob_y + tree[CANCEL].ob_height / 2);
            desk_test_record(message);
        }
#endif
        answer = form_do(tree, FILENAME) & 0x7fff;
        if (answer >= FIRST && answer < FIRST + count) {
            i = answer - FIRST;
            if (attributes[i] & FA_SUBDIR) {
                if (strlen(directory) + strlen(names[i]) + strlen(mask) + 1 < sizeof(directory)) {
                    strcat(directory, names[i]); strcat(directory, "\\"); skip = 0;
                }
            } else
                strlcpy(name, names[i], sizeof(name));
        } else if (answer == UP) {
            directory[strlen(directory) - 1] = 0;
            p = separator(directory);
            if (p)
                p[1] = 0;
            skip = 0;
        } else if (answer == PREVIOUS)
            skip = skip > ROWS ? skip - ROWS : 0;
        else if (answer == NEXT)
            skip += ROWS;
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
    form_dial(FMD_FINISH, 0, 0, 0, 0, x, y, w, h);
    strcpy(path, directory); strcat(path, mask);
    strlcpy(selection, name, 13);
    return TRUE;
}
