/* Loaded ring-3 resource, menu, dialog and USERDEF consumers. */
#include <mint/mintbind.h>
#include <string.h>
#include <stdio.h>
#include "aes_objects.h"
#include "gemerror.h"
#include "vdipb.h"

int accessory_object_checks(WORD vdi_handle);
int accessory_object_fault(WORD vdi_handle);

typedef struct {
    AES_TREE_HEADER header;
    AES_OBJECT objects[4];
    AES_OBJECT_TEXT text;
    AES_OBJECT_STRING button;
    AES_OBJECT_USERDEF user;
    char value[8], pattern[8], valid[2], label[8];
} RESOURCE;
typedef struct {
    AES_TREE_HEADER header;
    AES_OBJECT objects[16];
    AES_OBJECT_STRING strings[16];
    char text[16][16];
} MENU_RESOURCE;
typedef struct {
    AES_TREE_HEADER header;
    AES_OBJECT objects[3];
    AES_OBJECT_TEXT ted;
    AES_OBJECT_STRING string;
    char text[81], pattern[81], valid[82], label[129];
} LIMIT_RESOURCE;
typedef struct {
    AES_TREE_HEADER header;
    AES_OBJECT object;
    AES_OBJECT_CICON icon;
    AES_OBJECT_COLOUR colour;
    AES_OBJECT_STRING label;
    char text[24];
    WORD mono[16], mask[16], data[64], selected[64];
} ICON_RESOURCE;
typedef struct {
    AES_TREE_HEADER header;
    AES_OBJECT objects[4];
    AES_OBJECT_STRING strings[3];
    char text[3][8];
} RADIO_RESOURCE;

static ULONG expected_tree;
static volatile WORD callbacks, callback_bad, revoke, fault;
static volatile LONG unregistered;
static volatile WORD renew;
static volatile LONG renewed, update_lock, mouse_lock;
static AES_APP_INFO renewed_info;
static WORD drawing_handle;

#define CHECK(c) do { if (!(c)) { (void)Cconws("accessory object FAIL: " #c "\r\n"); return __LINE__; } } while (0)

static LONG command(ULONG tree, WORD operation, WORD object, WORD value,
                      WORD extra, ULONG data, ULONG bytes, AES_OBJECT_RESULT *out)
{
    AES_OBJECT_REQUEST r = { tree, operation, object, value, extra, { 0 }, data, bytes };
    LONG rc;
    do {
        rc = aes_object(&r, out);
        if (rc == EAGAIN && aes_appl_yield())
            return EACCDN;
    } while (rc == EAGAIN);
    return rc;
}

static WORD draw(const AES_OBJECT_DRAW *p)
{
    UWORD cs;
    VDICONTROL control = { 0 };
    WORD points[4];
    VDIPB pb = { &control, NULL, points, NULL, NULL };

    __asm__ volatile ("mov %%cs,%0" : "=r"(cs));
    if ((cs & 3) != 3 || p->tree != expected_tree || p->object != 3 ||
        p->parameter != 0x659548 || p->reserved || p->clip.g_w <= 0 || p->clip.g_h <= 0)
        callback_bad = 1;
    callbacks++;
    if (fault)
        __asm__ volatile ("ud2");
    if (revoke)
        unregistered = aes_appl_exit();
    else {
        control.code = 6; control.nptsin = 2; control.handle = drawing_handle;
        points[0] = p->rectangle.g_x; points[1] = p->rectangle.g_y;
        points[2] = points[0] + 8; points[3] = points[1];
        if (vdi(&pb))
            callback_bad = 1;
    }
    return p->current;
}

static void resource(RESOURCE *r)
{
    static const RESOURCE initial = {
        { AES_TREE_VERSION, sizeof(RESOURCE), 4, 0, 0 },
        {
            { -1, 1, 3, 20, 0, 0, 0x1100, { 30, 30, 240, 140 } },
            { 2, -1, -1, 29, 8, 0, offsetof(RESOURCE, text), { 20, 20, 160, 16 } },
            { 3, -1, -1, 26, 7, 0, offsetof(RESOURCE, button), { 60, 65, 80, 20 } },
            { 0, -1, -1, 24, 0, 0, offsetof(RESOURCE, user), { 20, 110, 80, 16 } }
        },
        { offsetof(RESOURCE, value), offsetof(RESOURCE, pattern), offsetof(RESOURCE, valid),
          2, 3, 0, 0x1100, 0, 8, 8 },
        { offsetof(RESOURCE, label), 8 },
        { 0, 0x659548 }, "", "_______", "X", "OK"
    };
    *r = initial;
    r->user.handler = (uintptr_t)draw;
}

