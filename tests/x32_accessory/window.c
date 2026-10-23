/* Loaded ring-3 windows: ownership, copied buffers and real native routing. */
#include "../../include/config.h"
#include <mint/mintbind.h>
#include <stdio.h>
#include <string.h>
#include "aes_client.h"
#include "aes_service.h"
#include "gemerror.h"
#include "vdipb.h"

int accessory_window_checks(const AES_APP_INFO *info, WORD role, WORD vdi_handle);
int accessory_window_redraw(WORD handle, WORD vdi_handle);
int accessory_window_reopen(WORD role, WORD vdi_handle);
int accessory_window_borrow(void);
int accessory_window_pool(WORD command, WORD *handle);

static volatile LONG revoked, reregistered;
static volatile LONG cursor_freed;
static GRECT *cursor_output;
static volatile LONG return_revoked, return_registered, return_freed;
static GRECT *return_output;
static AES_APP_INFO callback_info;
static WORD peer_window;
static GRECT pool_previous;
static UWORD pool_kind;

static void revoke_window(WORD status)
{
    if (status == 626 || status == 628) {
        revoked = aes_appl_exit();
        reregistered = aes_appl_init("ACCTWO", &callback_info);
    }
}

static void free_cursor_output(WORD status)
{
    if (status == 629)
        cursor_freed = Mfree(cursor_output);
}

static void return_cursor_callback(WORD status)
{
    if (status == 630) {
        return_revoked = aes_appl_exit();
        return_registered = aes_appl_init("ACCTWO", &callback_info);
    } else if (status == 631)
        return_freed = Mfree(return_output);
}

#define CHECK(c) do { if (!(c)) { char failure[80]; \
    snprintf(failure, sizeof(failure), "accessory window FAIL line %d: ", __LINE__); \
    (void)Cconws(failure); (void)Cconws(#c "\r\n"); return 99; } } while (0)

static const UWORD kind = AES_WINDOW_NAME_GADGET | AES_WINDOW_CLOSER |
    AES_WINDOW_MOVER | AES_WINDOW_INFO_GADGET | AES_WINDOW_SIZER |
    AES_WINDOW_HSLIDE_GADGET | AES_WINDOW_VSLIDE_GADGET;
static const AES_WINDOW_INFO immutable_info = { 0 };
static const GRECT immutable_rect = { 0 };

int accessory_window_redraw(WORD handle, WORD vdi_handle)
{
    AES_WINDOW_INFO info;
    GRECT visible;
    VDICONTROL c = { 0 };
    WORD input[24] = { 0 }, output[2], points[4] = { 0 }, pointout[2], y;
    VDIPB pb = { &c, input, points, output, pointout };
    LONG result;

    do {
        result = aes_wind_update(AES_BEG_UPDATE);
        if (result == EAGAIN)
            CHECK(aes_appl_yield() == E_OK);
    } while (result == EAGAIN);
    CHECK(result == E_OK);
    do {
        result = aes_wind_get(handle, &info);
        if (result == EAGAIN)
            CHECK(aes_appl_yield() == E_OK);
    } while (result == EAGAIN);
    if (result == EIHNDL || result == EACCDN) {
        CHECK(aes_wind_update(AES_END_UPDATE) == E_OK);
        return 0;
    }
    CHECK(result == E_OK);
    c.handle = vdi_handle;
    CHECK(aes_wind_rect(handle, 0, &visible) == E_OK);
    while (visible.g_w && visible.g_h) {
        points[0] = visible.g_x; points[1] = visible.g_y;
        points[2] = visible.g_x + visible.g_w - 1;
        points[3] = visible.g_y + visible.g_h - 1;
        c.code = 129; c.nptsin = 2; c.nintin = 1; input[0] = 1;
        CHECK(vdi(&pb) == E_OK);
        /* Use the existing narrow VDI drawing ABI: color, clip and lines. */
        c.code = 17; c.nptsin = 0; input[0] = 0;
        CHECK(vdi(&pb) == E_OK);
        for (y = visible.g_y; y < visible.g_y + visible.g_h; y++) {
            points[0] = visible.g_x; points[1] = points[3] = y;
            points[2] = visible.g_x + visible.g_w - 1;
            c.code = 6; c.nintin = 0; c.nptsin = 2;
            CHECK(vdi(&pb) == E_OK);
        }
        c.code = 17; c.nintin = 1; c.nptsin = 0; input[0] = 1;
        CHECK(vdi(&pb) == E_OK);
        points[0] = info.work.g_x + 8; points[1] = info.work.g_y + 8;
        points[2] = info.work.g_x + info.work.g_w - 9;
        points[3] = info.work.g_y + info.work.g_h - 9;
        c.code = 6; c.nintin = 0; c.nptsin = 2;
        CHECK(vdi(&pb) == E_OK);
        points[1] = info.work.g_y + info.work.g_h - 9;
        points[3] = info.work.g_y + 8;
        CHECK(vdi(&pb) == E_OK);
        CHECK(aes_wind_rect(handle, 1, &visible) == E_OK);
    }
    points[0] = points[1] = 0; points[2] = points[3] = 1;
    c.code = 129; c.nptsin = 2; c.nintin = 1; input[0] = 0;
    CHECK(vdi(&pb) == E_OK);
    CHECK(aes_wind_update(AES_END_UPDATE) == E_OK);
    return 0;
}

static LONG raw_window(UQUAD request, UQUAD result, UQUAD extra)
{
    register UQUAD rax __asm__("rax") = ((UQUAD)AES_SERVICE_CLASS << 32) | AES_SERVICE_WINDOW;
    register UQUAD rdi __asm__("rdi") = request;
    register UQUAD rsi __asm__("rsi") = result;
    register UQUAD rdx __asm__("rdx") = extra;
    register UQUAD r10 __asm__("r10") = 0;

    __asm__ volatile ("syscall" : "+r"(rax)
                      : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10)
                      : "rcx", "r11", "memory", "cc");
    return (LONG)rax;
}

static int copied_text(WORD handle)
{
    LONG memory = Malloc(4096);
    char *text = (char *)(uintptr_t)(ULONG)memory;

    CHECK(memory > 0);
    strcpy(text, "Typed x32 window");
    CHECK(aes_wind_set_text(handle, AES_WINDOW_NAME, text) == E_OK);
    strcpy(text, "Copied window information");
    CHECK(aes_wind_set_text(handle, AES_WINDOW_INFO_LINE, text) == E_OK);
    memset(text, 'X', 4096);
    CHECK(Mfree(text) == E_OK);
    return 0;
}

