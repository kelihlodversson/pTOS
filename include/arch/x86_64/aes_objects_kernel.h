/* Native AES lifetime hooks for validated x32 snapshots. */
#ifndef AES_OBJECTS_KERNEL_H
#define AES_OBJECTS_KERNEL_H
#include "aes_objects.h"
#include "bdosdefs.h"
struct _object;
struct aespd;
typedef struct aes_tree_state AES_TREE_STATE;
LONG x86_64_aes_objects(UQUAD input, UQUAD output);
void x86_64_aes_objects_exit(PD *owner);
void x86_64_aes_objects_reap(void);
void x86_64_aes_objects_drain(void);
void x86_64_aes_objects_finished(struct aespd *executor);
void x86_64_aes_objects_menu_change(struct _object *tree, WORD show);
void x86_64_aes_objects_menu_fixup(struct _object *tree, WORD box);
AES_TREE_STATE *x86_64_aes_tree_pin(struct _object *tree);
void x86_64_aes_tree_unpin(AES_TREE_STATE *tree);
BOOL x86_64_aes_tree_alive(AES_TREE_STATE *tree);
BOOL x86_64_aes_tree_cancelled(struct _object *tree);
BOOL x86_64_aes_tree_managed(struct _object *tree);
void x86_64_aes_tree_message(PD *owner, WORD *message);
void wm_snapshot_set(struct _object *tree, WORD root);
void wm_snapshot_clear(struct _object *tree);
#endif
