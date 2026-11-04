/* Standard API behavior over production graphics/metric adapters. */
#include "gem.h"
#include "gemerror.h"
#include "vdi_service.h"
#include "graphics.h"
#include "cuis.h"
#include "mforms.h"
#undef CHECK
#define CHECK(c) do { if (!(c)) { __builtin_printf("graphics failure: %d\n",__LINE__); return __LINE__; } } while (0)
short aes_global[15];
static LONG session = 7, open_error, query_error, check_error, style_error;
static WORD next_handle = 10, closed_handle;
static unsigned opens, closes, queries, styles;
static WORD pixel_x = 1, pixel_y = 1, char_w = 8, char_h = 16;
static MFORM installed;
static ULONG hidden;
LONG aes_application_session(short *global) { return global ? session : -1; }
LONG aes_graphics_call(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    (void)c; (void)d;
    if (fn == VDI_SERVICE_OPEN) {
        VDI_SERVICE_WORKSTATION *ws = (void *)(uintptr_t)b;
        if (open_error) return open_error;
        __builtin_memset(ws,0,sizeof(*ws)); ws->handle = next_handle++; opens++;
        ws->intout[0] = 639; ws->intout[1] = 399; ws->intout[3] = pixel_x; ws->intout[4] = pixel_y;
        return E_OK;
    }
    if (fn == VDI_SERVICE_CHECK) return check_error;
    if (fn == VDI_SERVICE_CLOSE) { closes++; closed_handle = a; return E_OK; }
    if (fn == VDI_SERVICE_DRAW) {
        VDI_SERVICE_DRAW_RESULT *out = (void *)(uintptr_t)c;
        VDI_SERVICE_DRAW_REQUEST *in = (void *)(uintptr_t)b;
        queries++;
        if (query_error) return query_error;
        if (in->code != 38) return EINVFN;
        out->ptsout[2] = char_w; out->ptsout[3] = char_h; return E_OK;
    }
    return EINVFN;
}
LONG cuis_call6(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e, UQUAD f)
{
    (void)d; (void)e; (void)f;
    if (fn != CUIS_POINTER_STYLE || a != (UQUAD)session) return EINVAL;
    if (style_error) return style_error;
    installed = *(MFORM *)(uintptr_t)b; hidden = c; styles++; return E_OK;
}
int main(void)
{
    WORD w = 99,h = 99,bw = 99,bh = 99, handle, other[15] = {0};
    MFORM custom;
    unsigned before;
    CHECK(sizeof(MFORM) == 74 && __builtin_offsetof(MFORM,mf_fg) == 6 &&
        __builtin_offsetof(MFORM,mf_bg) == 8 && __builtin_offsetof(MFORM,mf_mask) == 10);
    CHECK(__builtin_types_compatible_p(__typeof__(((MFORM *)0)->mf_mask[0]),WORD));
    session = -1;
    CHECK(!graf_handle(&w,&h,&bw,&bh) && w == 99 && !graf_mouse(ARROW,NULL));
    session = 7; open_error = ENSMEM;
    CHECK(!graf_handle(&w,&h,&bw,&bh) && w == 99); open_error = 0;
    query_error = EINVAL;
    CHECK(!graf_handle(&w,&h,&bw,&bh) && opens == closes && w == 99); query_error = 0;
    handle = mt_graf_handle(&w,&h,&bw,&bh,other);
    CHECK(handle > 0 && w == 8 && h == 16 && bw == 19 && bh == 19);
    before = opens;
    CHECK(graf_handle(NULL,NULL,NULL,NULL) == handle && opens == before);
    check_error = EACCDN; w = 99;
    CHECK(!graf_handle(&w,NULL,NULL,NULL) && w == 99 && opens == before);
    check_error = 0;
    CHECK(graf_handle(NULL,NULL,NULL,NULL) == handle);
    CHECK(graf_mouse(HOURGLASS,NULL) == 1 && !__builtin_memcmp(&installed,mform_rs_data[2],sizeof(installed)));
    CHECK(graf_mouse(M_SAVE,NULL) == 1);
    CHECK(graf_mouse(POINT_HAND,NULL) == 1 && graf_mouse(M_PREVIOUS,NULL) == 1);
    CHECK(!__builtin_memcmp(&installed,mform_rs_data[2],sizeof(installed)));
    CHECK(graf_mouse(M_PREVIOUS,NULL) == 1 && !__builtin_memcmp(&installed,mform_rs_data[3],sizeof(installed)));
    CHECK(graf_mouse(M_RESTORE,NULL) == 1 && !__builtin_memcmp(&installed,mform_rs_data[2],sizeof(installed)));
    __builtin_memset(&custom,0,sizeof(custom)); custom.mf_fg = 7; custom.mf_mask[3] = 0x1234;
    CHECK(graf_mouse(USER_DEF,&custom) == 1 && installed.mf_mask[3] == 0x1234);
    custom.mf_mask[3] = (WORD)0xffff;
    CHECK(graf_mouse(M_SAVE,NULL) == 1 && graf_mouse(ARROW,NULL) == 1 && graf_mouse(M_RESTORE,NULL) == 1);
    CHECK(installed.mf_mask[3] == 0x1234 && !graf_mouse(USER_DEF,NULL));
    CHECK(graf_mouse(M_OFF,NULL) == 1 && hidden == 1 && graf_mouse(M_OFF,NULL) == 1 && hidden == 2);
    CHECK(graf_mouse(M_ON,NULL) == 1 && hidden == 1 && graf_mouse(M_ON,NULL) == 1 && !hidden);
    CHECK(graf_mouse(M_ON,NULL) == 1 && !hidden);
    style_error = EACCDN;
    CHECK(!graf_mouse(M_OFF,NULL)); style_error = 0;
    CHECK(graf_mouse(M_ON,NULL) == 1 && !hidden);
    before = styles;
    CHECK(graf_mouse(999,NULL) == 1 && styles == before);
    CHECK(graf_mouse(-1,NULL) == 1 && !__builtin_memcmp(&installed,mform_rs_data[0],sizeof(installed)));
    CHECK(graf_mouse(42,NULL) == 1 && !__builtin_memcmp(&installed,mform_rs_data[0],sizeof(installed)));
    check_error = EIHNDL;
    CHECK(graf_handle(NULL,NULL,NULL,NULL) != handle); check_error = 0;
    CHECK(graf_mouse(M_OFF,NULL) == 1 && graf_mouse(M_OFF,NULL) == 1 && hidden == 2);
    aes_graphics_wind_new();
    CHECK(graf_mouse(M_OFF,NULL) == 1 && hidden == 1);
    aes_graphics_reset(); CHECK(closed_handle > 0);
    pixel_y = 2;
    CHECK(graf_handle(&w,&h,&bw,&bh) > 0 && bw == 38);
    aes_graphics_reset(); pixel_y = 1; pixel_x = 3;
    CHECK(graf_handle(&w,&h,&bw,&bh) > 0 && bw == 12);
    aes_graphics_reset(); char_h = 32765; w = 99;
    CHECK(!graf_handle(&w,NULL,NULL,NULL) && w == 99);
    return 0;
}