static int boundary_resizes(void)
{
    GRECT small = { 0, 60, 340, 240 }, large;
    AES_WINDOW_INFO state;
    AES_MESSAGE message;
    LONG handle;
    WORD axis, size, redraws;

    for (axis = 0; axis < 2; axis++) {
        small.g_x = axis ? 60 : 0;
        small.g_y = axis ? 0 : 60;
        handle = aes_wind_create(0, &small);
        CHECK(handle > 0);
        for (size = 32764; size <= 32765; size++) {
            large = small;
            if (axis) large.g_h = size;
            else large.g_w = size;
            CHECK(aes_wind_create(0, &large) == EINVAL);
            CHECK(aes_wind_open((WORD)handle, &large) == EINVAL);
            CHECK(aes_wind_get((WORD)handle, &state) == E_OK && !state.flags);
        }
        CHECK(aes_wind_open((WORD)handle, &small) == E_OK);
        while (aes_appl_read(&message) == E_OK)
            CHECK(message.type == 20);
        for (size = 32764; size <= 32765; size++) {
            large = small;
            if (axis) large.g_h = size;
            else large.g_w = size;
            CHECK(aes_wind_set_rect((WORD)handle, &large) == EINVAL);
            CHECK(aes_wind_get((WORD)handle, &state) == E_OK && rc_equal(&small, &state.current));
            CHECK(aes_appl_read(&message) == EAGAIN);
        }
        /* The largest accepted dimension leaves room for both native
         * shadow additions. Growth must still deliver a positive redraw. */
        large = small;
        if (axis) large.g_h = 32763;
        else large.g_w = 32763;
        CHECK(aes_wind_set_rect((WORD)handle, &large) == E_OK);
        CHECK(aes_wind_get((WORD)handle, &state) == E_OK && rc_equal(&large, &state.current));
        redraws = 0;
        while (aes_appl_read(&message) == E_OK) {
            CHECK(message.type == 20 && message.data[0] == handle);
            CHECK(message.data[3] > 0 && message.data[4] > 0);
            redraws++;
        }
        CHECK(redraws > 0);
        /* Shrinking uses the old large dimensions in the same arithmetic. */
        CHECK(aes_wind_set_rect((WORD)handle, &small) == E_OK);
        CHECK(aes_wind_get((WORD)handle, &state) == E_OK && rc_equal(&small, &state.current));
        CHECK(rc_equal(&large, &state.previous));
        CHECK(aes_wind_delete((WORD)handle) == E_OK);
    }
    (void)Cconws("x32 window boundary resizes: PASS\r\n");
    return 0;
}

static int redraw_bounds(WORD id)
{
    AES_WINDOW_INFO desktop, state;
    AES_MESSAGE message = { 646, 0, 0, { 0 } }, received;
    GRECT offscreen, rectangle, dirty, expected, covered;
    LONG handle;
    WORD axis, full, i;

    CHECK(aes_wind_get(0, &desktop) == E_OK);
    for (axis = 0; axis < 2; axis++) {
        for (full = 0; full < 2; full++) {
            while (aes_appl_read(&received) == E_OK)
                CHECK(received.type == 20);
            if (full)
                for (i = 0; i < AES_MESSAGE_QUEUE; i++) {
                    message.data[0] = i;
                    CHECK(aes_appl_write(id, &message) == E_OK);
                }
            offscreen.g_x = axis ? 60 : 30000;
            offscreen.g_y = axis ? 30000 : 60;
            offscreen.g_w = 340;
            offscreen.g_h = 240;
            handle = aes_wind_create(0, &offscreen);
            CHECK(handle > 0 && aes_wind_open((WORD)handle, &offscreen) == E_OK);
            /* Do not drain redraws between these moves. Their combined
             * raw bounds exceed 32767 although each move is valid. */
            rectangle = offscreen;
            if (axis) {
                rectangle.g_y = 0;
                rectangle.g_h += 4;
            } else {
                rectangle.g_x = 0;
                rectangle.g_w += 4;
            }
            CHECK(aes_wind_set_rect((WORD)handle, &rectangle) == E_OK);
            if (axis) {
                rectangle.g_y = -30000;
                rectangle.g_h = 31000;
            } else {
                rectangle.g_x = -30000;
                rectangle.g_w = 31000;
            }
            CHECK(aes_wind_set_rect((WORD)handle, &rectangle) == E_OK);
            CHECK(aes_wind_get((WORD)handle, &state) == E_OK &&
                  rc_equal(&rectangle, &state.current));
            if (full)
                for (i = 0; i < AES_MESSAGE_QUEUE; i++) {
                    CHECK(aes_appl_read(&received) == E_OK);
                    CHECK(received.type == 646 && received.data[0] == i);
                }
            CHECK(aes_appl_read(&received) == E_OK);
            CHECK(received.type == 20 && !received.sender && received.data[0] == handle);
            memcpy(&dirty, received.data + 1, sizeof(dirty));
            CHECK(dirty.g_w > 0 && dirty.g_h > 0);
            covered = dirty;
            CHECK(rc_intersect(&desktop.current, &covered) && rc_equal(&dirty, &covered));
            expected = state.work;
            CHECK(rc_intersect(&desktop.work, &expected));
            covered = dirty;
            CHECK(rc_intersect(&expected, &covered) && rc_equal(&expected, &covered));
            CHECK(aes_appl_read(&received) == EAGAIN);
            /* A wholly off-screen open must leave no redraw to deliver. */
            CHECK(aes_wind_close((WORD)handle) == E_OK);
            while (aes_appl_read(&received) == E_OK)
                CHECK(received.type == 20);
            CHECK(aes_wind_open((WORD)handle, &offscreen) == E_OK);
            CHECK(aes_appl_read(&received) == EAGAIN);
            CHECK(aes_wind_delete((WORD)handle) == E_OK);
        }
    }
    (void)Cconws("x32 redraw coalescing bounds: PASS\r\n");
    return 0;
}

static int mover_boundaries(void)
{
    GRECT small = { 0, 60, 340, 240 }, large;
    AES_WINDOW_INFO state;
    UWORD kinds[2] = { AES_WINDOW_NAME_GADGET | AES_WINDOW_MOVER, AES_WINDOW_MOVER };
    LONG low, high, middle, handle;
    WORD i;

    large = small; large.g_w = 32763;
    handle = aes_wind_create(AES_WINDOW_NAME_GADGET, &large);
    CHECK(handle > 0 && aes_wind_delete((WORD)handle) == E_OK);
    for (i = 0; i < 2; i++) {
        CHECK(aes_wind_create(kinds[i], &large) == EINVAL);
        /* Find the screen/font-dependent boundary through the public ABI,
         * then check both sides for create/open and same-origin resizing. */
        low = small.g_w; high = 32762;
        while (low < high) {
            middle = low + (high - low + 1) / 2;
            large.g_w = middle;
            handle = aes_wind_create(kinds[i], &large);
            if (handle > 0) {
                CHECK(aes_wind_delete((WORD)handle) == E_OK);
                low = middle;
            } else {
                CHECK(handle == EINVAL);
                high = middle - 1;
            }
        }
        CHECK(low > small.g_w && low < 32763);
        large.g_w = low + 1;
        CHECK(aes_wind_create(kinds[i], &large) == EINVAL);
        handle = aes_wind_create(kinds[i], &small);
        CHECK(handle > 0 && aes_wind_open((WORD)handle, &large) == EINVAL);
        CHECK(aes_wind_get((WORD)handle, &state) == E_OK && !state.flags);
        CHECK(aes_wind_open((WORD)handle, &small) == E_OK);
        CHECK(aes_wind_set_rect((WORD)handle, &large) == EINVAL);
        CHECK(aes_wind_get((WORD)handle, &state) == E_OK && rc_equal(&small, &state.current));
        large.g_w = low;
        CHECK(aes_wind_set_rect((WORD)handle, &large) == E_OK);
        CHECK(aes_wind_get((WORD)handle, &state) == E_OK && rc_equal(&large, &state.current));
        CHECK(aes_wind_set_rect((WORD)handle, &small) == E_OK);
        CHECK(aes_wind_delete((WORD)handle) == E_OK);
        large.g_w = 32763;
    }
    (void)Cconws("x32 mover drag boundaries: PASS\r\n");
    return 0;
}