static void menu_resource(MENU_RESOURCE *r)
{
    WORD i;
    memset(r, 0, sizeof(*r));
    r->header.version = AES_TREE_VERSION;
    r->header.bytes = sizeof(*r); r->header.count = 16;
    for (i = 0; i < 16; i++) {
        r->objects[i].next = r->objects[i].head = r->objects[i].tail = -1;
        r->objects[i].type = 25;
        r->objects[i].flags = 128;
        r->strings[i].text = offsetof(MENU_RESOURCE, text) + i * 16;
        r->strings[i].capacity = 16;
    }
    for (i = 0; i < 14; i++)
        r->objects[i].flags = 0;
    r->objects[0].head = 1; r->objects[0].tail = 4;
    r->objects[0].rectangle.g_w = 1280; r->objects[0].rectangle.g_h = 720;
    r->objects[1].next = 4; r->objects[1].head = r->objects[1].tail = 2;
    r->objects[1].type = 20; r->objects[1].spec = 0x1100;
    r->objects[1].rectangle.g_w = 1280; r->objects[1].rectangle.g_h = 20;
    r->objects[2].next = 1; r->objects[2].head = r->objects[2].tail = 3;
    r->objects[2].rectangle.g_w = 1280; r->objects[2].rectangle.g_h = 20;
    r->objects[3].next = 2; r->objects[3].type = 32;
    r->objects[3].spec = offsetof(MENU_RESOURCE, strings) + 3 * sizeof(AES_OBJECT_STRING);
    r->objects[3].rectangle.g_x = 8; r->objects[3].rectangle.g_w = 80; r->objects[3].rectangle.g_h = 20;
    strcpy(r->text[3], " Desk ");
    r->objects[4].next = 0; r->objects[4].head = r->objects[4].tail = 5;
    r->objects[4].rectangle.g_y = 20;
    r->objects[4].rectangle.g_w = 1280; r->objects[4].rectangle.g_h = 200;
    r->objects[5].next = 4; r->objects[5].head = 6; r->objects[5].tail = 13;
    r->objects[5].type = 20; r->objects[5].spec = 0x1100;
    r->objects[5].rectangle.g_x = 8; r->objects[5].rectangle.g_w = 160; r->objects[5].rectangle.g_h = 144;
    for (i = 6; i < 14; i++) {
        r->objects[i].type = 28;
        r->objects[i].next = i == 13 ? 5 : i + 1;
        r->objects[i].spec = offsetof(MENU_RESOURCE, strings) + i * sizeof(AES_OBJECT_STRING);
        r->objects[i].rectangle.g_y = (i - 6) * 16;
        r->objects[i].rectangle.g_w = 160; r->objects[i].rectangle.g_h = 16;
        strcpy(r->text[i], i == 6 ? " About..." : " ------------");
    }
    /* Native menu installation discards this extra original child. Its
     * stale sibling chain must never be accepted by later native calls. */
    r->objects[5].tail = 14;
    r->objects[13].next = 14; r->objects[14].next = 5;
}

static LONG draw_tree(ULONG tree)
{
    AES_OBJECT_REQUEST r = { tree, AES_OBJECT_DRAW_TREE, 0, AES_TREE_MAX_DEPTH };
    return aes_object(&r, NULL);
}

