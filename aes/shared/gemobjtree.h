/* Application-local object-tree algorithms; no drawing or shared state. */
#ifndef GEMOBJTREE_H
#define GEMOBJTREE_H
#include "portab.h"
#include "obdefs.h"
WORD get_par(OBJECT *tree, WORD obj);
void ob_offset(OBJECT *tree, WORD obj, WORD *x, WORD *y);
void ob_add(OBJECT *tree, WORD parent, WORD child);
WORD ob_delete_local(OBJECT *tree, WORD obj);
BOOL ob_order(OBJECT *tree, WORD obj, WORD position);
#endif