static int slider_gadgets(void)
{
    static const UWORD kinds[] = {
        0, AES_WINDOW_NAME_GADGET | AES_WINDOW_MOVER,
        AES_WINDOW_SIZER | AES_WINDOW_UPARROW | AES_WINDOW_DNARROW |
            AES_WINDOW_LFARROW | AES_WINDOW_RTARROW,
        AES_WINDOW_HSLIDE_GADGET, AES_WINDOW_VSLIDE_GADGET,
        AES_WINDOW_HSLIDE_GADGET | AES_WINDOW_VSLIDE_GADGET
    };
    static const UWORD required[] = {
        AES_WINDOW_HSLIDE_GADGET, AES_WINDOW_VSLIDE_GADGET,
        AES_WINDOW_HSLIDE_GADGET, AES_WINDOW_VSLIDE_GADGET
    };
    GRECT rectangle = { 40, 60, 340, 240 };
    AES_WINDOW_INFO before, after;
    LONG handle, result;
    UWORD i, field;
    WORD opened, value;

    for (i = 0; i < ARRAY_SIZE(kinds); i++) {
        handle = aes_wind_create(kinds[i], &rectangle);
        CHECK(handle > 0);
        for (opened = 0; opened < 2; opened++) {
            if (opened) {
                CHECK(aes_wind_open((WORD)handle, &rectangle) == E_OK);
                CHECK(aes_wind_top((WORD)handle) == E_OK);
            }
            for (field = 0; field < ARRAY_SIZE(required); field++) {
                CHECK(aes_wind_get((WORD)handle, &before) == E_OK);
                if (opened)
                    CHECK(before.top == handle);
                value = 500 + opened * 100 + field;
                result = aes_wind_slider((WORD)handle, field, value);
                if (kinds[i] & required[field]) {
                    CHECK(result == E_OK);
                    switch (field) {
                    case AES_WINDOW_HSLIDE: before.hslide = value; break;
                    case AES_WINDOW_VSLIDE: before.vslide = value; break;
                    case AES_WINDOW_HSLSIZ: before.hslsiz = value; break;
                    case AES_WINDOW_VSLSIZ: before.vslsiz = value; break;
                    }
                } else
                    CHECK(result == EINVAL);
                /* Rejection preserves all state; accepted updates change
                 * only the selected slider, including on topped windows. */
                CHECK(aes_wind_get((WORD)handle, &after) == E_OK);
                CHECK(!memcmp(&before, &after, sizeof(before)));
            }
        }
        CHECK(aes_wind_delete((WORD)handle) == E_OK);
    }
    (void)Cconws("x32 slider gadget validation: PASS\r\n");
    return 0;
}

int accessory_window_pool(WORD command, WORD *handle)
{
    AES_WINDOW_INFO state;
    GRECT strip = { 40 + 165 * (NUM_WIN / 2 - 2), 20, 20, 740 };

    if (command == 640) {
        CHECK(aes_wind_get(peer_window, &state) == E_OK);
        pool_previous = state.current;
        pool_kind = state.kind;
        CHECK(aes_wind_delete(peer_window) == E_OK);
        peer_window = aes_wind_create(0, &pool_previous);
        CHECK(peer_window > 0);
    } else if (command == 642)
        CHECK(aes_wind_open(peer_window, &strip) == E_OK);
    else {
        CHECK(command == 644);
        CHECK(aes_wind_delete(peer_window) == E_OK);
        peer_window = aes_wind_create(pool_kind, &pool_previous);
        CHECK(peer_window > 0 && copied_text(peer_window) == 0);
        CHECK(aes_wind_open(peer_window, &pool_previous) == E_OK);
    }
    *handle = peer_window;
    return 0;
}

static int pool_command(WORD peer, WORD command, WORD *handle)
{
    AES_MESSAGE message = { 0 };
    AES_EVENT event;

    message.type = command;
    CHECK(aes_appl_write(peer, &message) == E_OK);
    do {
        CHECK(aes_evnt_wait(AES_EVENT_MESSAGE, &event) == AES_EVENT_MESSAGE);
    } while (event.message.type >= 20 && event.message.type <= 31);
    CHECK(event.message.type == command + 1 && event.message.sender == peer);
    *handle = event.message.data[0];
    return 0;
}

static int rectangle_pool(WORD *peer_handle)
{
    WORD handles[NUM_WIN - 2], i, count, peer;
    GRECT strip, visible;
    AES_WINDOW_INFO state;

    peer = aes_appl_find("ACCWAIT");
    CHECK(peer > 0 && pool_command(peer, 640, peer_handle) == 0);
    /* With AES 3.40, all fifteen user handles form the reported eight
     * horizontal and seven vertical strips. The other client owns the
     * final vertical strip, opened after our fourteen windows. */
    for (i = 0; i < NUM_WIN - 2; i++) {
        if (i < NUM_WIN / 2) {
            strip.g_x = 20; strip.g_y = 30 + 87 * i;
            strip.g_w = 1200; strip.g_h = 20;
        } else {
            strip.g_x = 40 + 165 * (i - NUM_WIN / 2); strip.g_y = 20;
            strip.g_w = 20; strip.g_h = 740;
        }
        handles[i] = aes_wind_create(0, &strip);
        CHECK(handles[i] > 0 && aes_wind_open(handles[i], &strip) == E_OK);
    }
    CHECK(pool_command(peer, 642, peer_handle) == 0);
    CHECK(aes_wind_create(0, &strip) == ENSMEM);
    for (i = 0; i < NUM_WIN - 2; i++) {
        CHECK(aes_wind_top(handles[i]) == E_OK);
        CHECK(aes_wind_get(handles[i], &state) == E_OK);
        CHECK(aes_wind_rect(handles[i], 0, &visible) == E_OK);
        CHECK(rc_equal(&visible, &state.work));
        count = 0;
        while (visible.g_w && visible.g_h) {
            CHECK(++count < 1024);
            CHECK(rc_intersect(&state.work, &visible));
            CHECK(aes_wind_rect(handles[i], 1, &visible) == E_OK);
        }
        CHECK(count == 1);
    }
    /* Retain every closed window's list until all windows are closed,
     * then reclaim the handles and restore the other client's window. */
    for (i = 0; i < NUM_WIN - 2; i++)
        CHECK(aes_wind_close(handles[i]) == E_OK);
    for (i = 0; i < NUM_WIN - 2; i++)
        CHECK(aes_wind_delete(handles[i]) == E_OK);
    CHECK(pool_command(peer, 644, peer_handle) == 0);
    (void)Cconws("x32 rectangle pool overlap: PASS\r\n");
    return 0;
}

