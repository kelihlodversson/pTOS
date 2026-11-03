/* Standard file-selector contracts through the production selector/adapter. */
#include "fsel_context.h"
#include "transport.h"
#include "gemerror.h"
#include "vdi_service.h"
#include "gemobjtree.h"
#include "optimize.h"
#include "string.h"
#undef CHECK
#define CHECK(c) do { if (!(c)) { __builtin_printf("selector failure: %d\n",__LINE__); return __LINE__; } } while (0)
extern void *malloc(__SIZE_TYPE__ bytes);
extern void free(void *memory);
short aes_global[15] = { 0x400,-1,7 };
static LONG display_depth, mouse_depth, keyboard_depth, overlays, dialogs, allocations;
static LONG next_error, first_error, graphics_error, input_error, draw_error;
static BOOL fail_alloc, fail_listing, fail_restore;
static int mode, step, searches, item, files, alerts, copies, saves, restores, opens, closes;
static void *current_dta = (void *)(uintptr_t)0x123400;
static char searched[128];
LONG aes_application_session(short *global) { return global && global[2] == 7 ? 9 : EACCDN; }
BOOL aes_layout_metrics(LAYOUT_METRICS *m, WORD *global)
{
    if (aes_application_session(global) < 0) return FALSE;
    m->width = 640; m->height = 480; m->wchar = 8; m->hchar = 16; m->hbox = 19;
    return TRUE;
}
BOOL aes_frames_dialog(BOOL begin) { dialogs += begin ? 1 : -1; return TRUE; }
BOOL aes_frames_suspend(void) { overlays++; return TRUE; }
void aes_frames_resume(void) { overlays--; }
void *aes_temporary_alloc(LONG bytes)
{
    void *memory;
    if (fail_alloc || (fail_listing && bytes < 100000)) return NULL;
    memory = malloc(bytes);
    if (memory) allocations++;
    return memory;
}
WORD aes_resource_discard(void *memory) { free(memory); allocations--; return 0; }
LONG aes_drive_map(void) { return 1|4|8; }
static WORD next_file(void)
{
    DTA *dta = current_dta;
    if (item == files) return ENMFIL;
    memset(dta,0,sizeof(*dta));
    if (files >= 12) {
        __builtin_snprintf(dta->d_fname,sizeof(dta->d_fname),"FILE%04d.TXT",item);
    } else if (strstr(searched,"BETA")) strcpy(dta->d_fname,"INSIDE.TXT");
    else {
        static const char *names[] = { "ZETA.TXT","BETA","ALPHA.TXT","OTHER.DAT" };
        strcpy(dta->d_fname,names[item]);
        if (item == 1) dta->d_attrib = FA_SUBDIR;
    }
    item++; return E_OK;
}
UQUAD aes_gemdos_call(ULONG function, UQUAD a, UQUAD b, UQUAD c)
{
    (void)b; (void)c;
    if (function == 0x19) return 2;
    if (function == 0x2f) return (uintptr_t)current_dta;
    if (function == 0x1a) { current_dta = (void *)(uintptr_t)a; return 0; }
    if (function == 0x4e) {
        searches++; strcpy(searched,(char *)(uintptr_t)a); item = 0;
        if (first_error) return (QUAD)first_error;
        if (mode == 3 || mode == 4) files = strstr(searched,"BETA") ? 1 : 4;
        return next_file();
    }
    if (function == 0x4f) return next_error ? (UQUAD)(QUAD)next_error : (UQUAD)(QUAD)next_file();
    return (QUAD)EINVFN;
}
LONG cuis_call6(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e, UQUAD f)
{
    (void)a; (void)c; (void)d; (void)e; (void)f;
    if (input_error && b) return input_error;
    if (fn == CUIS_DISPLAY_LOCK) { display_depth += b ? 1 : -1; return E_OK; }
    if (fn == CUIS_MOUSE_LOCK) { mouse_depth += b ? 1 : -1; return E_OK; }
    if (fn == CUIS_KEYBOARD_LOCK) { keyboard_depth += b ? 1 : -1; return E_OK; }
    return EINVFN;
}
LONG aes_graphics_call(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    (void)a; (void)c;
    if (graphics_error) return graphics_error;
    if (fn == VDI_SERVICE_OPEN) { ((VDI_SERVICE_WORKSTATION *)(uintptr_t)b)->handle = 1; opens++; return E_OK; }
    if (fn == VDI_SERVICE_CLOSE) { closes++; return E_OK; }
    if (fn == VDI_SERVICE_CLIP) return E_OK;
    if (fn == VDI_SERVICE_SCREEN_COPY) { copies++; return E_OK; }
    if (fn == VDI_SERVICE_SCREEN_BUFFER) {
        if (d) { restores++; return fail_restore ? EACCDN : E_OK; }
        saves++; return E_OK;
    }
    return EINVFN;
}
WORD mt_objc_draw(OBJECT *tree, WORD object, WORD depth, WORD x, WORD y, WORD w, WORD h, WORD *global)
{
    (void)tree; (void)object; (void)depth; (void)x; (void)y; (void)w; (void)h; (void)global;
    return draw_error ? 0 : 1;
}
WORD mt_objc_change(OBJECT *tree, WORD object, WORD reserved, WORD x, WORD y, WORD w, WORD h, WORD state, WORD redraw, WORD *global)
{
    (void)reserved; (void)x; (void)y; (void)w; (void)h; (void)redraw; (void)global;
    if (draw_error) return 0;
    tree[object].ob_state = state; return 1;
}
WORD mt_graf_mouse(WORD value, const MFORM *form, WORD *global) { (void)value; (void)form; (void)global; return 1; }
WORD mt_graf_mkstate(WORD *x, WORD *y, WORD *buttons, WORD *modifiers, WORD *global)
{ (void)buttons; (void)modifiers; (void)global; *x = *y = 0; return 1; }
WORD mt_form_dial(WORD kind, WORD x, WORD y, WORD w, WORD h, WORD dx, WORD dy, WORD dw, WORD dh, WORD *global)
{
    (void)x; (void)y; (void)w; (void)h; (void)dx; (void)dy; (void)dw; (void)dh; (void)global;
    dialogs += kind == FMD_START ? 1 : -1; return 1;
}
WORD mt_graf_slidebox(OBJECT *tree, WORD parent, WORD child, WORD vertical, WORD *global)
{ (void)tree; (void)parent; (void)child; (void)vertical; (void)global; return 1000; }
WORD mt_form_error(WORD error, WORD *global) { (void)error; (void)global; alerts++; return 1; }
WORD mt_form_alert(WORD button, const char *text, WORD *global) { (void)button; (void)text; (void)global; alerts++; return 1; }
WORD mt_form_do(OBJECT *tree, WORD start, WORD *global)
{
    WORD object = FSCANCEL;
    (void)start; (void)global;
    if (current_dta != (void *)(uintptr_t)0x123400) return 0;
    if (mode == 1) object = step == 0 ? F2NAME : FSOK;
    if (mode == 2) object = (WORD)(0x8000|F2NAME);
    if (mode == 3) object = step == 0 ? F1NAME : (WORD)(0x8000|F1NAME);
    if (mode == 4 && !step) object = FCLSBOX;
    if (mode == 5 && !step) object = FS1STDRV+3;
    if (mode == 6) object = step == 0 ? FDNAROW : step == 1 ? FUPAROW : step == 2 ? FSVELEV : FSCANCEL;
    if (mode <= 2 && !step && files == 4 && !first_error) {
        char name[14];
        unfmt_str(tree[F1NAME].ob_spec.tedinfo->te_ptext+1,name);
        if (strcmp(name,"BETA")) return 0;
        unfmt_str(tree[F2NAME].ob_spec.tedinfo->te_ptext+1,name);
        if (strcmp(name,"ALPHA.TXT")) return 0;
        if (tree[F4NAME].ob_spec.tedinfo->te_ptext[1]) return 0;
    }
    step++;
    if (object == FSOK || object == FSCANCEL) tree[object].ob_state |= SELECTED;
    return object;
}
static int call_selector(BOOL success, WORD expected, const char *initial, const char *name)
{
    char path[128], selection[13];
    WORD button = 99;
    strcpy(path,initial); strcpy(selection,"OLD.TXT");
    step = searches = 0; files = mode == 6 ? 12 : mode == 7 ? 1700 : 4;
    CHECK(mt_fsel_exinput(path,selection,&button,"Select a file",aes_global) == success);
    CHECK(current_dta == (void *)(uintptr_t)0x123400 && !allocations && !dialogs && !overlays);
    CHECK(display_depth == 2 && mouse_depth == 3 && keyboard_depth == 1 && opens == closes);
    if (success) {
        if (button != expected || strcmp(selection,name))
            __builtin_printf("mode %d: button %d/%d selection %s/%s\n",mode,button,expected,selection,name);
        CHECK(button == expected && !strcmp(selection,name));
        if (mode == 3) CHECK(!strcmp(path,"C:\\BETA\\*.TXT"));
        if (mode == 4) CHECK(!strcmp(path,"C:\\*.TXT"));
        if (mode == 5) CHECK(!strcmp(path,"D:\\*.TXT"));
        if (mode == 7) CHECK(!alerts);
    } else CHECK(button == 99 && !strcmp(path,initial) && !strcmp(selection,"OLD.TXT"));
    return 0;
}
int main(void)
{
    char path[128] = "C:\\*.TXT", selection[13] = "";
    WORD button = 99;
    int error;
    display_depth = 2; mouse_depth = 3; keyboard_depth = 1;
    CHECK(!fsel_input(NULL,selection,&button) && !fsel_input(path,NULL,&button));
    CHECK(!fsel_input(path,selection,NULL) && !mt_fsel_input(path,selection,&button,NULL));
    for (mode = 0; mode <= 7; mode++) {
        error = call_selector(TRUE,mode >= 1 && mode <= 3,mode == 4 ? "C:\\BETA\\*.TXT" : "C:\\*.TXT",
                              mode == 3 ? "INSIDE.TXT" : mode == 1 || mode == 2 ? "ALPHA.TXT" : mode == 4 || mode == 5 ? "" : "OLD.TXT");
        CHECK(!error);
    }
    CHECK(copies >= 3 && saves == restores);
    mode = 0; fail_alloc = TRUE; CHECK(!call_selector(FALSE,0,path,"")); fail_alloc = FALSE;
    fail_listing = TRUE; CHECK(!call_selector(FALSE,0,path,"")); fail_listing = FALSE;
    fail_restore = TRUE; CHECK(!call_selector(FALSE,0,path,"")); fail_restore = FALSE;
    graphics_error = EACCDN; CHECK(!call_selector(FALSE,0,path,"")); graphics_error = 0;
    input_error = EACCDN; CHECK(!call_selector(FALSE,0,path,"")); input_error = 0;
    draw_error = EACCDN; CHECK(!call_selector(FALSE,0,path,"")); draw_error = 0;
    first_error = EPTHNF; CHECK(!call_selector(TRUE,0,path,"OLD.TXT")); first_error = 0;
    return 0;
}
