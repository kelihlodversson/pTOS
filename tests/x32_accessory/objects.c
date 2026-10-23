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

static ULONG expected_tree;
static volatile WORD callbacks, callback_bad, revoke, fault;
static volatile LONG unregistered;
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
}

static LONG draw_tree(ULONG tree)
{
    AES_OBJECT_REQUEST r = { tree, AES_OBJECT_DRAW_TREE, 0, AES_TREE_MAX_DEPTH };
    return aes_object(&r, NULL);
}

int accessory_object_fault(WORD vdi_handle)
{
    RESOURCE r;
    AES_OBJECT_RESULT out;

    drawing_handle = vdi_handle;
    resource(&r);
    CHECK(command(0, AES_OBJECT_CREATE, 0, 0, 0, (uintptr_t)&r, sizeof(r), &out) == E_OK);
    expected_tree = out.value;
    fault = 1;
    (void)Cconws("x32 object USERDEF fault: ARMED\r\n");
    (void)draw_tree(expected_tree);
    CHECK(FALSE);                        /* only this owner must terminate */
    return 99;
}

static void cancel_menu(WORD buttons)
{
    if (buttons & 1)
        unregistered = aes_appl_exit();
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
    CHECK(command(tree, AES_OBJECT_DESKTOP, 0, 1, 0, 0, 0, &out) == E_OK);
    revoke = 1;
    CHECK(draw_tree(tree) == EACCDN && unregistered == E_OK && !callback_bad);
    revoke = 0;
    CHECK(aes_appl_init("ACCTWO", &info) == E_OK);
    CHECK(command(tree, AES_OBJECT_GET, 0, 0, 0, 0, 0, &out) == EIHNDL);
    CHECK(command(menu_tree, AES_OBJECT_GET, 0, 0, 0, 0, 0, &out) == EIHNDL);
    (void)Cconws("x32 objects snapshot/menu/dialog/USERDEF/revocation: PASS\r\n");
    return 0;
}