static int cursor_copyout(WORD vdi_handle)
{
    GRECT border = { 40, 60, 340, 240 }, cover = { 180, 60, 30, 240 };
    GRECT offscreen = { 2000, 2000, 40, 40 }, first, second, visible;
    LONG handle, overlay, cleanup, memory;
    WORD next;
    VDICONTROL control = { 0 };
    VDIPB pb = { &control, NULL, NULL, NULL, NULL };
    VDI_BUTTON_CALLBACK *old_button;

    handle = aes_wind_create(0, &border);
    CHECK(handle > 0 && aes_wind_open((WORD)handle, &border) == E_OK);
    overlay = aes_wind_create(0, &cover);
    CHECK(overlay > 0 && aes_wind_open((WORD)overlay, &cover) == E_OK);
    /* Copying text without either gadget must not try to redraw a missing
     * native object. These plain windows deliberately exercise both cases. */
    CHECK(aes_wind_set_text((WORD)handle, AES_WINDOW_NAME, "Cursor probe") == E_OK);
    CHECK(aes_wind_set_text((WORD)handle, AES_WINDOW_INFO_LINE, "No info gadget") == E_OK);
    CHECK(aes_wind_rect((WORD)handle, 0, &first) == E_OK && first.g_w > 0);
    CHECK(aes_wind_rect((WORD)handle, 1, &second) == E_OK && second.g_w > 0);
    CHECK(!rc_equal(&first, &second));
    CHECK(aes_wind_rect((WORD)handle, 1, &visible) == E_OK && !visible.g_w && !visible.g_h);
    control.code = 125; control.handle = vdi_handle;
    control.args.button.new_handler = free_cursor_output;
    CHECK(vdi(&pb) == E_OK);
    old_button = control.args.button.old_handler;
    for (next = 0; next < 2; next++) {
        CHECK(aes_wind_rect((WORD)handle, 0, &visible) == E_OK && rc_equal(&first, &visible));
        if (!next)
            CHECK(aes_wind_rect((WORD)handle, 1, &visible) == E_OK && rc_equal(&second, &visible));
        cleanup = aes_wind_create(0, &offscreen);
        CHECK(cleanup > 0 && aes_wind_open((WORD)cleanup, &offscreen) == E_OK);
        CHECK(aes_wind_set_text((WORD)cleanup, AES_WINDOW_NAME, "Cursor cleanup") == E_OK);
        memory = Malloc(4096);
        CHECK(memory > 0);
        cursor_output = (GRECT *)(uintptr_t)(ULONG)memory;
        cursor_freed = EINVAL;
        CHECK(aes_wind_rect((WORD)handle, next, cursor_output) == EIMBA);
        CHECK(cursor_freed == E_OK && aes_appl_find("ACCTWO") > 0);
        /* A failed FIRST must not reset the exhausted cursor; a failed
         * NEXT must leave the second, undelivered rectangle available. */
        CHECK(aes_wind_rect((WORD)handle, 1, &visible) == E_OK);
        if (next)
            CHECK(rc_equal(&second, &visible));
        else
            CHECK(!visible.g_w && !visible.g_h);
        CHECK(aes_wind_rect((WORD)handle, 0, &visible) == E_OK && rc_equal(&first, &visible));
    }
    cursor_output = NULL;
    control.args.button.new_handler = old_button;
    CHECK(vdi(&pb) == E_OK);
    CHECK(aes_wind_delete((WORD)overlay) == E_OK);
    CHECK(aes_wind_delete((WORD)handle) == E_OK);
    (void)Cconws("x32 visible rectangle copyout: PASS\r\n");
    return 0;
}

static int cursor_return(const AES_APP_INFO *info, WORD vdi_handle)
{
    GRECT rectangle = { 40, 60, 340, 240 }, offscreen = { 2000, 2000, 40, 40 };
    LONG handle, cleanup, memory;
    AES_WINDOW_INFO state;
    VDICONTROL control = { 0 };
    VDIPB pb = { &control, NULL, NULL, NULL, NULL };
    VDI_BUTTON_CALLBACK *old_button;

    handle = aes_wind_create(kind, &rectangle);
    CHECK(handle > 0 && aes_wind_open((WORD)handle, &rectangle) == E_OK);
    CHECK(aes_wind_set_text((WORD)handle, AES_WINDOW_NAME, "Return cursor probe") == E_OK);
    cleanup = aes_wind_create(0, &offscreen);
    CHECK(cleanup > 0 && aes_wind_open((WORD)cleanup, &offscreen) == E_OK);
    CHECK(aes_wind_set_text((WORD)cleanup, AES_WINDOW_NAME, "Return cursor cleanup") == E_OK);
    control.code = 125; control.handle = vdi_handle;
    control.args.button.new_handler = return_cursor_callback;
    CHECK(vdi(&pb) == E_OK);
    old_button = control.args.button.old_handler;
    memory = Malloc(4096);
    CHECK(memory > 0);
    return_output = (GRECT *)(uintptr_t)(ULONG)memory;
    return_revoked = return_registered = return_freed = EINVAL;
    CHECK(aes_wind_rect((WORD)handle, 0, return_output) == E_OK);
    /* The kernel checks the completed return before SYSRET. User IRQs may
     * now deliver the deferred callback, so do not dereference its freed
     * buffer. The next ordinary syscall must also permit pending hooks. */
    CHECK(aes_appl_find("ACCTWO") == info->id);
    CHECK(return_revoked == E_OK && return_registered == E_OK && return_freed == E_OK);
    CHECK(callback_info.id == info->id);
    CHECK(aes_wind_get((WORD)handle, &state) == EIHNDL);
    CHECK(aes_wind_get((WORD)cleanup, &state) == EIHNDL);
    control.args.button.new_handler = old_button;
    CHECK(vdi(&pb) == E_OK);
    return_output = NULL;
    (void)Cconws("x32 visible rectangle return hooks: PASS\r\n");
    return 0;
}

static int closed_geometry(WORD visible_handle, const GRECT *rectangle)
{
    GRECT before, after;
    AES_WINDOW_INFO state;
    LONG handle;
    WORD i;

    CHECK(aes_wind_rect(visible_handle, 0, &before) == E_OK && before.g_w > 0);
    /* Repeated deletion of a never-opened window must neither obscure its
     * neighbor nor retain a rectangle node. */
    for (i = 0; i < 192; i++) {
        handle = aes_wind_create(0, rectangle);
        CHECK(handle > 0 && aes_wind_set_rect((WORD)handle, rectangle) == E_OK);
        CHECK(aes_wind_get((WORD)handle, &state) == E_OK && !state.flags);
        CHECK(rc_equal(rectangle, &state.current));
        CHECK(aes_wind_delete((WORD)handle) == E_OK);
        CHECK(aes_wind_rect(visible_handle, 0, &after) == E_OK && rc_equal(&before, &after));
    }
    (void)Cconws("x32 closed window geometry cleanup: PASS\r\n");
    return 0;
}

