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
