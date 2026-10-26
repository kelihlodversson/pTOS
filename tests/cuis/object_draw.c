/* Public object-drawing contracts against a small independent pixel oracle. */
#include "gem.h"
#include "gemerror.h"
#include "vdi_service.h"
#include "../../lib/aes/transport.h"
#define VERIFY(c) do { if (!(c)) { __builtin_printf("object draw failure: %d\n",__LINE__); return __LINE__; } } while (0)
short aes_global[15];
static LONG session_error, graphics_error;
static int opens, closes, nesting, calls, texts;
static WORD image[120][160], last_text[256], last_count;
static struct { WORD mode, color, interior, clip[4]; } workstation[32];
static PARMBLK callback_parameters;
static BOOL draw_error;
static BOOL invalidate_global;
static ULONG callbacks;
LONG aes_application_session(short *global) { return global && !session_error ? 7 : EACCDN; }
LONG cuis_call6(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e, UQUAD f)
{
    (void)c; (void)d; (void)e; (void)f;
    if (fn != CUIS_DISPLAY_LOCK || a != 7) return EACCDN;
    nesting += b ? 1 : -1;
    return E_OK;
}
LONG aes_graphics_call(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    VDI_SERVICE_DRAW_REQUEST *r = (void *)(uintptr_t)b;
    VDI_SERVICE_DRAW_RESULT *out = (void *)(uintptr_t)c;
    LONG x,y;
    (void)d;
    if (fn == VDI_SERVICE_CLOSE) { closes++; return E_OK; }
    if (graphics_error) return graphics_error;
    if (fn == VDI_SERVICE_OPEN) {
        VDI_SERVICE_WORKSTATION *ws = (void *)(uintptr_t)b;
        ws->handle = ++opens; ws->intout[0] = 159; ws->intout[1] = 119;
        if (opens >= 32) return ENSMEM;
        return E_OK;
    }
    if (a <= 0 || a >= 32 || !nesting) return EACCDN;
    if (fn == VDI_SERVICE_CLIP) {
        __builtin_memcpy(workstation[a].clip,(void *)(uintptr_t)c,8);
        return E_OK;
    }
    if (fn == VDI_SERVICE_LINE || fn == VDI_SERVICE_COLOR) return E_OK;
    if (fn != VDI_SERVICE_DRAW) return EINVFN;
    if (draw_error) return EACCDN;
    calls++;
    if (r->code == 38 || r->code == 12) {
        out->ptsout[0] = out->ptsout[2] = 8;
        out->ptsout[1] = 13; out->ptsout[3] = 16;
    } else if (r->code == 32) workstation[a].mode = r->intin[0];
    else if (r->code == 25) workstation[a].color = r->intin[0];
    else if (r->code == 23) workstation[a].interior = r->intin[0];
    else if (r->code == 8) {
        texts++; last_count = r->nintin;
        __builtin_memcpy(last_text,r->intin,2*last_count);
    } else if (r->code == 114) {
        for (y = r->ptsin[1]; y <= r->ptsin[3]; y++) for (x = r->ptsin[0]; x <= r->ptsin[2]; x++) {
            if (x < 0 || x >= 160 || y < 0 || y >= 120 || x < workstation[a].clip[0] || x > workstation[a].clip[2] ||
                y < workstation[a].clip[1] || y > workstation[a].clip[3]) return EINVAL;
            if (workstation[a].mode == MD_XOR) image[y][x] ^= 15;
            else image[y][x] = workstation[a].interior == FIS_HOLLOW ? WHITE : workstation[a].color;
        }
    }
    return E_OK;
}
static WORD callback(PARMBLK *parameters)
{
    callbacks++; callback_parameters = *parameters;
    if (invalidate_global) session_error = EACCDN;
    return 0;
}
int main(void)
{
    OBJECT tree[4] = { { 0 } };
    WORD x,y;
    int before;
    OBSPEC indirect;
    TEDINFO ted = { 0 };
    USERBLK user;
    char raw[] = "AB", pattern[] = "__.__", big[258];
    tree[0].ob_next = -1; tree[0].ob_head = 1; tree[0].ob_tail = 3;
    tree[0].ob_type = G_BOX; tree[0].ob_spec.index = 0x1100;
    tree[0].ob_x = 10; tree[0].ob_y = 12; tree[0].ob_width = 100; tree[0].ob_height = 80;
    for (x = 1; x < 4; x++) {
        tree[x].ob_head = tree[x].ob_tail = -1; tree[x].ob_next = x == 3 ? 0 : x+1;
        tree[x].ob_type = G_BOX; tree[x].ob_spec.index = 0x1172;
        tree[x].ob_x = 5; tree[x].ob_y = 6; tree[x].ob_width = tree[x].ob_height = 20;
    }
    tree[3].ob_type = G_ICON; tree[3].ob_flags = HIDETREE;
    VERIFY(objc_draw(tree,0,1,20,20,10,10) == 1 && opens == closes && !nesting);
    for (y = 0; y < 120; y++) for (x = 0; x < 160; x++)
        VERIFY(image[y][x] == (x >= 20 && x < 30 && y >= 20 && y < 30 ? RED : WHITE));
    VERIFY(objc_change(tree,1,99,0,0,160,120,SELECTED,1) == 1 && tree[1].ob_state == SELECTED);
    VERIFY(image[20][20] == (RED ^ 15));
    VERIFY(objc_change(tree,1,0,0,0,160,120,0,1) == 1 && image[20][20] == RED);
    before = opens;
    VERIFY(mt_objc_change(tree,1,0,0,0,160,120,DISABLED,0,NULL) == 1 && opens == before);
    tree[1].ob_state = 0;
    session_error = EACCDN;
    VERIFY(objc_change(tree,1,0,0,0,160,120,SELECTED,1) == 0 && !tree[1].ob_state);
    session_error = 0; graphics_error = EINVFN;
    VERIFY(objc_draw(tree,0,1,0,0,160,120) == 0 && !nesting); graphics_error = 0;
    before = opens;
    VERIFY(mt_objc_change(tree,3,0,0,0,160,120,SELECTED,1,NULL) == 1 && opens == before);
    tree[3].ob_state = 0;
    draw_error = TRUE; before = opens;
    VERIFY(objc_draw(tree,0,1,0,0,160,120) == 0 && opens == before+1 && opens == closes && !nesting);
    draw_error = FALSE;
    tree[3].ob_flags = 0; before = opens;
    VERIFY(objc_draw(tree,0,1,0,0,160,120) == 0 && opens == before);
    tree[3].ob_flags = HIDETREE;
    VERIFY(objc_draw(tree,0,0,0,0,160,120) == 1 && image[20][20] == WHITE);
    indirect.index = 0x1174; tree[1].ob_flags = INDIRECT; tree[1].ob_spec.indirect = &indirect;
    VERIFY(mt_objc_draw(tree,1,0,0,0,160,120,aes_global) == 1 && image[20][20] == BLUE);
    tree[1].ob_flags = 0; tree[1].ob_type = G_FTEXT; tree[1].ob_spec.tedinfo = &ted;
    ted.te_ptext = raw; ted.te_ptmplt = pattern; ted.te_font = IBM; ted.te_color = 0x1100;
    VERIFY(objc_draw(tree,1,0,0,0,160,120) == 1 && last_count == 5);
    VERIFY(last_text[0] == 'A' && last_text[1] == 'B' && last_text[2] == '.' && last_text[3] == '_');
    ted.te_just = 1;
    VERIFY(objc_draw(tree,1,0,0,0,160,120) == 1 && last_text[0] == '_' && last_text[3] == 'A' && last_text[4] == 'B');
    __builtin_memset(big,'_',257); big[257] = 0; ted.te_ptmplt = big; before = opens;
    VERIFY(objc_draw(tree,1,0,0,0,160,120) == 0 && opens == before);
    tree[1].ob_type = G_USERDEF; tree[1].ob_spec.userblk = &user;
    user.ub_code = callback; user.ub_parm = 123;
    VERIFY(objc_draw(tree,1,0,20,20,10,10) == 1 && callback_parameters.pb_tree == tree);
    VERIFY(callback_parameters.pb_obj == 1 && callback_parameters.pb_x == 15 && callback_parameters.pb_y == 18 &&
           callback_parameters.pb_xc == 20 && callback_parameters.pb_wc == 10 && callback_parameters.pb_parm == 123);
    invalidate_global = TRUE;
    VERIFY(mt_objc_change(tree,1,0,20,20,10,10,DISABLED,1,aes_global) == 1 && !nesting);
    session_error = 0; invalidate_global = FALSE;
    VERIFY(callback_parameters.pb_prevstate == 0 && callback_parameters.pb_currstate == DISABLED);
    before = callbacks;
    VERIFY(objc_draw(tree,1,0,100,100,10,10) == 1 && callbacks == (ULONG)before);
    before = opens;
    VERIFY(objc_draw(tree,0,-1,0,0,160,120) == 0 && objc_draw(tree,0,MAX_DEPTH+1,0,0,160,120) == 0);
    VERIFY(objc_draw(tree,0,1,0,0,0,120) == 1 && opens == before);
    VERIFY(opens == closes && !nesting && calls > 0 && texts >= 2);
    return 0;
}