static int text_limit_checks(void)
{
    LIMIT_RESOURCE r = {
        { AES_TREE_VERSION, sizeof(LIMIT_RESOURCE), 3, 0, 0 },
        {
            { -1, 1, 2, 25, 0, 0, 0, { 30, 30, 1100, 64 } },
            { 2, -1, -1, 29, 8, 0, offsetof(LIMIT_RESOURCE, ted), { 0, 0, 640, 16 } },
            { 0, -1, -1, 28, 0, 0, offsetof(LIMIT_RESOURCE, string), { 0, 30, 1100, 16 } }
        },
        { offsetof(LIMIT_RESOURCE, text), offsetof(LIMIT_RESOURCE, pattern), offsetof(LIMIT_RESOURCE, valid),
          81, 3, 0, 0x1100, 0, 80, 80 },
        { offsetof(LIMIT_RESOURCE, label), 128 }, { 0 }, { 0 }, { 0 }, { 0 }
    };
    AES_OBJECT_RESULT out;
    ULONG tree;
    WORD i;

    for (i = 0; i < 80; i++) {
        r.text[i] = i < 79 ? 'a' : 0;
        r.pattern[i] = i < 79 ? '_' : 0;
        r.valid[i] = 'X';
    }
    for (i = 0; i < 127; i++) r.label[i] = 'b';
    CHECK(command(0, AES_OBJECT_CREATE, 0, 0, 0, (uintptr_t)&r, sizeof(r), &out) == E_OK);
    tree = out.value;
    CHECK(draw_tree(tree) == E_OK);
    CHECK(command(tree, AES_OBJECT_EDIT, 1, 0, 1, 0, 0, &out) == E_OK && out.index == 79);
    CHECK(command(tree, AES_OBJECT_EDIT, 1, 0x0e08, 2, 0, 79, &out) == E_OK && out.index == 78);
    CHECK(command(tree, AES_OBJECT_EDIT, 1, 'a', 2, 0, 78, &out) == E_OK && out.index == 79);
    CHECK(command(tree, AES_OBJECT_EDIT, 1, 0, 3, 0, 79, &out) == E_OK);
    CHECK(command(tree, AES_OBJECT_SET_TEXT, 1, 0, 0, (uintptr_t)r.text, 81, &out) == EINVAL);
    CHECK(command(tree, AES_OBJECT_SET_TEXT, 1, 0, 0, (uintptr_t)r.text, 80, &out) == E_OK);
    CHECK(command(tree, AES_OBJECT_SET_TEXT, 2, 0, 0, (uintptr_t)r.label, 129, &out) == EINVAL);
    CHECK(command(tree, AES_OBJECT_SET_TEXT, 2, 0, 0, (uintptr_t)r.label, 128, &out) == E_OK);
    CHECK(draw_tree(tree) == E_OK);
    CHECK(command(tree, AES_OBJECT_DESTROY, 0, 0, 0, 0, 0, &out) == E_OK);
    r.ted.template_capacity = 81;
    CHECK(command(0, AES_OBJECT_CREATE, 0, 0, 0, (uintptr_t)&r, sizeof(r), &out) == EINVAL);
    r.objects[1].flags = 0;
    r.ted.text_capacity = 81;
    r.text[79] = 'a'; r.pattern[79] = '_';
    CHECK(command(0, AES_OBJECT_CREATE, 0, 0, 0, (uintptr_t)&r, sizeof(r), &out) == E_OK);
    tree = out.value;
    CHECK(draw_tree(tree) == E_OK);
    CHECK(command(tree, AES_OBJECT_DESTROY, 0, 0, 0, 0, 0, &out) == E_OK);
    r.ted.text_capacity = 82;
    CHECK(command(0, AES_OBJECT_CREATE, 0, 0, 0, (uintptr_t)&r, sizeof(r), &out) == EINVAL);
    (void)Cconws("x32 object native text limits: PASS\r\n");
    return 0;
}

static int colour_checks(void)
{
    ICON_RESOURCE r = { 0 }, snapshot;
    AES_OBJECT_RESULT out;
    ULONG tree;
    WORD planes, i, p;

    r.header.version = AES_TREE_VERSION; r.header.count = 1;
    r.object.next = r.object.head = r.object.tail = -1;
    r.object.type = 33; r.object.spec = offsetof(ICON_RESOURCE, icon);
    r.object.rectangle.g_x = 760; r.object.rectangle.g_y = 420;
    r.object.rectangle.g_w = r.object.rectangle.g_h = 16;
    r.icon.mono.mask = offsetof(ICON_RESOURCE, mask); r.icon.mono.data = offsetof(ICON_RESOURCE, mono);
    r.icon.mono.text = offsetof(ICON_RESOURCE, label); r.icon.mono.character = 0x1000;
    r.icon.mono.icon.g_w = r.icon.mono.icon.g_h = 16;
    r.icon.mono.label.g_y = 20; r.icon.mono.label.g_w = 120; r.icon.mono.label.g_h = 8;
    r.icon.colours = offsetof(ICON_RESOURCE, colour);
    r.colour.data = offsetof(ICON_RESOURCE, data); r.colour.mask = offsetof(ICON_RESOURCE, mask);
    r.label.text = offsetof(ICON_RESOURCE, text); r.label.capacity = sizeof(r.text);
    strcpy(r.text, "X32 colour regression");
    for (planes = 1; planes <= 4; planes += 3) {
        r.colour.planes = planes;
        r.header.bytes = planes == 1 ? offsetof(ICON_RESOURCE, data) + 32 : sizeof(r);
        for (i = 0; i < 16; i++) { r.mono[i] = 0xaaaa; r.mask[i] = -1; }
        for (p = 0; p < planes; p++)
            for (i = 0; i < 16; i++) {
                r.data[p * 16 + i] = p == 0 ? 0xaaaa : p == 1 ? 0xcccc : p == 2 ? 0xf0f0 : 0xff00;
                r.selected[p * 16 + i] = ~r.data[p * 16 + i];
            }
        if (planes == 4) {
            r.colour.selected_data = offsetof(ICON_RESOURCE, selected);
            r.colour.selected_mask = offsetof(ICON_RESOURCE, mask);
        }
        CHECK(command(0, AES_OBJECT_CREATE, 0, 0, 0, (uintptr_t)&r, r.header.bytes, &out) == E_OK);
        tree = out.value;
        CHECK(command(tree, AES_OBJECT_GET, 0, 0, 0, 0, 0, &out) == E_OK && out.object.type == r.object.type);
        CHECK(draw_tree(tree) == E_OK);
        CHECK(command(tree, AES_OBJECT_READ, 0, 0, 0, (uintptr_t)&snapshot, r.header.bytes, &out) == E_OK);
        CHECK(snapshot.object.type == r.object.type);
        CHECK(snapshot.colour.data == r.colour.data && snapshot.data[0] == r.data[0]);
        if (planes == 4) {
            CHECK(command(tree, AES_OBJECT_STATE, 0, 1, 0, 0, 0, &out) == E_OK);
            CHECK(draw_tree(tree) == E_OK);
        }
        CHECK(command(tree, AES_OBJECT_DESTROY, 0, 0, 0, 0, 0, &out) == E_OK);
    }
    return 0;
}

