/* Ordinary GEM source, also compiled against unmodified GEMlib headers. */
#include <gem.h>
#include "gem_resources.h"
#include <stddef.h>
/* Avoid mixing the legacy kernel portab and libcmini binding macros. */
extern int strcmp(const char *a, const char *b);
extern int memcmp(const void *a, const void *b, size_t size);
extern void *memcpy(void *to, const void *from, size_t size);
#define VERIFY(c) do { if (!(c)) return 0x7800 | __LINE__; } while (0)
static unsigned long pointer_field(const short *global, short field)
{
    unsigned int value;
    memcpy(&value,global+field,4);
    return value;
}
int gem_resource_checks(short graphics, short wchar, short hchar)
{
    OBJECT *tree, *second, *object;
    TEDINFO *ted;
    ICONBLK *icon;
    BITBLK *bit;
    void *address, *slot;
    short first_global[15], stale[15], invalid[15], i, current_tree[2];
    char replacement[7] = "XYZ";
    aes_global[3] = 0x1234; aes_global[4] = 0x5678;
    VERIFY(rsrc_load("GEMTEST.RSC") == graphics);
    if (!graphics) {
        VERIFY(rsrc_free() == 0 && rsrc_gaddr(R_TREE,0,&tree) == 0);
        return 0;
    }
    VERIFY(rsrc_gaddr(R_TREE,0,&tree) == 1);
    VERIFY(tree[0].ob_width == 20*wchar && tree[0].ob_height == 8*hchar);
    VERIFY(tree[1].ob_x == 2*wchar-2 && tree[1].ob_y == hchar && tree[1].ob_width == 6*wchar);
    VERIFY(rsrc_gaddr(R_OBJECT,1,&object) == 1 && object == tree+1);
    VERIFY(rsrc_gaddr(R_TEDINFO,0,&ted) == 1 && tree[1].ob_spec.tedinfo == ted);
    VERIFY(!strcmp(ted->te_ptext,"abc") && !strcmp(ted->te_ptmplt,"______") && !strcmp(ted->te_pvalid,"F"));
    VERIFY(rsrc_gaddr(R_TEPTEXT,0,&slot) == 1 && slot == &ted->te_ptext);
    VERIFY(rsrc_gaddr(R_TEPTMPLT,0,&slot) == 1 && slot == &ted->te_ptmplt);
    VERIFY(rsrc_gaddr(R_TEPVALID,0,&slot) == 1 && slot == &ted->te_pvalid);
    VERIFY(tree[2].ob_spec.indirect == tree[3].ob_spec.indirect);
    VERIFY(!strcmp(tree[2].ob_spec.indirect->free_string,"Alias"));
    VERIFY(tree[6].ob_spec.userblk->ub_code == 0 && tree[6].ob_spec.userblk->ub_parm == 0x12345678);
    VERIFY(rsrc_gaddr(R_ICONBLK,0,&icon) == 1 && icon == tree[5].ob_spec.iconblk);
    VERIFY(rsrc_gaddr(R_IBPMASK,0,&slot) == 1 && slot == &icon->ib_pmask);
    VERIFY(rsrc_gaddr(R_IBPDATA,0,&slot) == 1 && slot == &icon->ib_pdata);
    VERIFY(rsrc_gaddr(R_IBPTEXT,0,&slot) == 1 && slot == &icon->ib_ptext);
    VERIFY((unsigned short)icon->ib_pdata[0] == 0xa55a && (unsigned short)icon->ib_pmask[0] == 0xffff);
    VERIFY(rsrc_gaddr(R_BITBLK,0,&bit) == 1 && bit == tree[4].ob_spec.bitblk);
    VERIFY(rsrc_gaddr(R_BIPDATA,0,&slot) == 1 && slot == &bit->bi_pdata);
    VERIFY(*(unsigned short *)bit->bi_pdata == 0xa55a);
    VERIFY(rsrc_gaddr(R_IMAGEDATA,1,&address) == 1 && address == bit);
    VERIFY(rsrc_gaddr(R_FRIMG,1,&slot) == 1 && *(void **)slot == bit);
    VERIFY(rsrc_gaddr(R_STRING,0,&address) == 1 && !strcmp(address,"Free one"));
    VERIFY(rsrc_gaddr(R_STRING,1,&address) == 1 && !strcmp(address,"Intact"));
    VERIFY(rsrc_gaddr(R_FRSTR,1,&slot) == 1 && *(void **)slot == address);
    VERIFY(aes_global[3] == 0x1234 && aes_global[4] == 0x5678);
    VERIFY(pointer_field(aes_global,5) != 0 && pointer_field(aes_global,7) != 0 && aes_global[9] != 0);
    if (sizeof(void *) == 4) {
        void **trees = 0;
        memcpy(&trees,aes_global+5,4);
        VERIFY(trees[0] == tree);
    }
    for (i = 0; i < 15; i++) first_global[i] = stale[i] = invalid[i] = aes_global[i];
    VERIFY(rsrc_load("MISSING.RSC") == 0 && !memcmp(first_global,aes_global,sizeof(first_global)));
    VERIFY(rsrc_saddr(R_TEPTEXT,0,replacement) == 1 && ted->te_ptext == replacement);
    VERIFY(rsrc_saddr(R_FRSTR,1,replacement) == 1);
    VERIFY(rsrc_gaddr(R_STRING,1,&address) == 1 && address == replacement);
    VERIFY(rsrc_saddr(R_OBSPEC,7,replacement) == 1 && tree[7].ob_spec.free_string == replacement);
    VERIFY(rsrc_saddr(R_OBJECT,8,replacement) == 0);
    VERIFY(rsrc_gaddr(17,0,&address) == 0 && address == (void *)-1L);
    VERIFY(rsrc_gaddr(R_OBJECT,-1,&address) == 0 && rsrc_gaddr(R_OBJECT,8,&address) == 0);
    VERIFY(rsrc_gaddr(R_TEDINFO,1,&address) == 0 && rsrc_gaddr(R_TREE,1,&address) == 0);
    invalid[2] = -1;
    VERIFY(mt_rsrc_gaddr(R_TREE,0,&address,invalid) == 0 && mt_rsrc_free(invalid) == 0);
    VERIFY(rsrc_load("GEMTEST.RSC") == 1 && rsrc_gaddr(R_TREE,0,&second) == 1 && tree != second);
    /* Native R_TREE follows the advertised tree-table global independently
     * of the private resource-image global used by all the other selectors. */
    current_tree[0] = aes_global[5]; current_tree[1] = aes_global[6];
    aes_global[5] = first_global[5]; aes_global[6] = first_global[6];
    VERIFY(rsrc_gaddr(R_TREE,0,&object) == 1 && object == tree);
    VERIFY(rsrc_gaddr(R_TEDINFO,0,&address) == 1 && address == second[1].ob_spec.tedinfo);
    aes_global[5] = current_tree[0]; aes_global[6] = current_tree[1];
    VERIFY(mt_rsrc_gaddr(R_TREE,0,&object,first_global) == 1 && object == tree);
    VERIFY(ted->te_ptext == replacement && !strcmp(second[1].ob_spec.tedinfo->te_ptext,"abc"));
    VERIFY(rsrc_free() == 1 && rsrc_free() == 0);
    VERIFY(pointer_field(aes_global,5) == 0 && pointer_field(aes_global,7) == 0 && aes_global[9] == 0);
    VERIFY(mt_rsrc_gaddr(R_TREE,0,&object,first_global) == 1 && object == tree);
    VERIFY(mt_rsrc_free(first_global) == 1 && mt_rsrc_free(stale) == 0);
    VERIFY(mt_rsrc_gaddr(R_TREE,0,&object,stale) == 0);
    return 0;
}

