/* Resource parser boundary tests, also run on the host with sanitizers. */
#include "aes_objects.h"
#include "gemerror.h"
#ifdef AES_TREE_HOST_TEST
#include <stdio.h>
#endif

int aes_tree_contract_checks(void);

typedef struct {
    AES_TREE_HEADER header;
    AES_OBJECT objects[3];
    AES_OBJECT_TEXT text;
    AES_OBJECT_STRING button;
    char value[8], pattern[8], valid[2], label[8];
} TEST_RESOURCE;

static void fixture(TEST_RESOURCE *r)
{
    static const TEST_RESOURCE initial = {
        { AES_TREE_VERSION, sizeof(TEST_RESOURCE), 3, 0, 0 },
        {
            { -1, 1, 2, 20, 0, 0, 0x1100, { 10, 10, 200, 100 } },
            { 2, -1, -1, 29, 8, 0, offsetof(TEST_RESOURCE, text), { 10, 10, 100, 16 } },
            { 0, -1, -1, 26, 7, 0, offsetof(TEST_RESOURCE, button), { 10, 50, 80, 16 } }
        },
        { offsetof(TEST_RESOURCE, value), offsetof(TEST_RESOURCE, pattern),
          offsetof(TEST_RESOURCE, valid), 2, 3, 0, 0x1100, 0, 8, 8 },
        { offsetof(TEST_RESOURCE, label), 8 },
        "", "_______", "X", "OK"
    };
    *r = initial;
}

