/* Real ring-3 VDI entry, boundary and ownership checks. */
#include "vdipb.h"
#include "vdi_service.h"
#include "gemerror.h"

int vdi_roundtrip_checks(WORD inherited_handle, BOOL fault_after_open);
int vdi_pixel_bounds_checks(void);

#if CONF_WITH_X32_VDI
static UWORD bitmap_source[8], bitmap_destination[8];
static const VDICONTROL immutable_control = { 0 };

static void callback_timer(int ms) { (void)ms; }
static void callback_button(WORD state) { (void)state; }
static ULONG callback_motion(WORD x, WORD y) { return MAKE_ULONG(x, y); }
static void callback_cursor(WORD x, WORD y) { (void)x; (void)y; }
static void callback_wheel(WORD n, WORD amount) { (void)n; (void)amount; }
#endif

static LONG raw_call(ULONG cls, ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    register UQUAD rax __asm__("rax") = ((UQUAD)cls << 32) | fn;
    register UQUAD rdi __asm__("rdi") = a;
    register UQUAD rsi __asm__("rsi") = b;
    register UQUAD rdx __asm__("rdx") = c;
    register UQUAD r10 __asm__("r10") = d;

    __asm__ volatile ("syscall" : "+r"(rax)
                      : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10)
                      : "rcx", "r11", "memory", "cc");
    return (LONG)rax;
}

static LONG raw_service(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    return raw_call(VDI_SERVICE_CLASS, fn, a, b, c, d);
}

int vdi_pixel_bounds_checks(void)
{
#if CONF_WITH_X32_VDI
    VDI_SERVICE_WORKSTATION workstation;
    WORD input[11] = { 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 2 };
    WORD output[2] = { 0x1234, 0x5678 };
    const UQUAD rejected[] = { 32768, 65535, 65536, 0x100000000ULL, ~(UQUAD)0 };
    UWORD handle;
    ULONG i;
    int failures = 0;

#define CHECK(condition) do { if (!(condition) && !failures) failures = __LINE__; } while (0)
    if (raw_service(VDI_SERVICE_OPEN, (uintptr_t)input, (uintptr_t)&workstation, 0, 0))
        return __LINE__;
    handle = workstation.handle;
    /* The boot fixture advertises 32769 rows. y=32767 fits the renderer's
     * signed WORD; a null output tests its acceptance without reading a
     * pixel beyond the real fixture's framebuffer. y=32768 must be
     * refused before it can wrap to a negative backend coordinate. */
    CHECK(raw_service(VDI_SERVICE_PIXEL, handle, 0, 32767, 0) == EIMBA);
    for (i = 0; i < ARRAY_SIZE(rejected); i++) {
        CHECK(raw_service(VDI_SERVICE_PIXEL, handle, 0, rejected[i], (uintptr_t)output) == EINVAL);
        CHECK(raw_service(VDI_SERVICE_PIXEL, handle, rejected[i], 0, (uintptr_t)output) == EINVAL);
        CHECK(output[0] == 0x1234 && output[1] == 0x5678);
    }
    CHECK(raw_service(VDI_SERVICE_PIXEL, handle, 0, 0, (uintptr_t)output) == E_OK);
    CHECK(raw_service(VDI_SERVICE_CLOSE, handle, 0, 0, 0) == E_OK);
#undef CHECK
    return failures;
#else
    return 1;                           /* fixture only runs with the service */
#endif
}

#if CONF_WITH_X32_VDI
static LONG call(VDIPB *pb, WORD code, WORD integers, WORD pairs)
{
    pb->contrl->code = code;
    pb->contrl->nintin = integers;
    pb->contrl->nptsin = pairs;
    return vdi(pb);
}

static LONG line(VDIPB *pb, WORD x1, WORD y1, WORD x2, WORD y2)
{
    pb->ptsin[0] = x1;
    pb->ptsin[1] = y1;
    pb->ptsin[2] = x2;
    pb->ptsin[3] = y2;
    return call(pb, 6, 0, 2);
}

static LONG pixel(VDIPB *pb, WORD x, WORD y)
{
    LONG rc;

    pb->ptsin[0] = x;
    pb->ptsin[1] = y;
    rc = call(pb, 105, 0, 1);
    return rc ? rc : (UWORD)pb->intout[1];
}