int gem_resource_original(void)
{
    OBJECT *tree[3];
    BITBLK *icon;
    void *text, *invalid;
    VERIFY(rsrc_load("GEMORIG.RSC") == 1);
    VERIFY(rsrc_gaddr(R_TREE,0,&tree[0]) == 1 && rsrc_gaddr(R_TREE,1,&tree[1]) == 1 && rsrc_gaddr(R_TREE,2,&tree[2]) == 1);
    VERIFY(tree[0] != tree[1] && tree[1] != tree[2] && rsrc_gaddr(R_TREE,3,&invalid) == 0);
    VERIFY(rsrc_gaddr(R_STRING,0,&text) == 1 && ((char *)text)[0] == '[');
    VERIFY(rsrc_gaddr(R_BITBLK,0,&icon) == 1 && icon->bi_wb == 4 && icon->bi_hl == 32);
    VERIFY(rsrc_free() == 1);
    return 0;
}

/* Source-generator layout: native structs, native byte order and native
 * pointer-sized image-relative offsets. This is not a packed disk RSC. */
typedef struct {
    RSHDR header;
    OBJECT objects[8];
    TEDINFO ted;
    ICONBLK icon;
    BITBLK bit;
    USERBLK user;
    OBJECT *trees[1];
    char *strings[2];
    BITBLK *images[1];
    OBSPEC indirect;
    unsigned short bitmap, mask;
    char text[8], template[8], valid[2], label[8];
} NATIVE_RSC;
static short native_callback(PARMBLK *pb) { return pb->pb_currstate; }
#define NATIVE_OFFSET(member) ((unsigned long)offsetof(NATIVE_RSC,member))
static void native_fixture(NATIVE_RSC *r)
{
    short i;
    unsigned char *bytes = (unsigned char *)r;
    for (i = 0; i < (short)sizeof(*r); i++) bytes[i] = 0;
    r->header.rsh_vrsn = 1;
    r->header.rsh_object = NATIVE_OFFSET(objects);
    r->header.rsh_tedinfo = NATIVE_OFFSET(ted);
    r->header.rsh_iconblk = NATIVE_OFFSET(icon);
    r->header.rsh_bitblk = NATIVE_OFFSET(bit);
    r->header.rsh_trindex = NATIVE_OFFSET(trees);
    r->header.rsh_frstr = NATIVE_OFFSET(strings);
    r->header.rsh_frimg = NATIVE_OFFSET(images);
    r->header.rsh_string = NATIVE_OFFSET(text);
    r->header.rsh_imdata = NATIVE_OFFSET(bitmap);
    r->header.rsh_nobs = 8; r->header.rsh_ntree = 1;
    r->header.rsh_nted = r->header.rsh_nib = r->header.rsh_nbb = 1;
    r->header.rsh_nstring = 2; r->header.rsh_nimages = 1;
    r->header.rsh_rssize = sizeof(*r);
    for (i = 0; i < 8; i++) {
        OBJECT *o = r->objects+i;
        o->ob_next = i == 0 ? -1 : i == 7 ? 0 : i+1;
        o->ob_head = o->ob_tail = -1;
        o->ob_type = G_STRING;
        o->ob_width = 6; o->ob_height = 1; o->ob_x = 0xfe02; o->ob_y = i;
        o->ob_spec.free_string = (char *)NATIVE_OFFSET(label);
    }
    r->objects[0].ob_type = G_IBOX; r->objects[0].ob_spec.index = 0;
    r->objects[0].ob_head = 1; r->objects[0].ob_tail = 7;
    r->objects[0].ob_x = r->objects[0].ob_y = 0;
    r->objects[0].ob_width = 20; r->objects[0].ob_height = 8;
    r->objects[1].ob_type = G_FTEXT;
    r->objects[1].ob_spec.tedinfo = (TEDINFO *)NATIVE_OFFSET(ted);
    r->objects[2].ob_flags = r->objects[3].ob_flags = 0x100; /* INDIRECT */
    r->objects[2].ob_spec.indirect = r->objects[3].ob_spec.indirect = (OBSPEC *)NATIVE_OFFSET(indirect);
    r->indirect.free_string = (char *)NATIVE_OFFSET(label);
    r->objects[3].ob_type = G_TITLE; /* Compatible shared string slot. */
    r->objects[4].ob_type = G_IMAGE; r->objects[4].ob_spec.bitblk = (BITBLK *)NATIVE_OFFSET(bit);
    r->objects[5].ob_type = G_ICON; r->objects[5].ob_spec.iconblk = (ICONBLK *)NATIVE_OFFSET(icon);
    r->objects[6].ob_type = G_USERDEF; r->objects[6].ob_spec.userblk = (USERBLK *)NATIVE_OFFSET(user);
    r->objects[7].ob_type = G_BUTTON; r->objects[7].ob_flags = 0x20; /* LASTOB */
    r->ted.te_ptext = (char *)NATIVE_OFFSET(text);
    r->ted.te_ptmplt = (char *)NATIVE_OFFSET(template);
    r->ted.te_pvalid = (char *)NATIVE_OFFSET(valid);
    r->ted.te_txtlen = r->ted.te_tmplen = 8; r->ted.te_font = 3;
    r->icon.ib_pmask = (short *)NATIVE_OFFSET(mask);
    r->icon.ib_pdata = (short *)NATIVE_OFFSET(bitmap);
    r->icon.ib_ptext = (char *)NATIVE_OFFSET(label);
    r->icon.ib_wicon = 16; r->icon.ib_hicon = 1;
    r->bit.bi_pdata = (void *)NATIVE_OFFSET(bitmap); r->bit.bi_wb = 2; r->bit.bi_hl = 1;
    r->user.ub_code = native_callback; r->user.ub_parm = 1234;
    r->trees[0] = (OBJECT *)NATIVE_OFFSET(objects);
    r->strings[0] = (char *)NATIVE_OFFSET(text); r->strings[1] = (char *)-1L;
    r->images[0] = (BITBLK *)NATIVE_OFFSET(bit);
    r->bitmap = 0xa55a; r->mask = 0xffff;
    memcpy(r->text,"abc",4); memcpy(r->template,"______",7);
    memcpy(r->valid,"F",2); memcpy(r->label,"Native",7);
}

