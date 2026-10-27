/* Public object-drawing contracts against a small independent pixel oracle. */
#include "gem.h"
#include "gemerror.h"
#include "mono_objects.h"
#include "vdi_service.h"
#include "../../lib/aes/transport.h"
#define VERIFY(c) do { if (!(c)) { __builtin_printf("object draw failure: %d\n",__LINE__); return __LINE__; } } while (0)
short aes_global[15];
static LONG session_error, graphics_error;
static int opens, closes, nesting, calls, texts;
static WORD image[120][160], last_text[256], last_count, last_text_x, last_text_y;
static struct { WORD mode, color, interior, clip[4], line; } workstation[128];
static PARMBLK callback_parameters;
static BOOL draw_error, window_update;
static int releases;
static BOOL invalidate_global;
static ULONG callbacks;
static OBJECT *mutation_target;
static int mutation_kind;
static char mutation_text[200];
LONG aes_application_session(short *global) { return global && !session_error ? 7 : EACCDN; }
LONG cuis_call6(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e, UQUAD f)
{
    (void)c; (void)d; (void)e; (void)f;
    if (fn != CUIS_DISPLAY_LOCK || a != 7) return EACCDN;
    if (!b) releases++;
    if (window_update) return b ? EAGAIN : EACCDN;
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
        __builtin_memset(ws,0,sizeof(*ws));
        ws->handle = ++opens; ws->intout[0] = 159; ws->intout[1] = 119;
        ws->ptsout[1] = 4;
        if (opens >= 128) return ENSMEM;
        return E_OK;
    }
    if (a <= 0 || a >= 128 || (!nesting && !window_update)) return EACCDN;
    if (fn == VDI_SERVICE_CLIP) {
        __builtin_memcpy(workstation[a].clip,(void *)(uintptr_t)c,8);
        return E_OK;
    }
    if (fn == VDI_SERVICE_COLOR) { workstation[a].line = b; return (LONG)b; }
    if (fn == VDI_SERVICE_LINE) {
        WORD *points = (void *)(uintptr_t)c;
        LONG i, x2, y2, dx, dy, sx, sy, error, twice;
        for (i = 0; i < (LONG)b*2; i++)
            if (points[i] < -10000 || points[i] > 10000) return EINVAL;
        for (i = 1; i < (LONG)b; i++) {
            x = points[(i-1)*2]; y = points[(i-1)*2+1];
            x2 = points[i*2]; y2 = points[i*2+1];
            dx = __builtin_abs(x2-x); dy = -__builtin_abs(y2-y);
            sx = x < x2 ? 1 : -1; sy = y < y2 ? 1 : -1; error = dx+dy;
            for (;;) {
                if (x >= 0 && x < 160 && y >= 0 && y < 120 &&
                    x >= workstation[a].clip[0] && x <= workstation[a].clip[2] &&
                    y >= workstation[a].clip[1] && y <= workstation[a].clip[3])
                    image[y][x] = workstation[a].line;
                if (x == x2 && y == y2) break;
                twice = error*2;
                if (twice >= dy) { error += dy; x += sx; }
                if (twice <= dx) { error += dx; y += sy; }
            }
        }
        return E_OK;
    }
    if (fn != VDI_SERVICE_DRAW) return EINVFN;
    if (draw_error) return EACCDN;
    if (r->code == 114 || r->code == 8) {
        int i;
        for (i = 0; i < r->nptsin*2; i++) if (r->ptsin[i] < -10000 || r->ptsin[i] > 10000) return EINVAL;
    }
    if (r->code == 8) {
        int i;
        for (i = r->nintin; i < VDI_SERVICE_MAX_TEXT; i++) if (r->intin[i]) return EINVAL;
    }
    calls++;
    if (r->code == 38 || r->code == 12) {
        out->ptsout[0] = out->ptsout[2] = 8;
        out->ptsout[1] = 13; out->ptsout[3] = 16;
    } else if (r->code == 32) workstation[a].mode = r->intin[0];
    else if (r->code == 25) workstation[a].color = r->intin[0];
    else if (r->code == 23) workstation[a].interior = r->intin[0];
    else if (r->code == 8) {
        texts++; last_count = r->nintin; last_text_x = r->ptsin[0]; last_text_y = r->ptsin[1];
        __builtin_memcpy(last_text,r->intin,2*last_count);
    } else if (r->code == 114) {
        for (y = r->ptsin[1]; y <= r->ptsin[3]; y++) for (x = r->ptsin[0]; x <= r->ptsin[2]; x++) {
            if (x < 0 || x >= 160 || y < 0 || y >= 120 || x < workstation[a].clip[0] || x > workstation[a].clip[2] ||
                y < workstation[a].clip[1] || y > workstation[a].clip[3]) continue;
            if (workstation[a].mode == MD_XOR) image[y][x] ^= 15;
            else image[y][x] = workstation[a].interior == FIS_HOLLOW ? WHITE : workstation[a].color;
        }
    }
    return E_OK;
}
static WORD callback(PARMBLK *parameters)
{
    callbacks++; callback_parameters = *parameters;
    if (mutation_target) {
        if (mutation_kind == 0) mutation_target->ob_spec.free_string = mutation_text;
        else if (mutation_kind == 1) mutation_target->ob_spec.tedinfo->te_ptext = mutation_text;
        else mutation_target->ob_spec.tedinfo->te_ptmplt = mutation_text;
    }
    if (invalidate_global) session_error = EACCDN;
    return 0;
}
int gem_probe_pixel(WORD handle, WORD x, WORD y)
{
    (void)handle;
    return image[y][x];
}
int main(void)
{
    OBJECT tree[4] = { { 0 } }, large = { 0 }, labels[3] = { { 0 } };
    WORD x,y;
    int before, axis, style;
    OBSPEC indirect;
    TEDINFO ted = { 0 };
    USERBLK user;
    TEDINFO changed = { 0 };
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
    tree[3].ob_type = G_TEXT; tree[3].ob_spec.tedinfo = NULL;
    VERIFY(objc_draw(tree,0,1,0,0,160,120) == 1);
    tree[3].ob_flags |= INDIRECT; tree[3].ob_spec.indirect = NULL;
    VERIFY(objc_draw(tree,0,1,0,0,160,120) == 1);
    VERIFY(objc_draw(tree,3,0,0,0,160,120) == 1);
    tree[3].ob_type = G_ICON; tree[3].ob_flags = HIDETREE;
    /* Restore the original clip's backdrop for selection round trips. */
    __builtin_memset(image,0,sizeof(image));
    VERIFY(objc_draw(tree,0,1,20,20,10,10) == 1);
    VERIFY(objc_change(tree,1,99,0,0,160,120,SELECTED,1) == 1 && tree[1].ob_state == SELECTED);
    VERIFY(image[20][20] == (RED ^ 15));
    VERIFY(objc_change(tree,1,0,0,0,160,120,0,1) == 1 && image[20][20] == RED);
    tree[2].ob_type = G_IBOX;
    before = image[32][32];
    VERIFY(objc_change(tree,2,0,0,0,160,120,SELECTED,1) == 1 && image[32][32] == (before ^ 15));
    VERIFY(objc_change(tree,2,0,0,0,160,120,CHECKED,1) == 1 && image[32][32] == before);
    tree[2].ob_state = 0;
    window_update = TRUE; before = releases;
    VERIFY(objc_draw(tree,1,0,0,0,160,120) == 1 && !nesting && releases == before);
    VERIFY(objc_change(tree,1,0,0,0,160,120,SELECTED,1) == 1 && !nesting && releases == before);
    VERIFY(objc_change(tree,1,0,0,0,160,120,0,1) == 1 && !nesting && releases == before);
    graphics_error = EINVFN;
    VERIFY(objc_draw(tree,1,0,0,0,160,120) == 0 && releases == before);
    graphics_error = 0; draw_error = TRUE;
    VERIFY(objc_change(tree,1,0,0,0,160,120,SELECTED,1) == 0 && !tree[1].ob_state && releases == before);
    draw_error = FALSE; window_update = FALSE;
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
    tree[1].ob_width = 80;
    tree[1].ob_flags = 0; tree[1].ob_type = G_FTEXT; tree[1].ob_spec.tedinfo = &ted;
    ted.te_ptext = raw; ted.te_ptmplt = pattern; ted.te_font = IBM; ted.te_color = 0x1100;
    VERIFY(objc_draw(tree,1,0,0,0,160,120) == 1 && last_count == 5);
    VERIFY(last_text[0] == 'A' && last_text[1] == 'B' && last_text[2] == '.' && last_text[3] == '_');
    ted.te_just = 1;
    VERIFY(objc_draw(tree,1,0,0,0,160,120) == 1 && last_text[0] == '_' && last_text[3] == 'A' && last_text[4] == 'B');
    __builtin_memset(big,'_',257); big[257] = 0; ted.te_ptmplt = big; before = opens;
    VERIFY(objc_draw(tree,1,0,0,0,160,120) == 0 && opens == before);
    tree[1].ob_width = 20;
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
    VERIFY(objc_change(tree,2,0,0,0,0,0,DISABLED,1) == 1 && tree[2].ob_state == DISABLED);
    VERIFY(objc_change(tree,2,0,200,200,10,10,CROSSED,1) == 1 && tree[2].ob_state == CROSSED);
    large.ob_next = large.ob_head = large.ob_tail = -1;
    large.ob_x = large.ob_y = 10; large.ob_width = 10000; large.ob_height = 20;
    large.ob_type = G_BOX; large.ob_spec.index = 0x1172;
    __builtin_memset(image,0,sizeof(image));
    VERIFY(objc_draw(&large,0,0,0,0,160,120) == 1);
    VERIFY(image[10][159] == RED && image[29][159] == RED && image[30][159] == WHITE);
    large.ob_type = G_IBOX; large.ob_spec.index = 0x11100;
    __builtin_memset(image,0,sizeof(image));
    VERIFY(objc_draw(&large,0,0,0,0,160,120) == 1);
    VERIFY(image[10][159] == BLACK && image[29][159] == BLACK && image[20][10] == BLACK);
    VERIFY(image[20][159] == WHITE && image[20][11] == WHITE);
    large.ob_width = 20; large.ob_height = 10000;
    __builtin_memset(image,0,sizeof(image));
    VERIFY(objc_draw(&large,0,0,0,0,160,120) == 1);
    VERIFY(image[119][10] == BLACK && image[119][29] == BLACK && image[10][20] == BLACK);
    VERIFY(image[119][20] == WHITE);
    large.ob_type = G_BOX; large.ob_spec.index = 0x1172;
    large.ob_width = 32767; large.ob_height = 20;
    __builtin_memset(image,0,sizeof(image));
    VERIFY(objc_draw(&large,0,0,0,0,160,120) == 1 && image[20][159] == RED);
    large.ob_width = 20; large.ob_height = 32767;
    VERIFY(objc_draw(&large,0,0,0,0,160,120) == 1 && image[119][20] == RED);
    for (axis = 0; axis < 2; axis++) for (style = 0; style < 5; style++) {
        large.ob_width = axis ? 20 : 32767; large.ob_height = axis ? 32767 : 20;
        large.ob_state = style == 2 ? OUTLINED : style == 3 ? CROSSED : style == 4 ? SHADOWED : 0;
        large.ob_spec.index = style == 0 ? 0x11172 : (style == 1 || style == 4) ? 0xff1172 : 0x1172;
        __builtin_memset(image,0,sizeof(image));
        VERIFY(objc_draw(&large,0,0,0,0,160,120) == 1 && image[15][15] == RED);
        if (style == 3) {
            VERIFY((axis ? image[100][10] : image[10][100]) == WHITE);
            VERIFY((axis ? image[100][29] : image[29][100]) == WHITE);
        } else if (style == 4) {
            VERIFY((axis ? image[100][31] : image[31][100]) == BLACK);
            VERIFY((axis ? image[100][33] : image[33][100]) == WHITE);
        } else if (style == 2) {
            VERIFY((axis ? image[100][7] : image[7][100]) == BLACK);
            VERIFY((axis ? image[100][8] : image[8][100]) == WHITE);
        } else {
            int edge = style == 0 ? 10 : 9;
            VERIFY((axis ? image[100][edge] : image[edge][100]) == BLACK);
        }
        VERIFY((axis ? image[119][15] : image[15][159]) == RED);
    }
    large.ob_state = 0;
    for (x = 0; x < 3; x++) labels[x].ob_next = labels[x].ob_head = labels[x].ob_tail = -1;
    labels[0].ob_type = G_IBOX; labels[0].ob_width = 160; labels[0].ob_height = 120;
    labels[1].ob_type = G_BUTTON; labels[1].ob_spec.free_string = "Wide";
    labels[1].ob_x = labels[1].ob_y = 10; labels[1].ob_width = 25000; labels[1].ob_height = 20;
    labels[2].ob_type = G_BOX; labels[2].ob_spec.index = 0x1174;
    labels[2].ob_x = 40; labels[2].ob_y = 60; labels[2].ob_width = labels[2].ob_height = 8;
    VERIFY(objc_add(labels,0,1) == 1 && objc_add(labels,0,2) == 1);
    before = texts;
    VERIFY(objc_draw(labels,0,1,0,0,160,120) == 1 && texts == before && image[60][40] == BLUE);
    labels[1].ob_width = 40; labels[1].ob_height = 25000;
    VERIFY(objc_draw(labels,0,1,0,0,160,120) == 1 && texts == before && image[60][40] == BLUE);
    large.ob_type = G_STRING; large.ob_spec.free_string = "ABCDE";
    large.ob_x = -10; large.ob_y = 20; large.ob_width = 80; large.ob_height = 16;
    VERIFY(objc_draw(&large,0,0,0,0,17,120) == 1);
    VERIFY(last_count == 3 && last_text[0] == 'B' && last_text[2] == 'D');
    VERIFY(last_text_x == -2 && last_text_y == 33);
    __builtin_memset(mutation_text,'X',sizeof(mutation_text)-1);
    labels[1].ob_type = G_USERDEF; labels[1].ob_spec.userblk = &user;
    labels[1].ob_width = labels[1].ob_height = 20; user.ub_code = callback;
    mutation_target = &labels[2];
    changed.te_font = IBM; changed.te_color = 0x1100;
    for (mutation_kind = 0; mutation_kind < 3; mutation_kind++) {
        changed.te_ptext = "ok"; changed.te_ptmplt = "__";
        labels[2].ob_type = mutation_kind == 0 ? G_STRING : mutation_kind == 1 ? G_TEXT : G_FTEXT;
        labels[2].ob_spec.tedinfo = &changed;
        if (!mutation_kind) labels[2].ob_spec.free_string = "ok";
        VERIFY(objc_draw(labels,0,1,0,0,160,120) == 0 && opens == closes && !nesting);
    }
    mutation_target = NULL;
    before = gem_mono_checks(0);
    if (before) __builtin_printf("mono object failure: %x\n",before);
    VERIFY(before == 0);
    VERIFY(opens == closes && !nesting && calls > 0 && texts >= 2);
    return 0;
}
