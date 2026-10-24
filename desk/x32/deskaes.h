/* Local classic binding adapter. No AESPB crosses into the kernel. */
#ifndef DESK_X32_AES_H
#define DESK_X32_AES_H
#include "aes_objects.h"
#include "obdefs.h"
/* Classic window argument/result arrays contain at least 8/7 WORDs. */
LONG desk_window(UWORD opcode, const UWORD *in, UWORD *out);
WORD desk_aes_dispatch(UWORD opcode, UWORD *global, UWORD *in, UWORD *out,
                       intptr_t *addresses, intptr_t *results);
#if CONF_WITH_X32_DESKTOP_TEST
void desk_test_record(const char *message);
void desk_tree_test(void);
void desk_window_test(void);
#else
#define desk_test_record(message) ((void)0)
#endif
typedef struct {
    OBJECT *source;
    UBYTE *blob;
    ULONG handle, bytes;
    WORD root;
} DESK_TREE;
LONG desk_object_call(AES_OBJECT_REQUEST *request, AES_OBJECT_RESULT *result);
BOOL desk_tree_create(DESK_TREE *snapshot, OBJECT *source);
void desk_tree_destroy(DESK_TREE *snapshot);
void desk_tree_read(DESK_TREE *snapshot);
LONG desk_tree_operation(OBJECT *tree, WORD object, UWORD operation,
                         WORD value, WORD extra, const GRECT *rectangle,
                         AES_OBJECT_RESULT *result);
WORD desk_tree_retain(OBJECT *tree, WORD root, BOOL menu, BOOL show);
void desk_trees_exit(void);
void desk_tree_checkpoint(void);
WORD desk_file_select(char *path, char *selection, WORD *button, const char *title);
#endif
