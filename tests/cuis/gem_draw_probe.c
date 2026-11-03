#include "vdi_service.h"
#include "gem.h"
#include "gemerror.h"
#include "libaes.h"
#include "gem_draw_probe.h"
static LONG service(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    register UQUAD ax __asm__("rax") = ((UQUAD)VDI_SERVICE_CLASS << 32) | fn;
    register UQUAD fourth __asm__("r10") = d;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"(a), "S"(b), "d"(c), "r"(fourth)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
LONG gem_probe_open(WORD *handle, WORD *width, WORD *height)
{
    VDI_SERVICE_WORKSTATION ws;
    WORD input[11] = { 1,1,1,1,1,1,1,0,1,1,2 };
    LONG result = service(VDI_SERVICE_OPEN,(uintptr_t)input,(uintptr_t)&ws,0,0);
    if (!result) { *handle = ws.handle; *width = ws.intout[0]+1; *height = ws.intout[1]+1; }
    return result;
}
LONG gem_probe_close(WORD handle) { return service(VDI_SERVICE_CLOSE,handle,0,0,0); }
int gem_probe_pixel(WORD handle, WORD x, WORD y)
{
    WORD output[2];
    return service(VDI_SERVICE_PIXEL,handle,x,y,(uintptr_t)output) == 0 ? output[1] : -1;
}

/* Transitional native per-window update API, isolated from GEM headers. */
LONG gem_probe_update_begin(void)
{
    LONG window = aes_window_create(8,8,40,40), rc;
    if (window < 0) return window;
    rc = aes_window_show(window);
    if (rc == 0) rc = aes_update_begin(window);
    return rc < 0 ? rc : window;
}
LONG gem_probe_update_end(LONG window)
{
    LONG rc = aes_update_end(window);
    if (rc == 0) rc = aes_window_hide(window);
    if (rc == 0) rc = aes_window_destroy(window);
    return rc;
}

LONG gem_probe_cell(WORD handle, WORD *width, WORD *height)
{
    VDI_SERVICE_DRAW_REQUEST request = { 0 };
    VDI_SERVICE_DRAW_RESULT result = { 0 };
    LONG status;
    request.code = 38;
    status = service(VDI_SERVICE_DRAW,handle,(uintptr_t)&request,(uintptr_t)&result,0);
    if (!status) { *width = result.ptsout[2]; *height = result.ptsout[3]; }
    return status;
}

/* Minimal ordinary VDI bindings over the existing x32 transport. The test
 * application uses the standard GEMlib signatures/startup sequence. */
void v_opnvwk(WORD *input, WORD *handle, WORD *output)
{
    VDI_SERVICE_WORKSTATION ws;
    WORD i;
    if (service(VDI_SERVICE_CHECK,*handle,0,0,0) != 0 ||
        service(VDI_SERVICE_OPEN,(uintptr_t)input,(uintptr_t)&ws,0,0) != 0) { *handle = 0; return; }
    *handle = ws.handle;
    for (i = 0; i < 45; i++) output[i] = ws.intout[i];
    for (i = 0; i < 12; i++) output[45+i] = ws.ptsout[i];
}
void v_clsvwk(WORD handle) { (void)service(VDI_SERVICE_CLOSE,handle,0,0,0); }

/* Compare cursor pixels with the native pen mapping without changing the
 * underlying scene. This fixture is a 32-bit truecolor display. */
LONG gem_probe_swatch(WORD handle, WORD x, WORD y, WORD pen, WORD *pixel)
{
    ULONG buffer[16];
    GRECT r = { x,y,16,1 };
    OBJECT object = { 0 };
    BITBLK bits = { 0 };
    WORD data = -1;
    LONG result;
    if (!wind_update(BEG_UPDATE)) return EACCDN;
    result = service(VDI_SERVICE_SCREEN_BUFFER,handle,(uintptr_t)&r,(uintptr_t)buffer,0);
    if (!result) {
        object.ob_next = object.ob_head = object.ob_tail = -1; object.ob_type = G_IMAGE;
        object.ob_x = x; object.ob_y = y; object.ob_width = 16; object.ob_height = 1;
        object.ob_spec.bitblk = &bits; bits.bi_pdata = &data; bits.bi_wb = 2; bits.bi_hl = 1; bits.bi_color = pen;
        if (!objc_draw(&object,0,0,x,y,16,1)) result = EACCDN;
        else *pixel = gem_probe_pixel(handle,x,y);
        if (service(VDI_SERVICE_SCREEN_BUFFER,handle,(uintptr_t)&r,(uintptr_t)buffer,1)) result = EACCDN;
    }
    if (!wind_update(END_UPDATE)) result = EACCDN;
    return result;
}