static int clipping_checks(WORD handle, WORD max_x, WORD max_y)
{
    VDICONTROL control = { 0 };
    WORD input[1], points[4], output[2];
    VDIPB pb = { &control, input, points, output, (WORD *)0 };
    LONG white, black;
    int phase, failures = 0;

#define CHECK(condition) do { if (!(condition) && !failures) failures = __LINE__; } while (0)
    control.handle = handle;
    input[0] = 0;
    CHECK(call(&pb, 17, 1, 0) == E_OK);
    CHECK(line(&pb, 0, 4, 1, 4) == E_OK);
    white = pixel(&pb, 0, 4);
    input[0] = 1;
    CHECK(call(&pb, 17, 1, 0) == E_OK);
    CHECK(line(&pb, 0, 4, 1, 4) == E_OK);
    black = pixel(&pb, 0, 4);
    CHECK(white >= 0 && black >= 0 && white != black);

    /* A fresh workstation, then explicitly disabled optional clipping.
     * Outside x coordinates must not wrap writes into adjacent scanlines.
     * Entirely outside and crossing lines exercise all four screen edges. */
    for (phase = 0; phase < 2; phase++) {
        if (phase) {
            input[0] = 0;
            CHECK(call(&pb, 129, 1, 2) == E_OK);
        }
        input[0] = 0;
        CHECK(call(&pb, 17, 1, 0) == E_OK);
        CHECK(line(&pb, 0, 0, max_x, 0) == E_OK);
        CHECK(line(&pb, 0, 1, max_x, 1) == E_OK);
        CHECK(line(&pb, 0, 3, max_x, 3) == E_OK);
        input[0] = 1;
        CHECK(call(&pb, 17, 1, 0) == E_OK);
        CHECK(line(&pb, -4, 1, -1, 1) == E_OK);
        CHECK(line(&pb, max_x + 1, 0, max_x + 4, 0) == E_OK);
        CHECK(line(&pb, 0, -1, 1, -1) == E_OK);
        CHECK(line(&pb, 0, max_y + 1, 1, max_y + 1) == E_OK);
        CHECK(pixel(&pb, max_x, 0) == white && pixel(&pb, 0, 1) == white);
        CHECK(line(&pb, -2, 2, max_x + 2, 2) == E_OK);
        CHECK(pixel(&pb, max_x, 1) == white && pixel(&pb, 0, 3) == white);
        CHECK(pixel(&pb, 0, 2) == black && pixel(&pb, max_x, 2) == black);
        CHECK(line(&pb, 2, -2, 2, max_y + 2) == E_OK);
        CHECK(pixel(&pb, 2, 0) == black && pixel(&pb, 2, max_y) == black);
    }

    /* Repeated calls preserve the caller's narrower clip rectangle. */
    input[0] = 0;
    CHECK(call(&pb, 17, 1, 0) == E_OK);
    CHECK(line(&pb, 0, 4, 8, 4) == E_OK);
    points[0] = 2; points[1] = 4; points[2] = 5; points[3] = 4;
    input[0] = 1;
    CHECK(call(&pb, 129, 1, 2) == E_OK);
    CHECK(call(&pb, 17, 1, 0) == E_OK);
    CHECK(line(&pb, 0, 4, 8, 4) == E_OK);
    CHECK(line(&pb, 0, 4, 8, 4) == E_OK);
    CHECK(pixel(&pb, 1, 4) == white && pixel(&pb, 6, 4) == white);
    CHECK(pixel(&pb, 3, 4) == black);
    points[0] = max_x + 1; points[1] = 0; points[2] = max_x + 4; points[3] = 4;
    CHECK(call(&pb, 129, 1, 2) == E_OK);
    CHECK(line(&pb, 0, 4, 8, 4) == E_OK); /* empty intersection is a no-op */
    CHECK(pixel(&pb, 1, 4) == white && pixel(&pb, 6, 4) == white);
    input[0] = 0;
    CHECK(call(&pb, 129, 1, 2) == E_OK);
#undef CHECK
    return failures;
}
#endif