static int menu_validation_checks(void)
{
    MENU_RESOURCE r;
    AES_OBJECT_RESULT out;
    ULONG tree;
    WORD parents[16];

    menu_resource(&r);
    r.objects[5].tail = 13; r.objects[13].next = 5;
    r.objects[14] = r.objects[3];
    r.objects[3].type = 25; r.objects[3].spec = 0; r.objects[3].next = -1;
    r.objects[2].head = r.objects[2].tail = 14;
    CHECK(aes_tree_validate(&r, sizeof(r), parents) == E_OK);
    CHECK(command(0, AES_OBJECT_CREATE, 0, 0, 0, (uintptr_t)&r, sizeof(r), &out) == E_OK);
    tree = out.value;
    CHECK(command(tree, AES_OBJECT_MENU, 0, 1, 0, 0, 0, &out) == EINVAL);
    CHECK(command(tree, AES_OBJECT_DESTROY, 0, 0, 0, 0, 0, &out) == E_OK);
    menu_resource(&r);
    r.objects[5].tail = 13; r.objects[13].next = 5;
    r.objects[14] = r.objects[5]; r.objects[14].head = r.objects[14].tail = -1;
    r.objects[14].next = 4; r.objects[14].rectangle.g_x = 200;
    r.objects[5].next = 14; r.objects[4].tail = 14;
    r.objects[15] = r.objects[3]; r.objects[15].rectangle.g_x = 100;
    r.objects[3].next = 15; r.objects[2].tail = 15;
    CHECK(aes_tree_validate(&r, sizeof(r), parents) == E_OK);
    CHECK(command(0, AES_OBJECT_CREATE, 0, 0, 0, (uintptr_t)&r, sizeof(r), &out) == E_OK);
    tree = out.value;
    CHECK(command(tree, AES_OBJECT_MENU, 0, 1, 0, 0, 0, &out) == EINVAL);
    CHECK(command(tree, AES_OBJECT_DESTROY, 0, 0, 0, 0, 0, &out) == E_OK);
    (void)Cconws("x32 object consecutive menu titles: PASS\r\n");
    return 0;
}

static int radio_checks(void)
{
    RADIO_RESOURCE r = {
        { AES_TREE_VERSION, sizeof(RADIO_RESOURCE), 4, 0, 0 },
        {
            { -1, 1, 3, 25, 0, 0, 0, { 100, 100, 240, 140 } },
            { 2, -1, -1, 26, 17, 1, offsetof(RADIO_RESOURCE, strings), { 20, 20, 80, 20 } },
            { 3, -1, -1, 26, 17, 0, offsetof(RADIO_RESOURCE, strings) + sizeof(AES_OBJECT_STRING), { 120, 20, 80, 20 } },
            { 0, -1, -1, 26, 7, 0, offsetof(RADIO_RESOURCE, strings) + 2 * sizeof(AES_OBJECT_STRING), { 60, 80, 80, 20 } }
        },
        { { offsetof(RADIO_RESOURCE, text), 8 }, { offsetof(RADIO_RESOURCE, text) + 8, 8 },
          { offsetof(RADIO_RESOURCE, text) + 16, 8 } },
        { "First", "Second", "OK" }
    }, snapshot;
    AES_OBJECT_RESULT out;
    ULONG tree;
    char ready[100];

    CHECK(command(0, AES_OBJECT_CREATE, 0, 0, 0, (uintptr_t)&r, sizeof(r), &out) == E_OK);
    tree = out.value;
    CHECK(draw_tree(tree) == E_OK);
    CHECK(command(0, AES_OBJECT_MOUSE_STATE, 0, 0, 0, 0, 0, &out) == E_OK);
    sprintf(ready, "x32 object radio: READY 260 130 %d %d\r\n", out.rectangle.g_x, out.rectangle.g_y);
    (void)Cconws(ready);
    CHECK(aes_form_do(tree, 0) == 3);
    CHECK(command(tree, AES_OBJECT_READ, 0, 0, 0, (uintptr_t)&snapshot, sizeof(snapshot), &out) == E_OK);
    CHECK(!(snapshot.objects[1].state & 1) && (snapshot.objects[2].state & 1));
    CHECK(command(tree, AES_OBJECT_DESTROY, 0, 0, 0, 0, 0, &out) == E_OK);
    (void)Cconws("x32 object radio drag selection: PASS\r\n");
    return 0;
}

