/* Native x32 VDI contract checks, runnable on the host and in ring 3. */
#include "vdipb.h"
#ifdef VDI_CONTRACT_HOST_TEST
#include <stdio.h>
#endif

int vdi_contract_checks(void);

/* A real x32 compiler must accept the five distinct callback signatures. */
#if defined(__x86_64__) && defined(__ILP32__)
static void timer(int ms) { (void)ms; }
static void button(WORD status) { (void)status; }
static ULONG motion(WORD x, WORD y) { return MAKE_ULONG(x, y); }
static void cursor(WORD x, WORD y) { (void)x; (void)y; }
static void wheel(WORD number, WORD amount) { (void)number; (void)amount; }

static int callback_types(void)
{
    VDICONTROL control;

    control.args.timer.new_handler = timer;
    if (control.args.timer.new_handler != timer)
        return 1;
    control.args.button.new_handler = button;
    if (control.args.button.new_handler != button)
        return 1;
    control.args.motion.new_handler = motion;
    if (control.args.motion.new_handler != motion)
        return 1;
    control.args.cursor.new_handler = cursor;
    if (control.args.cursor.new_handler != cursor)
        return 1;
    control.args.wheel.new_handler = wheel;
    return control.args.wheel.new_handler != wheel;
}
#endif

int vdi_contract_checks(void)
{
    VDI_USER_FDB form = { 0 };
    ULONG bytes;
    int failures = 0, code;

#define CHECK(condition) do { if (!(condition)) failures++; } while (0)
    CHECK(sizeof(VDICONTROL) == 28 && sizeof(VDIPB) == 20);
    CHECK(sizeof(((VDIPB *)0)->ptsin) == 4);
    CHECK(vdi_user_control_kind(1, 0) == VDI_USER_ARRAYS);
    CHECK(vdi_user_control_kind(100, 0) == VDI_USER_ARRAYS);
    CHECK(vdi_user_control_kind(6, 0) == VDI_USER_ARRAYS);
    CHECK(vdi_user_control_kind(109, 0) == VDI_USER_RASTER);
    CHECK(vdi_user_control_kind(110, 0) == VDI_USER_RASTER);
    CHECK(vdi_user_control_kind(121, 0) == VDI_USER_RASTER);
    CHECK(vdi_user_control_kind(119, 0) == VDI_USER_FONT_LOAD);
    CHECK(vdi_user_control_kind(118, 0) == VDI_USER_TIMER);
    CHECK(vdi_user_control_kind(125, 0) == VDI_USER_BUTTON);
    CHECK(vdi_user_control_kind(126, 0) == VDI_USER_MOTION);
    CHECK(vdi_user_control_kind(127, 0) == VDI_USER_CURSOR);
    CHECK(vdi_user_control_kind(134, 0) == VDI_USER_WHEEL);
    for (code = 1; code <= 19; code++)
        CHECK(vdi_user_control_kind(5, code) == VDI_USER_ARRAYS);
    for (code = 1; code <= 10; code++)
        CHECK(vdi_user_control_kind(11, code) == VDI_USER_ARRAYS);
    CHECK(vdi_user_control_kind(5, 20) == VDI_USER_UNSUPPORTED);
    CHECK(vdi_user_control_kind(11, 13) == VDI_USER_UNSUPPORTED);
    CHECK(vdi_user_control_kind(6, 13) == VDI_USER_UNSUPPORTED);
    CHECK(vdi_user_control_kind(9, 13) == VDI_USER_UNSUPPORTED);
    CHECK(vdi_user_control_kind(-1, 0) == VDI_USER_UNSUPPORTED);
    CHECK(vdi_user_control_kind(32767, 0) == VDI_USER_UNSUPPORTED);
    CHECK(vdi_user_control_kind(109, 1) == VDI_USER_UNSUPPORTED);
    CHECK(vdi_user_control_kind(4, 0) == VDI_USER_UNSUPPORTED);

    /* A screen form has no caller-owned bitmap, regardless of dimensions. */
    CHECK(vdi_user_bitmap_bytes(&form, 0, &bytes) && bytes == 0);
    form.fd_addr = PTR_TO_USERPTR((void *)0x1000UL);
    form.fd_w = 17;
    form.fd_h = 10;
    form.fd_wdwidth = 2;
    form.fd_stand = 1;
    form.fd_nplanes = 4;
    CHECK(vdi_user_bitmap_bytes(&form, 0, &bytes) && bytes == 160);
    form.fd_wdwidth = 1;             /* 17 pixels do not fit a 16-bit row */
    CHECK(!vdi_user_bitmap_bytes(&form, 0, &bytes) && bytes == 0);
    form.fd_wdwidth = 2;
    form.fd_h = -1;
    CHECK(!vdi_user_bitmap_bytes(&form, 0, &bytes));
    form.fd_h = 10;
    form.fd_nplanes = 0;
    CHECK(!vdi_user_bitmap_bytes(&form, 0, &bytes));
    form.fd_nplanes = 33;
    CHECK(!vdi_user_bitmap_bytes(&form, 0, &bytes));
    form.fd_nplanes = 4;
    form.fd_r1 = 1;
    CHECK(!vdi_user_bitmap_bytes(&form, 0, &bytes));
    form.fd_r1 = 0;
    form.fd_stand = 2;
    CHECK(!vdi_user_bitmap_bytes(&form, 0, &bytes));

    /* Packed rows use pixels, not the planar word count. */
    form.fd_stand = 0;
    form.fd_nplanes = 16;
    CHECK(vdi_user_bitmap_bytes(&form, 2, &bytes) && bytes == 340);
    form.fd_nplanes = 32;
    CHECK(vdi_user_bitmap_bytes(&form, 4, &bytes) && bytes == 680);
    CHECK(!vdi_user_bitmap_bytes(&form, 2, &bytes));
    CHECK(!vdi_user_bitmap_bytes(&form, 3, &bytes));
    form.fd_stand = 1;
    CHECK(!vdi_user_bitmap_bytes(&form, 4, &bytes));

    /* Full-buffer arithmetic must never wrap to a short valid range. */
    form.fd_stand = 0;
    form.fd_addr = PTR_TO_USERPTR((void *)0xfffffff0UL);
    CHECK(!vdi_user_bitmap_bytes(&form, 4, &bytes) && bytes == 0);
    form.fd_addr = PTR_TO_USERPTR((void *)0x1000UL);
    form.fd_w = form.fd_h = form.fd_wdwidth = 32767;
    CHECK(!vdi_user_bitmap_bytes(&form, 0, &bytes) && bytes == 0);
#if defined(__x86_64__) && defined(__ILP32__)
    CHECK(callback_types() == 0);
#endif
#undef CHECK
    return failures;
}

#ifdef VDI_CONTRACT_HOST_TEST
int main(int argc, char **argv)
{
    int code, subcode;

    (void)argv;
    if (argc > 1) {
        for (code = 0; code <= 255; code++)
            printf("%d 0 %d\n", code, vdi_user_control_kind(code, 0));
        for (subcode = 1; subcode <= 100; subcode++) {
            printf("5 %d %d\n", subcode, vdi_user_control_kind(5, subcode));
            printf("11 %d %d\n", subcode, vdi_user_control_kind(11, subcode));
        }
    }
    return vdi_contract_checks();
}
#endif