int gem_resource_native(short graphics, short wchar, short hchar)
{
    NATIVE_RSC r, raw, fixed;
    OBJECT *tree;
    void *address;
    short saved[15], other[15], invalid[15], i;
    if (sizeof(void *) == 4)
        VERIFY(sizeof(RSHDR) == 36 && sizeof(OBJECT) == 24 && sizeof(TEDINFO) == 28 &&
            sizeof(ICONBLK) == 36 && sizeof(BITBLK) == 16 && sizeof(USERBLK) == 8);
    native_fixture(&r); memcpy(&raw,&r,sizeof(r));
    memcpy(saved,aes_global,sizeof(saved)); memcpy(invalid,saved,sizeof(saved)); invalid[2] = -1;
    VERIFY(mt_rsrc_rcfix(&r,invalid) == 0 && !memcmp(&r,&raw,sizeof(r)));
    VERIFY(rsrc_rcfix(&r) == graphics);
    if (!graphics) { VERIFY(!memcmp(&r,&raw,sizeof(r))); return 0; }
    VERIFY(rsrc_gaddr(R_TREE,0,&tree) == 1 && tree == r.objects);
    VERIFY(tree[0].ob_width == 20*wchar && tree[0].ob_height == 8*hchar);
    VERIFY(tree[1].ob_x == 2*wchar-2 && tree[1].ob_y == hchar);
    VERIFY(tree[1].ob_spec.tedinfo == &r.ted && r.ted.te_ptext == r.text &&
        r.ted.te_ptmplt == r.template && r.ted.te_pvalid == r.valid);
    VERIFY(tree[2].ob_spec.indirect == &r.indirect && tree[3].ob_spec.indirect == &r.indirect &&
        r.indirect.free_string == r.label);
    VERIFY(tree[4].ob_spec.bitblk == &r.bit && (void *)r.bit.bi_pdata == &r.bitmap);
    VERIFY(tree[5].ob_spec.iconblk == &r.icon && r.icon.ib_pdata == (short *)&r.bitmap &&
        r.icon.ib_pmask == (short *)&r.mask && r.icon.ib_ptext == r.label);
    VERIFY(tree[6].ob_spec.userblk == &r.user && r.user.ub_code == native_callback && r.user.ub_parm == 1234);
    VERIFY(rsrc_gaddr(R_STRING,0,&address) == 1 && address == r.text);
    VERIFY(rsrc_gaddr(R_FRSTR,1,&address) == 1 && *(char **)address == (char *)-1L);
    VERIFY(rsrc_gaddr(R_IMAGEDATA,0,&address) == 1 && address == &r.bit);
    memcpy(other,aes_global,sizeof(other)); memcpy(&fixed,&r,sizeof(r));
    VERIFY(rsrc_rcfix(&r) == 0 && !memcmp(&r,&fixed,sizeof(r)));
    VERIFY(mt_rsrc_free(other) == 1 && !memcmp(&r,&fixed,sizeof(r)));
    VERIFY(rsrc_gaddr(R_TREE,0,&tree) == 0);
    /* An already relocated image is not silently fixed twice after free. */
    VERIFY(rsrc_rcfix(&r) == 0 && !memcmp(&r,&fixed,sizeof(r)));
    memcpy(&r,&raw,sizeof(r)); memcpy(aes_global,saved,sizeof(saved));
    VERIFY(mt_rsrc_rcfix(&r,aes_global) == 1 && rsrc_free() == 1);
    /* Every rejected raw image must leave both data and globals unchanged. */
    for (i = 0; i < 20; i++) {
        memcpy(&r,&raw,sizeof(r));
        switch (i) {
        case 0: r.header.rsh_nobs = -1; break;
        case 1: r.header.rsh_object++; break;
        case 2: r.header.rsh_rssize = sizeof(RSHDR)-1; break;
        case 3: r.header.rsh_vrsn = 4; break;
        case 4: r.ted.te_ptext = (char *)(unsigned long)sizeof(r); break;
        case 5: r.objects[1].ob_spec.tedinfo = (TEDINFO *)NATIVE_OFFSET(label); break;
        case 6: r.objects[2].ob_spec.indirect = (OBSPEC *)NATIVE_OFFSET(text); break;
        case 7: r.objects[1].ob_next = 1; break;
        case 8: r.trees[0] = (OBJECT *)NATIVE_OFFSET(ted); break;
        case 9: r.bit.bi_wb = -1; break;
        case 10: r.icon.ib_hicon = 32767; break;
        case 11: r.objects[7].ob_flags = 0; break;
        case 12: r.header.rsh_frstr = r.header.rsh_trindex; break;
        case 13: r.ted.te_ptext = (char *)NATIVE_OFFSET(objects); break;
        case 14: /* Relocation must not replace a USERDEF callback parameter. */
        case 15:
            r.user.ub_parm = NATIVE_OFFSET(label);
            r.objects[2].ob_spec.indirect = r.objects[3].ob_spec.indirect =
                (OBSPEC *)(NATIVE_OFFSET(user)+offsetof(USERBLK,ub_parm));
            if (i == 15) { /* Protect USERBLK before encountering the slot. */
                r.objects[1].ob_type = G_USERDEF;
                r.objects[1].ob_spec.userblk = (USERBLK *)NATIVE_OFFSET(user);
            }
            break;
        case 16: r.strings[0] = (char *)NATIVE_OFFSET(indirect); break;
        case 17: /* Protect string data before encountering the slot. */
            r.objects[1].ob_type = G_STRING;
            r.objects[1].ob_spec.free_string = (char *)NATIVE_OFFSET(indirect);
            break;
        case 18: r.bit.bi_pdata = (void *)NATIVE_OFFSET(indirect); break;
        case 19: r.icon.ib_pmask = (short *)NATIVE_OFFSET(indirect); break;
        }
        memcpy(&fixed,&r,sizeof(r)); memcpy(saved,aes_global,sizeof(saved));
        VERIFY(rsrc_rcfix(&r) == 0 && !memcmp(&r,&fixed,sizeof(r)) &&
            !memcmp(saved,aes_global,sizeof(saved)));
    }
    return 0;
}

/* Host fault injection expects all failed fixups to be transactional. */
int gem_resource_native_failure(void)
{
    NATIVE_RSC r, raw;
    short saved[15];
    native_fixture(&r); memcpy(&raw,&r,sizeof(r)); memcpy(saved,aes_global,sizeof(saved));
    VERIFY(rsrc_rcfix(&r) == 0);
    VERIFY(!memcmp(&r,&raw,sizeof(r)) && !memcmp(saved,aes_global,sizeof(saved)));
    return 0;
}