int accessory_object_fault(WORD vdi_handle)
{
    RESOURCE r;
    AES_OBJECT_RESULT out;
    AES_EVENT event;

    drawing_handle = vdi_handle;
    resource(&r);
    r.objects[0].rectangle.g_x = 760; r.objects[0].rectangle.g_y = 420;
    CHECK(command(0, AES_OBJECT_CREATE, 0, 0, 0, (uintptr_t)&r, sizeof(r), &out) == E_OK);
    expected_tree = out.value;
    do {
        CHECK(aes_evnt_wait(AES_EVENT_MESSAGE, &event) == AES_EVENT_MESSAGE);
    } while (event.message.type == 20);
    CHECK(event.message.type == 665);
    if (event.message.data[0])
        CHECK(command(expected_tree, AES_OBJECT_DESKTOP, 0, 1, 0, 0, 0, &out) == E_OK);
    fault = 1;
    (void)Cconws("x32 object USERDEF fault: ARMED\r\n");
    if (event.message.data[0]) {
        (void)Cconws("x32 object foreign USERDEF fault: ARMED\r\n");
        for (;;)
            CHECK(aes_evnt_wait(AES_EVENT_MESSAGE, &event) == AES_EVENT_MESSAGE);
    } else {
        (void)Cconws("x32 object owner USERDEF fault: ARMED\r\n");
        (void)draw_tree(expected_tree);
    }
    CHECK(FALSE);                        /* only this owner must terminate */
    return 99;
}

static void cancel_menu(WORD buttons)
{
    if (buttons & 1) {
        unregistered = aes_appl_exit();
        if (renew) {
            renewed = aes_appl_init("ACCTWO", &renewed_info);
            update_lock = aes_wind_update(AES_BEG_UPDATE);
            mouse_lock = aes_wind_update(AES_BEG_MCTRL);
        }
    }
}

static void scope_callback(WORD event)
{
    if (event == 666 || event == 667) {
        unregistered = aes_appl_exit();
        renewed = aes_appl_init("ACCTWO", &renewed_info);
        update_lock = aes_wind_update(AES_BEG_UPDATE);
        mouse_lock = aes_wind_update(AES_BEG_MCTRL);
    }
}

static int scope_checks(WORD vdi_handle)
{
    RESOURCE r;
    AES_OBJECT_RESULT out;
    ULONG tree;
    WORD i;
    VDICONTROL control = { 0 };
    VDIPB pb = { &control, NULL, NULL, NULL, NULL };
    VDI_BUTTON_CALLBACK *old_button;

    control.code = 125; control.handle = vdi_handle;
    control.args.button.new_handler = scope_callback;
    CHECK(vdi(&pb) == E_OK);
    old_button = control.args.button.old_handler;
    for (i = 0; i < 2; i++) {
        resource(&r);
        strcpy(r.label, i ? "Scope B" : "Scope A");
        CHECK(command(0, AES_OBJECT_CREATE, 0, 0, 0, (uintptr_t)&r, sizeof(r), &out) == E_OK);
        tree = out.value;
        CHECK(command(tree, AES_OBJECT_UPDATE, 0, AES_BEG_UPDATE, 0, 0, 0, &out) == E_OK);
        CHECK(command(tree, AES_OBJECT_UPDATE, 0, AES_BEG_MCTRL, 0, 0, 0, &out) == E_OK);
        unregistered = renewed = update_lock = mouse_lock = EINVAL;
        if (!i) {
            CHECK(command(tree, AES_OBJECT_GET, 0, 0, 0, 0, 0, &out) == E_OK);
            CHECK(command(tree, AES_OBJECT_UPDATE, 0, AES_END_MCTRL, 0, 0, 0, &out) == EIHNDL);
        } else
            CHECK(command(tree, AES_OBJECT_UPDATE, 0, AES_END_MCTRL, 0, 0, 0, &out) == E_OK);
        CHECK(command(tree, AES_OBJECT_UPDATE, 0, AES_END_UPDATE, 0, 0, 0, &out) == EIHNDL);
        CHECK(unregistered == E_OK && renewed == E_OK && update_lock == E_OK && mouse_lock == E_OK);
        CHECK(aes_wind_update(AES_END_MCTRL) == E_OK);
        CHECK(aes_wind_update(AES_END_UPDATE) == E_OK);
        CHECK(aes_wind_update(AES_END_MCTRL) == EACCDN);
        CHECK(aes_wind_update(AES_END_UPDATE) == EACCDN);
    }
    control.args.button.new_handler = old_button;
    CHECK(vdi(&pb) == E_OK);
    (void)Cconws("x32 object scoped lock return races: PASS\r\n");
    return 0;
}