static int deferred_arrows(WORD handle, WORD id)
{
    AES_MESSAGE message = { 632, 0, 0, { 0 } };
    AES_EVENT event;
    AES_INPUT_EVENT input;
    AES_EVENT_REQUEST wait = { AES_EVENT_MESSAGE | AES_EVENT_TIMER, 0, 0, 0, 0, 0,
                               { 0 }, { 0 }, 1000 };
    WORD i;

    CHECK(aes_wind_set_text(handle, AES_WINDOW_NAME, "Deferred arrow test") == E_OK);
    while (aes_appl_read(&message) == E_OK)
        CHECK(message.type == 20);
    message.type = 632;
    for (i = 0; i < AES_MESSAGE_QUEUE; i++) {
        message.data[0] = i;
        CHECK(aes_appl_write(id, &message) == E_OK);
    }
    CHECK(aes_appl_yield() == E_OK); /* native action 0 while runnable */
    CHECK(aes_appl_read((AES_MESSAGE *)&immutable_info) == EIMBA);
    for (i = 0; i < AES_MESSAGE_QUEUE; i++) {
        CHECK(aes_appl_read(&message) == E_OK);
        CHECK(message.type == 632 && message.sender == id && message.data[0] == i);
    }
    CHECK(aes_appl_read(&message) == E_OK);
    CHECK(message.type == 24 && !message.sender && message.data[0] == handle && !message.data[1]);
    CHECK(aes_appl_yield() == E_OK); /* native action 1 */
    CHECK(aes_evnt_poll(AES_EVENT_MESSAGE, &event) == AES_EVENT_MESSAGE);
    CHECK(event.message.type == 24 && !event.message.sender &&
          event.message.data[0] == handle && event.message.data[1] == 1);
    CHECK(aes_appl_yield() == E_OK); /* native action 2 */
    CHECK(aes_evnt_wait(AES_EVENT_MESSAGE, &event) == AES_EVENT_MESSAGE);
    CHECK(event.message.type == 24 && !event.message.sender &&
          event.message.data[0] == handle && event.message.data[1] == 2);
    CHECK(aes_appl_yield() == E_OK); /* native action 3 */
    CHECK(aes_evnt_multi(&wait, &input) == AES_EVENT_MESSAGE);
    CHECK(input.message.type == 24 && !input.message.sender &&
          input.message.data[0] == handle && input.message.data[1] == 3);
    CHECK(aes_appl_read(&message) == EAGAIN);
    CHECK(copied_text(handle) == 0);
    (void)Cconws("x32 deferred arrow delivery: PASS\r\n");
    return 0;
}

static int replacement_deletion(const AES_APP_INFO *info, const GRECT *rectangle)
{
    LONG handle;
    AES_WINDOW_INFO state;
    AES_APP_INFO renewed;

    handle = aes_wind_create(kind, rectangle);
    CHECK(handle > 0);
    CHECK(aes_wind_set_text((WORD)handle, AES_WINDOW_NAME, "Replace during delete") == E_OK);
    CHECK(aes_wind_open((WORD)handle, rectangle) == E_OK);
    CHECK(aes_wind_delete((WORD)handle) == EIHNDL);
    CHECK(aes_wind_get((WORD)handle, &state) == EACCDN);
    handle = aes_wind_create(kind, rectangle);
    CHECK(handle > 0);
    CHECK(aes_wind_set_text((WORD)handle, AES_WINDOW_NAME, "Replace during cleanup") == E_OK);
    CHECK(aes_wind_open((WORD)handle, rectangle) == E_OK);
    CHECK(aes_appl_exit() == E_OK);
    CHECK(aes_appl_init("ACCTWO", &renewed) == E_OK && renewed.id == info->id);
    CHECK(aes_wind_get((WORD)handle, &state) == EACCDN);
    (void)Cconws("x32 replacement deletion isolation: PASS\r\n");
    return 0;
}

static int cleanup_synchronization(const GRECT *rectangle)
{
    LONG first, later, probe, created;
    AES_WINDOW_INFO state;
    WORD i;

    for (i = 0; i < 3; i++) {
        first = aes_wind_create(kind, rectangle);
        CHECK(first > 0);
        CHECK(aes_wind_set_text((WORD)first, AES_WINDOW_NAME, "Sync cleanup") == E_OK);
        CHECK(aes_wind_open((WORD)first, rectangle) == E_OK);
        later = aes_wind_create(kind, rectangle);
        CHECK(later > first);
        CHECK(aes_wind_set_text((WORD)later, AES_WINDOW_NAME, "Sync later") == E_OK);
        CHECK(aes_wind_open((WORD)later, rectangle) == E_OK);
        probe = aes_wind_create(0, rectangle);
        CHECK(probe > 0);
        CHECK(aes_wind_set_text((WORD)probe, AES_WINDOW_NAME, "Sync probe") == E_OK);
        /* Retry releases the simulated foreign owner, then finishes cleanup
         * before executing the request. Native contention must prevent a
         * new allocation; each typed token must prevent query copyout. */
        if (!i) {
            CHECK(aes_wind_create(0, rectangle) == EAGAIN);
            created = aes_wind_create(0, rectangle);
            CHECK(created > 0 && aes_wind_delete((WORD)created) == E_OK);
        } else {
            memset(&state, 0x6c, sizeof(state));
            CHECK(aes_wind_get((WORD)probe, &state) == EAGAIN);
            CHECK(state.handle == 0x6c6c && state.reserved == 0x6c6c);
            CHECK(aes_wind_get((WORD)probe, &state) == E_OK && state.handle == probe);
        }
        CHECK(aes_wind_get((WORD)first, &state) == EIHNDL);
        CHECK(aes_wind_get((WORD)later, &state) == EIHNDL);
        CHECK(aes_wind_delete((WORD)probe) == E_OK);
    }
    (void)Cconws("x32 cleanup synchronization recheck: PASS\r\n");
    return 0;
}

int accessory_window_borrow(void)
{
    AES_WINDOW_INFO state;

    CHECK(peer_window > 0);
    CHECK(aes_wind_set_text(peer_window, AES_WINDOW_NAME, "Borrowed trigger") == E_OK);
    CHECK(aes_wind_get(peer_window, &state) == E_OK);
    CHECK(copied_text(peer_window) == 0);
    return 0;
}

