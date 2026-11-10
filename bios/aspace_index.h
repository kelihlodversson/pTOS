/* Internal MMU metadata indexes, shared by anonymous COW and file pages.
 * No resource ownership, allocation, callbacks or I/O in index mutations. */
#ifndef ASPACE_INDEX_H
#define ASPACE_INDEX_H
#if CONF_WITH_PRIVATE_CLONE || CONF_WITH_MMAP
/* Intrusive AVL indexes: bounded logarithmic lookup without extra allocation.
 * The mapping list remains only the linear commit/rollback iteration view. */
struct cow_index {
    UQUAD key;
    struct cow_index *left, *right;
    UWORD height;
};
/* Each node is embedded first in its owning record. Rotations never move
 * records: frame references and mapping iteration links remain stable. */
#ifndef COW_INDEX_VISIT
#define COW_INDEX_VISIT() ((void)0)
#endif
static int cow_height(const struct cow_index *node) { return node ? node->height : 0; }
static void cow_height_update(struct cow_index *node)
{
    int left = cow_height(node->left), right = cow_height(node->right);
    node->height = 1 + (left > right ? left : right);
}
static struct cow_index *cow_rotate(struct cow_index *root, BOOL right)
{
    struct cow_index *top = right ? root->left : root->right;
    if (right) { root->left = top->right; top->right = root; }
    else { root->right = top->left; top->left = root; }
    cow_height_update(root); cow_height_update(top); return top;
}
static struct cow_index *cow_balance(struct cow_index *root)
{
    int delta;
    if (!root) return NULL;
    cow_height_update(root);
    delta = cow_height(root->left) - cow_height(root->right);
    if (delta > 1) {
        if (cow_height(root->left->left) < cow_height(root->left->right))
            root->left = cow_rotate(root->left,FALSE);
        return cow_rotate(root,TRUE);
    }
    if (delta < -1) {
        if (cow_height(root->right->right) < cow_height(root->right->left))
            root->right = cow_rotate(root->right,TRUE);
        return cow_rotate(root,FALSE);
    }
    return root;
}
static struct cow_index *cow_index_find(struct cow_index *root, UQUAD key)
{
    while (root) {
        COW_INDEX_VISIT();
        if (key == root->key) return root;
        root = key < root->key ? root->left : root->right;
    }
    return NULL;
}
static struct cow_index *cow_index_add(struct cow_index *root, struct cow_index *node)
{
    if (!root) { node->left = node->right = NULL; node->height = 1; return node; }
    COW_INDEX_VISIT();
    if (node->key == root->key) panic("duplicate COW index");
    if (node->key < root->key) root->left = cow_index_add(root->left,node);
    else root->right = cow_index_add(root->right,node);
    return cow_balance(root);
}
static struct cow_index *cow_index_remove(struct cow_index *root, UQUAD key)
{
    if (!root) panic("missing COW index");
    COW_INDEX_VISIT();
    if (key < root->key) root->left = cow_index_remove(root->left,key);
    else if (key > root->key) root->right = cow_index_remove(root->right,key);
    else {
        struct cow_index *next = root->right;
        if (!next) return root->left;
        while (next->left) { COW_INDEX_VISIT(); next = next->left; }
        next->right = cow_index_remove(root->right,next->key);
        next->left = root->left;
        return cow_balance(next);
    }
    return cow_balance(root);
}
#endif
#endif
