/* Native x32 VDI contract checks, runnable on the host and in ring 3. */
#include "vdipb.h"
#include "vdi_service.h"
#include "gemerror.h"
#ifdef VDI_CONTRACT_HOST_TEST
#include <stdio.h>
#endif

int vdi_contract_checks(void);

static int raster_checks(void)
{
    VDI_SERVICE_RASTER_REQUEST r = { 0 };
    UWORD source[4] = { 0xc000, 0, 0, 0 };
    UWORD destination[4];
    ULONG bytes_in, bytes_out, expected;
    const UWORD transparent[4] = { 0xc555, 0xe555, 0x6555, 0xb555 };
    ULONG packed_source[2] = { 0x11223344UL, 0x55667788UL }, packed_destination[2] = { 0 };
    int mode, failures = 0;

#define CHECK(condition) do { if (!(condition)) failures++; } while (0)
    r.operation = 109;
    r.source.fd_addr = PTR_TO_USERPTR((void *)0x1000UL);
    r.destination.fd_addr = PTR_TO_USERPTR((void *)0x2000UL);
    r.source.fd_w = r.destination.fd_w = 16;
    r.source.fd_h = r.destination.fd_h = 1;
    r.source.fd_wdwidth = r.destination.fd_wdwidth = 1;
    r.source.fd_nplanes = r.destination.fd_nplanes = 1;
    r.source.fd_stand = r.destination.fd_stand = 1;
    r.xy[2] = r.xy[6] = 3;
    for (mode = 0; mode < 16; mode++) {
        r.mode = mode;
        destination[0] = 0xa555;
        CHECK(vdi_user_raster_shape(&r, &bytes_in, &bytes_out) == E_OK);
        CHECK(bytes_in == 2 && bytes_out == 2);
        vdi_user_raster_execute(&r, (const UBYTE *)source, (UBYTE *)destination);
        expected = ((mode & 1) ? 0x8000 : 0) | ((mode & 2) ? 0x4000 : 0) |
                   ((mode & 4) ? 0x2000 : 0) | ((mode & 8) ? 0x1000 : 0) | 0x0555;
        CHECK(destination[0] == expected);
    }
    r.mode = 16;
    CHECK(vdi_user_raster_shape(&r, &bytes_in, &bytes_out) == EINVAL);
    r.mode = 3;
    r.xy[0] = -1;
    CHECK(vdi_user_raster_shape(&r, &bytes_in, &bytes_out) == EINVAL);
    r.xy[0] = 0;
    r.xy[6] = 4;
    CHECK(vdi_user_raster_shape(&r, &bytes_in, &bytes_out) == EINVAL);
    r.xy[6] = 3;

    r.operation = 121;
    r.foreground = 1;
    for (mode = 1; mode <= 4; mode++) {
        r.mode = mode;
        r.background = mode == 4 ? 1 : 0;
        destination[0] = 0xa555;
        CHECK(vdi_user_raster_shape(&r, &bytes_in, &bytes_out) == E_OK);
        vdi_user_raster_execute(&r, (const UBYTE *)source, (UBYTE *)destination);
        CHECK(destination[0] == transparent[mode - 1]);
    }

    r.operation = 109;
    r.mode = 3;
    r.source.fd_nplanes = r.destination.fd_nplanes = 32;
    r.source.fd_stand = r.destination.fd_stand = 0;
    r.source.fd_w = r.destination.fd_w = 2;
    r.xy[2] = r.xy[6] = 1;
    CHECK(vdi_user_raster_shape(&r, &bytes_in, &bytes_out) == E_OK);
    CHECK(bytes_in == 8 && bytes_out == 8);
    vdi_user_raster_execute(&r, (const UBYTE *)packed_source, (UBYTE *)packed_destination);
    CHECK(packed_destination[0] == packed_source[0] && packed_destination[1] == packed_source[1]);

    r.operation = 110;
    r.source.fd_w = r.destination.fd_w = 16;
    r.source.fd_stand = r.destination.fd_stand = 1;
    r.source.fd_nplanes = r.destination.fd_nplanes = 2;
    r.source.fd_h = r.destination.fd_h = 2;
    source[0] = 0xaaaa; source[1] = 0xbbbb;
    source[2] = 0xcccc; source[3] = 0xdddd;
    CHECK(vdi_user_raster_shape(&r, &bytes_in, &bytes_out) == E_OK);
    CHECK(bytes_in == 8 && bytes_out == 8 && r.destination.fd_stand == 0);
    vdi_user_raster_execute(&r, (const UBYTE *)source, (UBYTE *)destination);
    CHECK(destination[0] == 0xaaaa && destination[1] == 0xcccc &&
          destination[2] == 0xbbbb && destination[3] == 0xdddd);
    r.source.fd_stand = 0;
    CHECK(vdi_user_raster_shape(&r, &bytes_in, &bytes_out) == E_OK);
    CHECK(r.destination.fd_stand == 1);
    vdi_user_raster_execute(&r, (const UBYTE *)destination, (UBYTE *)source);
    CHECK(source[0] == 0xaaaa && source[1] == 0xbbbb &&
          source[2] == 0xcccc && source[3] == 0xdddd);
#undef CHECK
    return failures;
}

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
    CHECK(raster_checks() == 0);
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