static int borrowed_cleanup(const AES_APP_INFO *info, WORD vdi_handle, const GRECT *rectangle)
{
    LONG old, replacement, peer, result;
    AES_APP_INFO renewed;
    AES_WINDOW_INFO state;
    AES_EVENT event;
    AES_MESSAGE message = { 636, 0, 0, { 0 } };
    VDICONTROL control = { 0 };
    VDIPB pb = { &control, NULL, NULL, NULL, NULL };
    VDI_BUTTON_CALLBACK *old_button;

    old = aes_wind_create(kind, rectangle);
    CHECK(old > 0 && aes_wind_open((WORD)old, rectangle) == E_OK);
    CHECK(aes_wind_set_text((WORD)old, AES_WINDOW_NAME, "Borrowed cleanup") == E_OK);
    control.code = 125; control.handle = vdi_handle;
    control.args.button.new_handler = revoke_window;
    CHECK(vdi(&pb) == E_OK);
    old_button = control.args.button.old_handler;
    revoked = reregistered = EINVAL;
    peer = aes_appl_find("ACCWAIT");
    CHECK(peer > 0 && aes_appl_write((WORD)peer, &message) == E_OK);
    do {
        result = aes_evnt_wait(AES_EVENT_MESSAGE, &event);
        CHECK(result == EACCDN || result == AES_EVENT_MESSAGE);
    } while (result == EACCDN || event.message.type == 20);
    CHECK(event.message.type == 638 && !event.message.sender && event.message.data[0] == old);
    CHECK(revoked == E_OK && reregistered == E_OK && callback_info.id == info->id);
    /* The other task is suspended in cleanup of our now-closed window.
     * Normal unregister must preserve its reservation across registration. */
    CHECK(aes_appl_exit() == E_OK);
    CHECK(aes_appl_init("ACCTWO", &renewed) == E_OK && renewed.id == info->id);
    replacement = aes_wind_create(kind, rectangle);
    CHECK(replacement > 0 && replacement != old);
    CHECK(aes_wind_get((WORD)replacement, &state) == E_OK && !state.flags);
    message.type = 637;
    CHECK(aes_appl_write((WORD)peer, &message) == E_OK);
    do {
        CHECK(aes_evnt_wait(AES_EVENT_MESSAGE, &event) == AES_EVENT_MESSAGE);
    } while (event.message.type == 20);
    CHECK(event.message.type == 639 && event.message.sender == peer);
    CHECK(aes_wind_get((WORD)replacement, &state) == E_OK && !state.flags);
    CHECK(aes_wind_get((WORD)old, &state) == EIHNDL);
    CHECK(aes_wind_delete((WORD)replacement) == E_OK);
    control.args.button.new_handler = old_button;
    CHECK(vdi(&pb) == E_OK);
    (void)Cconws("x32 borrowed cleanup reservation: PASS\r\n");
    return 0;
}

int accessory_window_object_restore(WORD vdi_handle);
int accessory_window_object_restore(WORD vdi_handle)
{
    GRECT rectangle = { 48, 60, 356, 240 };
    LONG handle = aes_wind_create(kind, &rectangle);
    CHECK(handle > 0 && copied_text((WORD)handle) == 0);
    CHECK(aes_wind_open((WORD)handle, &rectangle) == E_OK);
    CHECK(accessory_window_redraw((WORD)handle, vdi_handle) == 0);
    CHECK(aes_wind_set_text((WORD)handle, AES_WINDOW_NAME, "Objects complete") == E_OK);
    return 0;
}

int accessory_window_reopen(WORD role, WORD vdi_handle)
{
    GRECT rectangle = { 48, 60, 356, 240 };
    LONG handle;

    if (role == 4) {
        rectangle.g_x = 440;
        rectangle.g_y = 140;
    }
    handle = aes_wind_create(kind, &rectangle);
    CHECK(handle > 0 && copied_text((WORD)handle) == 0);
    CHECK(aes_wind_open((WORD)handle, &rectangle) == E_OK);
    CHECK(accessory_window_redraw((WORD)handle, vdi_handle) == 0);
    if (role == 4)
        peer_window = handle;
    (void)Cconws("x32 window reopened on desktop: PASS\r\n");
    return 0;
}

