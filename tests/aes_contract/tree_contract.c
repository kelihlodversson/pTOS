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
    WORD parents[3];
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