int vdi_roundtrip_checks(WORD inherited_handle, BOOL fault_after_open)
{
#if CONF_WITH_X32_VDI
    VDICONTROL control = { 0 };
    WORD input[11] = { 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 2 };
    WORD points[12] = { 1, 1, 7, 1, 0, 0, 0, 0 };
    WORD integers[45], output_points[12];
    VDIPB pb = { &control, input, points, integers, output_points };
    VDIPB bad_block = pb;
    VDI_USER_FDB source = { 0 }, destination = { 0 };
    VDI_SERVICE_VECTOR_RESULT vector;
    WORD handle, saved;
    int failures = 0, i;

#define CHECK(condition) do { if (!(condition) && !failures) failures = __LINE__; } while (0)
    if (inherited_handle) {
        CHECK(raw_service(VDI_SERVICE_CHECK, inherited_handle, 0, 0, 0) == EIHNDL);
        CHECK(raw_service(VDI_SERVICE_CLOSE, inherited_handle, 0, 0, 0) == EIHNDL);
        CHECK(raw_service(VDI_SERVICE_LINE, inherited_handle, 2, (uintptr_t)points, 0) == EIHNDL);
    }
    CHECK(raw_call(2, 0x73, 0, 0, 0, 0) == -1); /* generic GEM remains unavailable */
    CHECK(vdi((VDIPB *)0) == EIMBA);
    CHECK(vdi((VDIPB *)0x30000000UL) == EIMBA);
    bad_block.contrl = (VDICONTROL *)&immutable_control;
    CHECK(vdi(&bad_block) == EIMBA);
    bad_block = pb;
    bad_block.intout = (WORD *)(uintptr_t)callback_timer;
    CHECK(call(&bad_block, 100, 11, 0) == EIMBA);
    CHECK(call(&pb, 100, 12, 0) == EINVAL);
    CHECK(raw_service(VDI_SERVICE_OPEN, 0, (uintptr_t)&vector, 0, 0) == EIMBA);
    CHECK(raw_service(VDI_SERVICE_OPEN, (uintptr_t)input, 0, 0, 0) == EIMBA);
    CHECK(call(&pb, 100, 11, 0) == E_OK);
    handle = control.handle;
    CHECK(handle >= 2 && integers[0] > 0 && integers[1] > 0);
    CHECK(!inherited_handle || handle != inherited_handle);
    CHECK(control.nintout == 45 && control.nptsout == 6);
    if (fault_after_open) {
        *(volatile ULONG *)0 = 1;       /* exit must reclaim the live workstation */
        return 99;
    }
    CHECK(clipping_checks(handle, integers[0], integers[1]) == 0);
    CHECK(call(&pb, 6, 0, 2) == E_OK);
    points[0] = 4;
    CHECK(call(&pb, 105, 0, 1) == E_OK);
    saved = integers[1];                /* truecolor returns a hardware index */
    input[0] = 0;
    CHECK(call(&pb, 17, 1, 0) == E_OK);
    points[0] = 1;
    CHECK(call(&pb, 6, 0, 2) == E_OK);
    points[0] = 4;
    CHECK(call(&pb, 105, 0, 1) == E_OK && integers[1] != saved);
    input[0] = 1;
    points[0] = points[1] = 0;
    points[2] = points[3] = 10;
    CHECK(call(&pb, 129, 1, 2) == E_OK);
    CHECK(call(&pb, 17, 1, 0) == E_OK && integers[0] == 1);
    CHECK(raw_service(VDI_SERVICE_LINE, handle, 257, (uintptr_t)points, 0) == EINVAL);
    CHECK(raw_service(VDI_SERVICE_LINE, handle, 2, 0, 0) == EIMBA);
    CHECK(raw_service(VDI_SERVICE_PIXEL, handle, 0xffff, 0, (uintptr_t)integers) == EINVAL);
    CHECK(raw_service(VDI_SERVICE_PIXEL, handle, 0, 0, 0) == EIMBA);
    CHECK(raw_service(VDI_SERVICE_VECTOR, handle, 118, 0x30000000UL, (uintptr_t)&vector) == EIMBA);
    CHECK(raw_service(VDI_SERVICE_VECTOR, handle, 118, (uintptr_t)input, (uintptr_t)&vector) == EIMBA);
    CHECK(raw_service(VDI_SERVICE_VECTOR, handle, 118, 0xffff800000000000ULL, (uintptr_t)&vector) == EIMBA);
    CHECK(raw_service(VDI_SERVICE_VECTOR, handle, 118, (uintptr_t)callback_timer, 0) == EIMBA);

    /* Every typed exchange validates its executable pointer and refuses
     * installation, leaving all kernel callbacks untouched. */
    control.args.timer.new_handler = callback_timer;
    CHECK(call(&pb, 118, 0, 0) == EINVFN && !control.args.timer.old_handler);
    control.args.button.new_handler = callback_button;
    CHECK(call(&pb, 125, 0, 0) == EINVFN && !control.args.button.old_handler);
    control.args.motion.new_handler = callback_motion;
    CHECK(call(&pb, 126, 0, 0) == EINVFN && !control.args.motion.old_handler);
    control.args.cursor.new_handler = callback_cursor;
    CHECK(call(&pb, 127, 0, 0) == EINVFN && !control.args.cursor.old_handler);
    control.args.wheel.new_handler = callback_wheel;
    CHECK(call(&pb, 134, 0, 0) == EINVFN && !control.args.wheel.old_handler);
    control.args.arrays.reserved[0] = control.args.arrays.reserved[1] = 0;

    source.fd_addr = bitmap_source;
    source.fd_w = destination.fd_w = 16;
    source.fd_h = destination.fd_h = 2;
    source.fd_wdwidth = destination.fd_wdwidth = 1;
    source.fd_nplanes = destination.fd_nplanes = 1;
    source.fd_stand = destination.fd_stand = 1;
    destination.fd_addr = bitmap_destination;
    control.args.raster.source = &source;
    control.args.raster.destination = &destination;
    for (i = 0; i < 8; i++)
        points[i] = 0;
    points[2] = points[6] = 15;
    points[3] = points[7] = 1;
    bitmap_source[0] = 0xa55a;
    bitmap_source[1] = 0x5aa5;
    input[0] = 3;
    CHECK(call(&pb, 109, 1, 4) == E_OK);
    CHECK(bitmap_destination[0] == 0xa55a && bitmap_destination[1] == 0x5aa5);
    input[0] = 6;
    CHECK(call(&pb, 109, 1, 4) == E_OK && bitmap_destination[0] == 0);
    input[0] = 1;
    input[1] = 1;
    input[2] = 0;
    CHECK(call(&pb, 121, 3, 4) == E_OK && bitmap_destination[0] == 0xa55a);
    CHECK(call(&pb, 110, 0, 0) == E_OK && destination.fd_stand == 0);
    CHECK(bitmap_destination[0] == 0xa55a && bitmap_destination[1] == 0x5aa5);
    control.args.raster.source = &destination;
    CHECK(call(&pb, 110, 0, 0) == E_OK && destination.fd_stand == 1);
    CHECK(bitmap_destination[0] == 0xa55a && bitmap_destination[1] == 0x5aa5);
    control.args.raster.source = &source;
    source.fd_addr = (void *)0x30000000UL;
    CHECK(call(&pb, 109, 1, 4) == EIMBA);
    source.fd_addr = (void *)0xfffffff0UL;
    source.fd_h = 32767;
    CHECK(call(&pb, 109, 1, 4) == EINVAL);
    source.fd_addr = bitmap_source;
    source.fd_h = 2;
    source.fd_nplanes = 0;
    CHECK(call(&pb, 109, 1, 4) == EINVAL);
    source.fd_nplanes = 1;
    control.args.raster.source = (VDI_USER_FDB *)0;
    CHECK(call(&pb, 109, 1, 4) == EIMBA);
    control.args.arrays.reserved[0] = control.args.arrays.reserved[1] = 0;
    if (!inherited_handle) {
        char tail[5] = { '@', (char)('0' + handle / 100),
                         (char)('0' + (handle / 10) % 10), (char)('0' + handle % 10), 0 };

        CHECK(raw_call(1, 0x4b, PE_LOADGO, (uintptr_t)"X32VDI.TOS", (uintptr_t)tail, 0) == 0);
        CHECK(raw_service(VDI_SERVICE_CHECK, handle, 0, 0, 0) == E_OK);
    }
    CHECK(call(&pb, 8, 0, 0) == EINVFN); /* unsupported pairs never reach legacy VDI */
    control.reserved = 1;
    CHECK(call(&pb, 101, 0, 0) == EINVAL);
    control.reserved = 0;
    saved = control.handle;
    CHECK(call(&pb, 101, 0, 0) == E_OK && control.handle == 0);
    CHECK(raw_service(VDI_SERVICE_CHECK, saved, 0, 0, 0) == EIHNDL);
#undef CHECK
    return failures;
#else
    (void)inherited_handle;
    (void)fault_after_open;
    return raw_service(VDI_SERVICE_OPEN, 0, 0, 0, 0) == EINVFN ? 0 : 1;
#endif
}