int accessory_window_checks(const AES_APP_INFO *info, WORD role, WORD vdi_handle)
{
    static const char *names[3] = { "ACCONE", "ACCFAULT", "ACCWAIT" };
    AES_MESSAGE message = { 626, 0, 0, { 0 } }, received;
    AES_EVENT event;
    AES_EVENT_REQUEST wait = { AES_EVENT_MESSAGE | AES_EVENT_TIMER, 0, 0, 0, 0, 0,
                               { 0 }, { 0 }, 3000 };
    AES_INPUT_EVENT input_event;
    AES_WINDOW_INFO state, desktop;
    AES_APP_INFO renewed;
    AES_WINDOW_REQUEST raw = { AES_WIN_GET, 0 };
    VDICONTROL control = { 0 };
    VDIPB pb = { &control, NULL, NULL, NULL, NULL };
    VDI_BUTTON_CALLBACK *old_button;
    GRECT rectangle = { 40, 60, 340, 240 }, calculated, work, visible;
    union { AES_WINDOW_REQUEST request; AES_WINDOW_INFO result; } alias;
    char line[100], too_long[AES_WINDOW_TEXT_MAX + 1];
    LONG handle, peer, target, extra, memory;
    WORD exhausted[32];
    UBYTE *edge;
    WORD i, count, pool_peer;

    CHECK(info->features & AES_FEATURE_WINDOW);
    if (role != 2 && role != 4) {
        CHECK(aes_evnt_wait(AES_EVENT_MESSAGE, &event) == AES_EVENT_MESSAGE);
        CHECK(event.message.type == 626);
    }
    if (role != 2) {
        rectangle.g_x += role * 100;
        rectangle.g_y += role * 20;
        handle = aes_wind_create(kind, &rectangle);
        CHECK(handle > 0 && copied_text((WORD)handle) == 0);
        CHECK(aes_wind_open((WORD)handle, &rectangle) == E_OK);
        if (role == 4) {
            peer_window = handle;
            peer = aes_appl_find("ACCTWO");
            CHECK(peer > 0);
            message.type = 627; message.data[0] = handle;
            CHECK(aes_appl_write((WORD)peer, &message) == E_OK);
        } else {
            /* Reclaim both a live window and a created-but-closed window
             * when this task returns normally or faults. */
            extra = aes_wind_create(kind, &rectangle);
            CHECK(extra > 0 && copied_text((WORD)extra) == 0);
            CHECK(aes_wind_set_rect((WORD)extra, &rectangle) == E_OK);
        }
        return 0;
    }
    for (i = 0; i < 3; i++) {
        target = aes_appl_find(names[i]);
        CHECK(target > 0);
        CHECK(aes_appl_write((WORD)target, &message) == E_OK);
    }
    do {
        CHECK(aes_evnt_multi(&wait, &input_event) == AES_EVENT_MESSAGE);
    } while (input_event.message.type == 20);
    CHECK(input_event.message.type == 627);
    peer = input_event.message.data[0];
    CHECK(peer > 0);
    CHECK(aes_wind_get((WORD)peer, &state) == EACCDN);
    CHECK(aes_wind_open((WORD)peer, &rectangle) == EACCDN);
    CHECK(aes_wind_close((WORD)peer) == EACCDN);
    CHECK(aes_wind_delete((WORD)peer) == EACCDN);
    CHECK(aes_wind_top((WORD)peer) == EACCDN);
    CHECK(aes_wind_set_rect((WORD)peer, &rectangle) == EACCDN);
    CHECK(aes_wind_set_text((WORD)peer, AES_WINDOW_NAME, "stolen") == EACCDN);
    CHECK(aes_wind_slider((WORD)peer, AES_WINDOW_HSLIDE, 500) == EACCDN);
    CHECK(aes_wind_rect((WORD)peer, 0, &visible) == EACCDN);
    CHECK(aes_wind_get(0, &desktop) == E_OK && !desktop.owner && !desktop.reserved);
    CHECK(desktop.work.g_w > 0 && desktop.work.g_h > 0);
    CHECK(aes_wind_delete(0) == EACCDN);
    CHECK(aes_wind_get(-1, &state) == EIHNDL);
    CHECK(aes_wind_get(32767, &state) == EIHNDL);
    CHECK(aes_wind_create(0x8000, &rectangle) == EINVAL);
    CHECK(aes_wind_create(kind, (const GRECT *)1) == EIMBA);
    CHECK(aes_wind_get(0, (AES_WINDOW_INFO *)&immutable_info) == EIMBA);
    CHECK(raw_window(0x100000000ULL + (uintptr_t)&raw, (uintptr_t)&state, 0) == EIMBA);
    CHECK(raw_window((uintptr_t)&raw, 0xffff800000000000ULL, 0) == EIMBA);
    CHECK(raw_window((uintptr_t)&raw, (uintptr_t)&state, 1) == EINVAL);
    raw.reserved = 1;
    CHECK(raw_window((uintptr_t)&raw, (uintptr_t)&state, 0) == EINVAL);
    raw.reserved = 0; raw.selector = 1;
    CHECK(raw_window((uintptr_t)&raw, (uintptr_t)&state, 0) == EINVAL);
    raw.selector = 0; alias.request = raw;
    CHECK(raw_window((uintptr_t)&alias, (uintptr_t)&alias, 0) == E_OK);
    CHECK(!alias.result.handle && !alias.result.owner);
    memory = Malloc(4096);
    CHECK(memory > 0);
    edge = (UBYTE *)(uintptr_t)(ULONG)memory;
    CHECK(aes_wind_create(kind, (const GRECT *)(edge + 4092)) == EIMBA);
    CHECK(aes_wind_get(0, (AES_WINDOW_INFO *)(edge + 4092)) == EIMBA);
    CHECK(Mfree(edge) == E_OK);
    CHECK(aes_wind_calc(AES_WINDOW_WORK, kind, &rectangle, &work) == E_OK);
    CHECK(aes_wind_calc(AES_WINDOW_BORDER, kind, &work, &calculated) == E_OK);
    CHECK(rc_equal(&rectangle, &calculated));
    CHECK(aes_wind_calc(2, kind, &rectangle, &work) == EINVAL);
    calculated = rectangle; calculated.g_x = 32760;
    CHECK(aes_wind_create(kind, &calculated) == EINVAL);
    calculated = rectangle; calculated.g_x = -2; calculated.g_w = 32767;
    CHECK(aes_wind_create(kind, &calculated) == EINVAL);
    calculated = work; calculated.g_x = calculated.g_y = 0;
    calculated.g_w = 32766 - (rectangle.g_w - work.g_w);
    CHECK(aes_wind_calc(AES_WINDOW_BORDER, kind, &calculated, &visible) == EINVAL);
    calculated = rectangle; calculated.g_w = 1;
    CHECK(aes_wind_create(kind, &calculated) == EINVAL);
    CHECK(rectangle_pool(&pool_peer) == 0);
    peer = pool_peer;
    handle = aes_wind_create(kind, &rectangle);
    CHECK(handle > 0 && handle != peer);
    CHECK(aes_wind_get((WORD)handle, &state) == E_OK);
    CHECK(state.owner == info->id && state.kind == kind && !state.flags && !state.reserved);
    CHECK(rc_equal(&rectangle, &state.full));
    CHECK(!state.work.g_w && !state.work.g_h);
    CHECK(aes_wind_rect((WORD)handle, 1, &visible) == E_OK && !visible.g_w && !visible.g_h);
    for (count = 0; count < 32; count++) {
        extra = aes_wind_create(kind, &rectangle);
        if (extra == ENSMEM)
            break;
        CHECK(extra > 0);
        exhausted[count] = extra;
    }
    CHECK(count > 0 && count < 32);
    for (i = 0; i < count; i++)
        CHECK(aes_wind_delete(exhausted[i]) == E_OK);
    CHECK(cleanup_synchronization(&rectangle) == 0);
    CHECK(boundary_resizes() == 0);
    CHECK(redraw_bounds(info->id) == 0);
    CHECK(mover_boundaries() == 0);
    CHECK(slider_gadgets() == 0);
    CHECK(cursor_copyout(vdi_handle) == 0);
    CHECK(copied_text((WORD)handle) == 0);
    memset(too_long, 'X', sizeof(too_long));
    CHECK(aes_wind_set_text((WORD)handle, AES_WINDOW_NAME, too_long) == EINVAL);
    too_long[AES_WINDOW_TEXT_MAX] = 0;
    CHECK(aes_wind_set_text((WORD)handle, AES_WINDOW_NAME, too_long) == E_OK);
    CHECK(aes_wind_set_text((WORD)handle, AES_WINDOW_NAME, "") == E_OK);
    CHECK(copied_text((WORD)handle) == 0);
    CHECK(aes_wind_set_text((WORD)handle, 2, "bad") == EINVAL);
    CHECK(aes_wind_set_text((WORD)handle, AES_WINDOW_NAME, (const char *)1) == EIMBA);
    CHECK(aes_wind_open((WORD)handle, &rectangle) == E_OK);
    CHECK(aes_wind_open((WORD)handle, &rectangle) == EINVAL);
    CHECK(aes_wind_get((WORD)handle, &state) == E_OK && state.flags == AES_WINDOW_OPEN);
    CHECK(rc_equal(&rectangle, &state.current) && rc_equal(&work, &state.work));
    CHECK(closed_geometry((WORD)handle, &rectangle) == 0);
    CHECK(deferred_arrows((WORD)handle, info->id) == 0);
    calculated = rectangle; calculated.g_x = -32000;
    CHECK(aes_wind_set_rect((WORD)handle, &calculated) == EINVAL);
    /* Two individually valid off-screen rectangles can have an unrepresentable
     * union. Include a closed window's old bounds in native move validation. */
    extra = aes_wind_create(0, &rectangle);
    CHECK(extra > 0);
    calculated = rectangle; calculated.g_x = -30000;
    CHECK(aes_wind_set_rect((WORD)extra, &calculated) == E_OK);
    calculated.g_x = 32000;
    CHECK(aes_wind_set_rect((WORD)handle, &calculated) == EINVAL);
    CHECK(aes_wind_get((WORD)handle, &state) == E_OK && rc_equal(&rectangle, &state.current));
    CHECK(aes_wind_delete((WORD)extra) == E_OK);
    CHECK(aes_wind_find(state.work.g_x + 2, state.work.g_y + 2) == handle);
    CHECK(aes_wind_slider((WORD)handle, AES_WINDOW_HSLIDE, 321) == E_OK);
    CHECK(aes_wind_slider((WORD)handle, AES_WINDOW_VSLIDE, 765) == E_OK);
    CHECK(aes_wind_slider((WORD)handle, AES_WINDOW_HSLSIZ, 400) == E_OK);
    CHECK(aes_wind_slider((WORD)handle, AES_WINDOW_VSLSIZ, -1) == E_OK);
    CHECK(aes_wind_slider((WORD)handle, 4, 500) == EINVAL);
    CHECK(aes_wind_slider((WORD)handle, AES_WINDOW_HSLIDE, -1) == EINVAL);
    CHECK(aes_wind_get((WORD)handle, &state) == E_OK);
    CHECK(state.hslide == 321 && state.vslide == 765 && state.hslsiz == 400 && state.vslsiz == -1);
    CHECK(aes_wind_top((WORD)handle) == E_OK);
    CHECK(aes_wind_rect((WORD)handle, 0, (GRECT *)&immutable_rect) == EIMBA);
    CHECK(aes_wind_rect((WORD)handle, 2, &visible) == EINVAL);
    CHECK(aes_wind_rect((WORD)handle, 0, &visible) == E_OK);
    count = 0;
    while (visible.g_w && visible.g_h) {
        CHECK(++count < 128);
        calculated = visible;
        CHECK(rc_intersect(&state.work, &calculated) && rc_equal(&calculated, &visible));
        CHECK(aes_wind_rect((WORD)handle, 1, &visible) == E_OK);
    }
    CHECK(count > 0);
    /* Native rectangle nodes are recycled by window changes. NEXT must
     * rescan safely rather than follow a retained pointer into that pool. */
    CHECK(aes_wind_rect((WORD)handle, 0, &visible) == E_OK && visible.g_w > 0);
    CHECK(aes_wind_set_rect((WORD)handle, &rectangle) == E_OK);
    CHECK(aes_wind_rect((WORD)handle, 1, &visible) == E_OK && visible.g_w > 0);
    while (aes_appl_read(&received) == E_OK)
        CHECK(received.type == 20);
    /* Full opaque-message queue: self redraws must coalesce, never suspend
     * the window syscall waiting for this same caller to drain its queue. */
    message.type = 630;
    for (i = 0; i < AES_MESSAGE_QUEUE; i++) {
        message.data[0] = i;
        CHECK(aes_appl_write(info->id, &message) == E_OK);
    }
    for (i = 0; i < 4; i++) {
        rectangle.g_x += 2;
        rectangle.g_w += 4;
        CHECK(aes_wind_set_rect((WORD)handle, &rectangle) == E_OK);
    }
    for (i = 0; i < AES_MESSAGE_QUEUE; i++) {
        CHECK(aes_appl_read(&received) == E_OK);
        CHECK(received.type == 630 && received.data[0] == i);
    }
    CHECK(aes_appl_read(&received) == E_OK);
    CHECK(received.type == 20 && !received.sender && received.data[0] == handle);
    CHECK(received.data[3] > 0 && received.data[4] > 0);
    while (aes_appl_read(&received) == E_OK)
        CHECK(received.type == 20);
    wait.mask = AES_EVENT_KEYBOARD | AES_EVENT_MESSAGE | AES_EVENT_TIMER;
    (void)Cconws("x32 window key: READY\r\n");
    do {
        CHECK(aes_evnt_multi(&wait, &input_event) > 0);
        CHECK(!(input_event.events & AES_EVENT_TIMER));
    } while (!(input_event.events & AES_EVENT_KEYBOARD));
    CHECK(input_event.key == 0x3062);
    wait.mask = AES_EVENT_TIMER; wait.timer_ms = 0;
    CHECK(aes_evnt_multi(&wait, &input_event) == AES_EVENT_TIMER);
    CHECK(aes_wind_get((WORD)handle, &state) == E_OK);
    snprintf(line, sizeof(line), "x32 window close: READY %d %d %d %d\r\n",
             rectangle.g_x + 8, rectangle.g_y + 8, input_event.mouse_x, input_event.mouse_y);
    (void)Cconws(line);
    wait.mask = AES_EVENT_MESSAGE | AES_EVENT_TIMER; wait.timer_ms = 3000;
    do {
        CHECK(aes_evnt_multi(&wait, &input_event) == AES_EVENT_MESSAGE);
    } while (input_event.message.type == 20);
    CHECK(input_event.message.type == 22 && !input_event.message.sender &&
          input_event.message.data[0] == handle);
    CHECK(aes_wind_get((WORD)handle, &state) == E_OK && state.flags == AES_WINDOW_OPEN);
    CHECK(aes_wind_close((WORD)handle) == E_OK);
    CHECK(aes_wind_close((WORD)handle) == EINVAL);
    CHECK(aes_wind_top((WORD)handle) == EINVAL);
    CHECK(aes_wind_set_text((WORD)handle, AES_WINDOW_NAME, "Recycle this window") == E_OK);
    CHECK(aes_wind_delete((WORD)handle) == E_OK);
    do {
        CHECK(aes_appl_read(&received) == E_OK);
    } while (received.type == 20);
    CHECK(received.type == 635 && received.data[0] == EAGAIN);
    CHECK(received.sender == aes_appl_find("ACCWAIT"));
    CHECK(aes_wind_get((WORD)handle, &state) == EIHNDL);
    CHECK(replacement_deletion(info, &rectangle) == 0);
    handle = aes_wind_create(kind, &rectangle);
    CHECK(handle > 0 && aes_wind_open((WORD)handle, &rectangle) == E_OK);
    extra = aes_wind_create(kind, &rectangle);
    CHECK(extra > 0 && aes_wind_set_rect((WORD)extra, &rectangle) == E_OK);
    CHECK(aes_appl_exit() == E_OK);
    CHECK(aes_appl_init("ACCTWO", &renewed) == E_OK && renewed.id == info->id);
    CHECK(aes_wind_get((WORD)handle, &state) == EIHNDL);
    CHECK(aes_wind_get((WORD)extra, &state) == EIHNDL);
    handle = aes_wind_create(kind, &rectangle);
    CHECK(handle > 0 && aes_wind_open((WORD)handle, &rectangle) == E_OK);
    extra = aes_wind_create(kind, &rectangle);
    CHECK(extra > 0 && aes_wind_set_rect((WORD)extra, &rectangle) == E_OK);
    CHECK(aes_wind_set_text((WORD)handle, AES_WINDOW_NAME, "Revoke this window") == E_OK);
    control.code = 125; control.handle = vdi_handle;
    control.args.button.new_handler = revoke_window;
    CHECK(vdi(&pb) == E_OK);
    old_button = control.args.button.old_handler;
    memset(&state, 0x7d, sizeof(state));
    revoked = reregistered = EINVAL;
    CHECK(aes_wind_get((WORD)handle, &state) == EACCDN);
    CHECK(revoked == E_OK && reregistered == E_OK && callback_info.id == info->id);
    CHECK(callback_info.features & AES_FEATURE_WINDOW);
    CHECK(state.handle == 0x7d7d && state.reserved == 0x7d7d);
    control.args.button.new_handler = old_button;
    CHECK(vdi(&pb) == E_OK);
    CHECK(aes_wind_get((WORD)handle, &state) == EIHNDL);
    CHECK(aes_wind_get((WORD)extra, &state) == EIHNDL);
    CHECK(aes_wind_get((WORD)peer, &state) == EACCDN);
    CHECK(borrowed_cleanup(info, vdi_handle, &rectangle) == 0);
    CHECK(cursor_return(info, vdi_handle) == 0);
    /* Leave a live, text-bearing window for desktop restart cleanup. */
    handle = aes_wind_create(kind, &rectangle);
    CHECK(handle > 0 && copied_text((WORD)handle) == 0);
    CHECK(aes_wind_open((WORD)handle, &rectangle) == E_OK);
    (void)Cconws("x32 window events/ownership/lifecycle: PASS\r\n");
    return 0;
}