int aes_tree_contract_checks(void)
{
    TEST_RESOURCE r;
    struct {
        AES_TREE_HEADER header;
        AES_OBJECT object;
        AES_OBJECT_CICON icon;
        AES_OBJECT_COLOUR colour;
        AES_OBJECT_STRING label;
        AES_OBJECT_IMAGE image;
        WORD pixels[16];
        char text[4];
    } bitmap = { 0 };
    struct {
        AES_TREE_HEADER header;
        AES_OBJECT objects[10];
    } deep = { { AES_TREE_VERSION, 0, 10, 0, 0 } };
    WORD parents[10];
    ULONG i;
    int failure = 0;

#define CHECK(c) do { if (!(c) && !failure) failure = __LINE__; } while (0)
    fixture(&r);
    CHECK(aes_tree_validate(&r, sizeof(r), parents) == E_OK);
    CHECK(parents[0] == -1 && parents[1] == 0 && parents[2] == 0);
    r.objects[1].next = 1;
    CHECK(aes_tree_validate(&r, sizeof(r), parents) == EINVAL);
    fixture(&r);
    r.objects[2].next = 3;
    CHECK(aes_tree_validate(&r, sizeof(r), parents) == EINVAL);
    fixture(&r);
    r.objects[1].head = r.objects[1].tail = 1;
    CHECK(aes_tree_validate(&r, sizeof(r), parents) == EINVAL);
    fixture(&r);
    r.objects[0].tail = 1;
    CHECK(aes_tree_validate(&r, sizeof(r), parents) == EINVAL);
    fixture(&r);
    r.objects[1].flags |= 0x0100;        /* INDIRECT is never a native pointer */
    CHECK(aes_tree_validate(&r, sizeof(r), parents) == EINVAL);
    fixture(&r);
    r.objects[1].spec = 0xfffffff0UL;
    CHECK(aes_tree_validate(&r, sizeof(r), parents) == EINVAL);
    fixture(&r);
    r.text.text = sizeof(r) - 2;
    CHECK(aes_tree_validate(&r, sizeof(r), parents) == EINVAL);
    fixture(&r);
    r.text.text_capacity = -1;
    CHECK(aes_tree_validate(&r, sizeof(r), parents) == EINVAL);
    fixture(&r);
    r.pattern[0] = 'x';
    CHECK(aes_tree_validate(&r, sizeof(r), parents) == EINVAL);
    fixture(&r);
    r.valid[1] = 'X';
    CHECK(aes_tree_validate(&r, sizeof(r), parents) == EINVAL);
    fixture(&r);
    r.objects[1].rectangle.g_x = 32767;
    CHECK(aes_tree_validate(&r, sizeof(r), parents) == EINVAL);
    fixture(&r);
    r.objects[0].spec = 0x7f001100;
    CHECK(aes_tree_validate(&r, sizeof(r), parents) == E_OK); /* box character, not pointer */
    r.objects[0].spec = 0x007f1100;
    CHECK(aes_tree_validate(&r, sizeof(r), parents) == EINVAL);
    fixture(&r);
    r.header.bytes--;
    CHECK(aes_tree_validate(&r, sizeof(r), parents) == EINVAL);
    bitmap.header.version = AES_TREE_VERSION;
    bitmap.header.bytes = sizeof(bitmap); bitmap.header.count = 1;
    bitmap.object.next = bitmap.object.head = bitmap.object.tail = -1;
    bitmap.object.type = 33;
#define AT(member) ((UBYTE *)&bitmap.member - (UBYTE *)&bitmap)
    bitmap.object.spec = AT(icon);
    bitmap.icon.mono.icon.g_w = bitmap.icon.mono.icon.g_h = 16;
    bitmap.icon.mono.mask = bitmap.icon.mono.data = AT(pixels);
    bitmap.icon.mono.text = AT(label);
    bitmap.label.text = AT(text); bitmap.label.capacity = sizeof(bitmap.text);
    bitmap.icon.colours = AT(colour);
    bitmap.colour.planes = 1;
    bitmap.colour.mask = bitmap.colour.data = AT(pixels);
    CHECK(aes_tree_validate(&bitmap, sizeof(bitmap), parents) == E_OK);
    bitmap.colour.selected_data = AT(pixels); /* selected mask is mandatory */
    CHECK(aes_tree_validate(&bitmap, sizeof(bitmap), parents) == EINVAL);
    bitmap.colour.selected_mask = AT(pixels);
    CHECK(aes_tree_validate(&bitmap, sizeof(bitmap), parents) == E_OK);
    bitmap.colour.planes = 32;            /* data span exceeds copied bytes */
    CHECK(aes_tree_validate(&bitmap, sizeof(bitmap), parents) == EINVAL);
    bitmap.colour.planes = 1; bitmap.colour.next = AT(colour);
    CHECK(aes_tree_validate(&bitmap, sizeof(bitmap), parents) == EINVAL);
    bitmap.object.type = 31;
    CHECK(aes_tree_validate(&bitmap, sizeof(bitmap), parents) == E_OK);
    bitmap.icon.mono.icon.g_w = 15;
    CHECK(aes_tree_validate(&bitmap, sizeof(bitmap), parents) == EINVAL);
    bitmap.object.type = 23; bitmap.object.spec = AT(image);
    bitmap.image.data = AT(pixels); bitmap.image.row_bytes = 2; bitmap.image.height = 16;
    CHECK(aes_tree_validate(&bitmap, sizeof(bitmap), parents) == E_OK);
    bitmap.image.row_bytes = 3;
    CHECK(aes_tree_validate(&bitmap, sizeof(bitmap), parents) == EINVAL);
#undef AT
    deep.header.bytes = sizeof(deep);
    for (i = 0; i < 10; i++) {
        deep.objects[i].next = i - 1;
        deep.objects[i].head = deep.objects[i].tail = i < 9 ? i + 1 : -1;
        deep.objects[i].type = 25;
    }
    CHECK(aes_tree_validate(&deep, sizeof(deep), parents) == EINVAL);
    deep.header.count = 9;
    deep.objects[8].head = deep.objects[8].tail = -1;
    CHECK(aes_tree_validate(&deep, sizeof(deep), parents) == E_OK);
#undef CHECK
    return failure;
}

#ifdef AES_TREE_HOST_TEST
int main(void)
{
    TEST_RESOURCE r;
    WORD parents[AES_TREE_MAX_OBJECTS];
    ULONG seed = 659;
    unsigned i, byte;
    int failure = aes_tree_contract_checks();

    /* Every randomized malformed input uses the real parser under ASan /
     * UBSan. The independent canary/allocator catches any out-of-range read. */
    for (i = 0; i < 20000; i++) {
        fixture(&r);
        seed = seed * 1664525UL + 1013904223UL;
        byte = seed % sizeof(r);
        ((UBYTE *)&r)[byte] ^= (UBYTE)(seed >> 24);
        (void)aes_tree_validate(&r, sizeof(r), parents);
    }
    printf("AES tree topology/payload bounds and malformed-input corpus: %s (%d)\n",
           failure ? "FAIL" : "PASS", failure);
    return !!failure;
}
#endif
