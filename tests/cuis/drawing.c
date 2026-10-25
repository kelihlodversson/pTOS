/* Real VDI service writes must stay inside the active CUIS visible region. */
#include "emutos.h"
#include "libaes.h"
#include "gemerror.h"
#include "ptos.h"
#if CONF_WITH_X32_VDI
#include "vdi_service.h"
static LONG service(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    register UQUAD ax __asm__("rax") = ((UQUAD)VDI_SERVICE_CLASS << 32) | fn;
    register UQUAD di __asm__("rdi") = a, si __asm__("rsi") = b;
    register UQUAD dx __asm__("rdx") = c, r10 __asm__("r10") = d;
    __asm__ volatile ("syscall" : "+a"(ax) : "r"(di), "r"(si), "r"(dx), "r"(r10)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
static LONG attribute(WORD handle, WORD code, WORD value)
{
    VDI_SERVICE_DRAW_REQUEST request = { 0 };
    VDI_SERVICE_DRAW_RESULT result;
    request.code = code;
    request.nintin = 1;
    request.intin[0] = value;
    return service(VDI_SERVICE_DRAW, handle, (uintptr_t)&request, (uintptr_t)&result, 0);
}
static LONG bar(WORD handle)
{
    VDI_SERVICE_DRAW_REQUEST request = { 0 };
    VDI_SERVICE_DRAW_RESULT result;
    request.code = 114;
    request.nptsin = 2;
    request.ptsin[2] = request.ptsin[3] = 63;
    return service(VDI_SERVICE_DRAW, handle, (uintptr_t)&request, (uintptr_t)&result, 0);
}
static LONG gdp_bar(WORD handle)
{
    VDI_SERVICE_DRAW_REQUEST request = { 0 };
    VDI_SERVICE_DRAW_RESULT result;
    request.code = 11;
    request.subcode = 1;
    request.nptsin = 2;
    request.ptsin[2] = request.ptsin[3] = 63;
    return service(VDI_SERVICE_DRAW, handle, (uintptr_t)&request, (uintptr_t)&result, 0);
}
static LONG pixel(WORD handle, WORD x, WORD y, WORD *output)
{
    return service(VDI_SERVICE_PIXEL, handle, x, y, (uintptr_t)output);
}
/* Capturing into application memory is allowed outside the active window.
 * Compare overlapping screen copies against that immutable source image. */
static int copy_checks(WORD handle)
{
    static WORD image[40 * 40];
    static const WORD cases[6][6] = {
        { 0, 0, 8, 8, 32, 32 }, { 8, 8, 9, 8, 31, 32 },
        { 9, 8, 8, 8, 31, 32 }, { 8, 8, 8, 9, 32, 31 },
        { 8, 9, 8, 8, 32, 31 }, { 8, 8, 9, 9, 31, 31 }
    };
    VDI_SERVICE_SCREEN_REQUEST copy = { 0 };
    WORD output[2], expected;
    int c, x, y;
#define CHECK(condition) do { if (!(condition)) return 0x5000 | __LINE__; } while (0)
    for (c = 0; c < 6; c++) {
        CHECK(bar(handle) == E_OK);
        for (y = 0; y < 40; y++) for (x = 0; x < 40; x++) {
            CHECK(pixel(handle, x, y, output) == E_OK);
            image[y * 40 + x] = output[1];
        }
        copy.mode = 3;
        copy.xy[0] = cases[c][0]; copy.xy[1] = cases[c][1];
        copy.xy[2] = cases[c][0] + cases[c][4] - 1;
        copy.xy[3] = cases[c][1] + cases[c][5] - 1;
        copy.xy[4] = cases[c][2]; copy.xy[5] = cases[c][3];
        copy.xy[6] = cases[c][2] + cases[c][4] - 1;
        copy.xy[7] = cases[c][3] + cases[c][5] - 1;
        CHECK(service(VDI_SERVICE_SCREEN_COPY, handle, (uintptr_t)&copy, 0, 0) == E_OK);
        for (y = 0; y < 40; y++) for (x = 0; x < 40; x++) {
            expected = image[y * 40 + x];
            if (x >= copy.xy[4] && x <= copy.xy[6] &&
                y >= copy.xy[5] && y <= copy.xy[7] &&
                x >= 8 && x < 48 && y >= 8 && y < 48 &&
                !(x >= 16 && x < 32 && y >= 16 && y < 32))
                expected = image[(y - copy.xy[5] + copy.xy[1]) * 40 + x - copy.xy[4] + copy.xy[0]];
            CHECK(pixel(handle, x, y, output) == E_OK && output[1] == expected);
        }
    }
#undef CHECK
    return 0;
}
#endif
int libaes_drawing_exit_handle(void);
int libaes_drawing_exit_handle(void)
{
#if CONF_WITH_X32_VDI
    VDI_SERVICE_WORKSTATION workstation;
    WORD input[11] = { 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 2 }, output[2];
    if (service(VDI_SERVICE_OPEN, (uintptr_t)input, (uintptr_t)&workstation, 0, 0) != E_OK)
        return 0x7001;
    if (pixel(workstation.handle, 0, 0, output) != E_OK) return 0x7002;
    /* Normal, _Exit and fault paths must reclaim this virtual handle without
     * closing the global display. This also opens during a foreign update. */
#endif
    return 0;
}
int libaes_drawing_checks(void);
int libaes_drawing_checks(void)
{
#if CONF_WITH_X32_VDI
    VDI_SERVICE_WORKSTATION workstation;
    WORD input[11] = { 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 2 };
    WORD output[2], black, handle;
    WORD line[4] = { 0, 50, 63, 50 }, clip[4] = { 10, 10, 12, 12 };
    LONG audit, target, cover;
    int rc;
    static const char pass[] = "x32 CUIS VDI enforcement: PASS\r\n";
#define CHECK(condition) do { if (!(condition)) return 0x6000 | __LINE__; } while (0)
    CHECK(service(VDI_SERVICE_OPEN, (uintptr_t)input, (uintptr_t)&workstation, 0, 0) == E_OK);
    handle = workstation.handle;
    CHECK(bar(handle) == EACCDN);
    CHECK(service(VDI_SERVICE_LINE, handle, 2, (uintptr_t)line, 0) == EACCDN);
    CHECK(pixel(handle, 0, 0, output) == E_OK);
    audit = aes_window_create(0, 0, 64, 64);
    CHECK(audit >= 0 && aes_window_show(audit) == E_OK);
    CHECK(aes_update_begin(audit) == E_OK);
    CHECK(attribute(handle, 23, 1) == E_OK);  /* solid */
    CHECK(attribute(handle, 32, 1) == E_OK);  /* replace */
    CHECK(attribute(handle, 25, 1) == E_OK);  /* black */
    CHECK(bar(handle) == E_OK);
    CHECK(pixel(handle, 4, 4, output) == E_OK);
    black = output[1];
    CHECK(aes_update_end(audit) == E_OK && aes_window_hide(audit) == E_OK);
    target = aes_window_create(8, 8, 40, 40);
    cover = aes_window_create(16, 16, 16, 16);
    CHECK(target >= 0 && cover >= 0);
    CHECK(aes_window_show(target) == E_OK && aes_window_show(cover) == E_OK);
    CHECK(aes_update_begin(target) == E_OK);
    CHECK(service(VDI_SERVICE_CLIP, handle, 0, (uintptr_t)clip, 0) == E_OK);
    CHECK(service(VDI_SERVICE_COLOR, handle, 0, 0, 0) == 0);
    CHECK(service(VDI_SERVICE_LINE, handle, 2, (uintptr_t)line, 0) == E_OK);
    CHECK(attribute(handle, 25, 0) == E_OK);  /* white */
    CHECK(gdp_bar(handle) == E_OK); /* default perimeter expands input points */
    CHECK(pixel(handle, 10, 10, output) == E_OK && output[1] != black);
    CHECK(pixel(handle, 40, 40, output) == E_OK && output[1] != black);
    CHECK(pixel(handle, 10, 20, output) == E_OK && output[1] != black);
    CHECK(pixel(handle, 35, 20, output) == E_OK && output[1] != black);
    CHECK(pixel(handle, 20, 20, output) == E_OK && output[1] == black);
    CHECK(pixel(handle, 4, 4, output) == E_OK && output[1] == black);
    CHECK(service(VDI_SERVICE_SCREEN_COPY, handle, 0, 0, 0) == EIMBA);
    rc = copy_checks(handle);
    if (rc) return rc;
    CHECK(service(VDI_SERVICE_CLIP, handle, 1, (uintptr_t)clip, 0) == E_OK);
    CHECK(attribute(handle, 25, 1) == E_OK);
    CHECK(bar(handle) == E_OK);
    CHECK(aes_update_end(target) == E_OK);
    CHECK(aes_window_show(audit) == E_OK && aes_update_begin(audit) == E_OK);
    CHECK(pixel(handle, 4, 4, output) == E_OK && output[1] == black);
    CHECK(pixel(handle, 20, 20, output) == E_OK && output[1] == black);
    CHECK(pixel(handle, 47, 47, output) == E_OK && output[1] != black);
    CHECK(pixel(handle, 48, 48, output) == E_OK && output[1] == black);
    CHECK(pixel(handle, 20, 50, output) == E_OK && output[1] == black);
    CHECK(pixel(handle, 11, 11, output) == E_OK && output[1] == black);
    CHECK(pixel(handle, 13, 13, output) == E_OK && output[1] != black);
    CHECK(aes_update_end(audit) == E_OK);
    CHECK(aes_window_hide(audit) == E_OK);
    CHECK(aes_window_configure(target, -100, -100, 10, 10) == E_OK);
    CHECK(aes_update_begin(target) == E_OK);
    CHECK(service(VDI_SERVICE_CLIP, handle, 0, (uintptr_t)clip, 0) == E_OK);
    CHECK(attribute(handle, 25, 0) == E_OK);
    CHECK(bar(handle) == E_OK); /* empty screen intersection must not draw */
    CHECK(aes_update_end(target) == E_OK);
    CHECK(aes_window_show(audit) == E_OK && aes_update_begin(audit) == E_OK);
    CHECK(pixel(handle, 4, 4, output) == E_OK && output[1] == black);
    CHECK(pixel(handle, 20, 20, output) == E_OK && output[1] == black);
    /* Last close and the next client open must not clear the display, even
     * while an update is held. Whole-screen screenshot reads remain usable. */
    CHECK(service(VDI_SERVICE_CLOSE, handle, 0, 0, 0) == E_OK);
    CHECK(service(VDI_SERVICE_OPEN, (uintptr_t)input, (uintptr_t)&workstation, 0, 0) == E_OK);
    handle = workstation.handle;
    CHECK(pixel(handle, 4, 4, output) == E_OK && output[1] == black);
    CHECK(pixel(handle, 47, 47, output) == E_OK && output[1] != black);
    CHECK(aes_update_end(audit) == E_OK);
    CHECK(aes_window_destroy(audit) == E_OK && aes_window_destroy(target) == E_OK);
    CHECK(aes_window_destroy(cover) == E_OK);
    CHECK(service(VDI_SERVICE_CLOSE, handle, 0, 0, 0) == E_OK);
    CHECK(ptos_write(1, pass, sizeof(pass) - 1) == sizeof(pass) - 1);
#undef CHECK
#endif
    return 0;
}