int accessory_object_checks(WORD vdi_handle)
{
    RESOURCE r, snapshot;
    MENU_RESOURCE menu;
    AES_OBJECT_REQUEST request = { 0 };
    AES_OBJECT_RESULT out;
    static const AES_OBJECT_RESULT readonly = { 0 };
    AES_APP_INFO info;
    AES_MESSAGE message = { 663, 0, 0, { 0 } };
    AES_EVENT event;
    ULONG tree, menu_tree;
    LONG peer;
    WORD parents[16];
    char ready[100];
    VDICONTROL control = { 0 };
    VDIPB pb = { &control, NULL, NULL, NULL, NULL };
    VDI_BUTTON_CALLBACK *old_button;

    drawing_handle = vdi_handle;
    CHECK(scope_checks(vdi_handle) == 0);
    CHECK(text_limit_checks() == 0);
    CHECK(colour_checks() == 0);
    CHECK(menu_validation_checks() == 0);
    CHECK(radio_checks() == 0);
    resource(&r);
    CHECK(aes_tree_validate(&r, sizeof(r), parents) == E_OK);
    r.objects[1].next = 1;
    CHECK(command(0, AES_OBJECT_CREATE, 0, 0, 0, (uintptr_t)&r, sizeof(r), &out) == EINVAL);
    resource(&r);
    CHECK(command(0, AES_OBJECT_CREATE, 0, 0, 0, 0, sizeof(r), &out) == EIMBA);
    r.objects[0].flags = 128;             /* a hidden ancestor hides its fields */
    CHECK(command(0, AES_OBJECT_CREATE, 0, 0, 0, (uintptr_t)&r, sizeof(r), &out) == E_OK);
    tree = out.value;
    CHECK(command(tree, AES_OBJECT_GET, 1, 0, 0, 0, 0, &out) == E_OK && !out.key && out.index == 0);
    CHECK(command(tree, AES_OBJECT_KEY, 1, 0x1c0d, 0, 0, 0, &out) == E_OK && out.value == 1);
    CHECK(aes_form_do(tree, 1) == EINVAL);
    CHECK(command(tree, AES_OBJECT_DESTROY, 0, 0, 0, 0, 0, &out) == E_OK);
    resource(&r);
    CHECK(command(0, AES_OBJECT_CREATE, 0, 0, 0, (uintptr_t)&r, sizeof(r), &out) == E_OK);
    tree = expected_tree = out.value;
    CHECK(tree > 0);
    r.objects[0].rectangle.g_x = 9000; r.label[0] = 'X';
    CHECK(command(tree, AES_OBJECT_GET, 0, 0, 0, 0, 0, &out) == E_OK && out.object.rectangle.g_x == 30);
    CHECK(command(tree, AES_OBJECT_GET, 3, 0, 0, 0, 0, &out) == E_OK && out.object.spec == offsetof(RESOURCE, user));
    request.tree = tree; request.operation = AES_OBJECT_GET;
    CHECK(aes_object(&request, (AES_OBJECT_RESULT *)&readonly) == EIMBA);
    request.data = 1;
    CHECK(aes_object(&request, &out) == EINVAL);
    request.data = 0;
    request.operation = AES_OBJECT_RECTANGLE;
    request.rectangle.g_x = 32767; request.rectangle.g_w = request.rectangle.g_h = 1;
    CHECK(aes_object(&request, &out) == EINVAL);
    request.object = 1;
    request.rectangle.g_x = -20; request.rectangle.g_y = 20;
    request.rectangle.g_w = 10; request.rectangle.g_h = 16;
    CHECK(aes_object(&request, &out) == E_OK);
    CHECK(command(tree, AES_OBJECT_OFFSET, 1, 0, 0, 0, 0, &out) == E_OK && out.rectangle.g_x == 10);
    request.rectangle.g_x = 20; request.rectangle.g_w = 160;
    CHECK(aes_object(&request, &out) == E_OK);
    request.object = 0;
    request.rectangle.g_x = -500; request.rectangle.g_y = 30;
    request.rectangle.g_w = 240; request.rectangle.g_h = 140;
    CHECK(aes_object(&request, &out) == E_OK);
    CHECK(command(tree, AES_OBJECT_GET, 0, 0, 0, 0, 0, &out) == E_OK && out.object.rectangle.g_x == -500);
    request.rectangle.g_x = 30;
    CHECK(aes_object(&request, &out) == E_OK);
    (void)Cconws("x32 object relative geometry: PASS\r\n");
    CHECK(draw_tree(tree) == E_OK && callbacks > 0 && !callback_bad);
    CHECK(command(tree, AES_OBJECT_DESKTOP, 0, 1, 0, 0, 0, &out) == E_OK);
    CHECK(command(tree, AES_OBJECT_DESKTOP, 0, 0, 0, 0, 0, &out) == E_OK);
    CHECK(command(tree, AES_OBJECT_CENTER, 0, 0, 0, 0, 0, &out) == E_OK);
    CHECK(out.rectangle.g_w > 0 && out.rectangle.g_h > 0);
    CHECK(draw_tree(tree) == E_OK);
    (void)Cconws("x32 object dialog: READY\r\n");
    CHECK(aes_form_do(tree, 1) == 2);
    CHECK(command(tree, AES_OBJECT_READ, 0, 0, 0, (uintptr_t)&snapshot, sizeof(snapshot), &out) == E_OK);
    CHECK(!strcmp(snapshot.value, "A") || !strcmp(snapshot.value, "a"));
    CHECK(snapshot.objects[2].state & 1);
    CHECK(snapshot.user.handler == (uintptr_t)draw && snapshot.user.parameter == 0x659548);
    peer = aes_appl_find("ACCWAIT");
    CHECK(peer > 0);
    message.data[0] = tree; message.data[1] = tree >> 16;
    CHECK(aes_appl_write(peer, &message) == E_OK);
    do {
        CHECK(aes_evnt_wait(AES_EVENT_MESSAGE, &event) == AES_EVENT_MESSAGE);
    } while (event.message.type != 664);
    CHECK(event.message.data[0] == EIHNDL);
    menu_resource(&menu);
    CHECK(aes_tree_validate(&menu, sizeof(menu), parents) == E_OK);
    CHECK(command(0, AES_OBJECT_CREATE, 0, 0, 0, (uintptr_t)&menu, sizeof(menu), &out) == E_OK);
    menu_tree = out.value;
    CHECK(command(menu_tree, AES_OBJECT_MENU, 0, 1, 0, 0, 0, &out) == E_OK);
    CHECK(command(menu_tree, AES_OBJECT_OFFSET, 14, 0, 0, 0, 0, &out) == EINVAL);
    CHECK(command(0, AES_OBJECT_MOUSE_STATE, 0, 0, 0, 0, 0, &out) == E_OK);
    sprintf(ready, "x32 object menu: READY 20 8 %d %d\r\n", out.rectangle.g_x, out.rectangle.g_y);
    (void)Cconws(ready);
    do {
        CHECK(aes_evnt_wait(AES_EVENT_MESSAGE, &event) == AES_EVENT_MESSAGE);
    } while (event.message.type != 10);
    CHECK(event.message.sender == 0 && event.message.data[0] == 3 && event.message.data[1] == 6);
    CHECK(((ULONG)(UWORD)event.message.data[2] | (ULONG)(UWORD)event.message.data[3] << 16) == menu_tree);
    CHECK(command(menu_tree, AES_OBJECT_STATE, 3, 0, 1, 0, 0, &out) == E_OK);
    control.code = 125; control.handle = vdi_handle;
    control.args.button.new_handler = cancel_menu;
    CHECK(vdi(&pb) == E_OK);
    old_button = control.args.button.old_handler;
    CHECK(command(0, AES_OBJECT_MOUSE_STATE, 0, 0, 0, 0, 0, &out) == E_OK);
    sprintf(ready, "x32 object cancel: READY 20 8 %d %d\r\n", out.rectangle.g_x, out.rectangle.g_y);
    (void)Cconws(ready);
    do {
        peer = aes_evnt_wait(AES_EVENT_MESSAGE, &event);
    } while (peer == AES_EVENT_MESSAGE);
    CHECK(peer == EACCDN && unregistered == E_OK);
    control.args.button.new_handler = old_button;
    CHECK(vdi(&pb) == E_OK);
    CHECK(aes_appl_init("ACCTWO", &info) == E_OK);
    CHECK(command(tree, AES_OBJECT_GET, 0, 0, 0, 0, 0, &out) == EIHNDL);
    CHECK(command(menu_tree, AES_OBJECT_GET, 0, 0, 0, 0, 0, &out) == EIHNDL);
    (void)Cconws("x32 object active menu cancellation: PASS\r\n");
    resource(&r);
    CHECK(command(0, AES_OBJECT_CREATE, 0, 0, 0, (uintptr_t)&r, sizeof(r), &out) == E_OK);
    tree = expected_tree = out.value;
    CHECK(command(0, AES_OBJECT_CREATE, 0, 0, 0, (uintptr_t)&menu, sizeof(menu), &out) == E_OK);
    menu_tree = out.value;
    CHECK(command(menu_tree, AES_OBJECT_MENU, 0, 1, 0, 0, 0, &out) == E_OK);
    CHECK(draw_tree(tree) == E_OK);
    control.args.button.new_handler = cancel_menu;
    CHECK(vdi(&pb) == E_OK);
    old_button = control.args.button.old_handler;
    renew = 1;
    (void)Cconws("x32 object modal: READY\r\n");
    peer = aes_form_do(tree, 1);
    /* Cancellation during the wait returns EACCDN. A replacement on its
     * completed syscall return invalidates the following tree call. */
    CHECK(peer == EACCDN || peer == EIHNDL);
    renew = 0;
    CHECK(unregistered == E_OK && renewed == E_OK && update_lock == E_OK && mouse_lock == E_OK);
    control.args.button.new_handler = old_button;
    CHECK(vdi(&pb) == E_OK);
    CHECK(aes_wind_update(AES_END_MCTRL) == E_OK);
    CHECK(aes_wind_update(AES_END_UPDATE) == E_OK);
    CHECK(command(tree, AES_OBJECT_GET, 0, 0, 0, 0, 0, &out) == EIHNDL);
    CHECK(command(menu_tree, AES_OBJECT_GET, 0, 0, 0, 0, 0, &out) == EIHNDL);
    (void)Cconws("x32 object cancelled modal preserves new locks: PASS\r\n");
    resource(&r);
    CHECK(command(0, AES_OBJECT_CREATE, 0, 0, 0, (uintptr_t)&r, sizeof(r), &out) == E_OK);
    tree = expected_tree = out.value;
    CHECK(command(0, AES_OBJECT_CREATE, 0, 0, 0, (uintptr_t)&menu, sizeof(menu), &out) == E_OK);
    menu_tree = out.value;
    CHECK(command(menu_tree, AES_OBJECT_MENU, 0, 1, 0, 0, 0, &out) == E_OK);
    CHECK(command(tree, AES_OBJECT_DESKTOP, 0, 1, 0, 0, 0, &out) == E_OK);
    revoke = 1;
    CHECK(draw_tree(tree) == EACCDN && unregistered == E_OK && !callback_bad);
    revoke = 0;
    CHECK(aes_appl_init("ACCTWO", &info) == E_OK);
    CHECK(command(tree, AES_OBJECT_GET, 0, 0, 0, 0, 0, &out) == EIHNDL);
    CHECK(command(menu_tree, AES_OBJECT_GET, 0, 0, 0, 0, 0, &out) == EIHNDL);
    (void)Cconws("x32 objects snapshot/menu/dialog/USERDEF/revocation: PASS\r\n");
    menu_resource(&menu);
    menu.objects[3].state = 8;          /* DISABLED: never opens a popup */
    CHECK(command(0, AES_OBJECT_CREATE, 0, 0, 0, (uintptr_t)&menu, sizeof(menu), &out) == E_OK);
    menu_tree = out.value;
    CHECK(command(menu_tree, AES_OBJECT_MENU, 0, 1, 0, 0, 0, &out) == E_OK);
    control.args.button.new_handler = cancel_menu;
    CHECK(vdi(&pb) == E_OK);
    old_button = control.args.button.old_handler;
    unregistered = EINVAL;
    CHECK(command(0, AES_OBJECT_MOUSE_STATE, 0, 0, 0, 0, 0, &out) == E_OK);
    sprintf(ready, "x32 object disabled: READY 20 8 %d %d\r\n", out.rectangle.g_x, out.rectangle.g_y);
    (void)Cconws(ready);
    do {
        peer = aes_evnt_wait(AES_EVENT_MESSAGE, &event);
    } while (peer == AES_EVENT_MESSAGE);
    CHECK(peer == EACCDN && unregistered == E_OK);
    control.args.button.new_handler = old_button;
    CHECK(vdi(&pb) == E_OK);
    CHECK(aes_appl_init("ACCTWO", &info) == E_OK);
    CHECK(command(menu_tree, AES_OBJECT_GET, 0, 0, 0, 0, 0, &out) == EIHNDL);
    (void)Cconws("x32 object disabled title cancellation: PASS\r\n");
    return 0;
}
