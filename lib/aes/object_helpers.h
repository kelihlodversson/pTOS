/* Pure object hit-testing shared by private native AES adapters. */
#ifndef LIBAES_OBJECT_HELPERS_H
#define LIBAES_OBJECT_HELPERS_H
#include "libaes.h"
WORD aes_object_find(OBJECT *tree, WORD start, WORD depth, WORD mx, WORD my);
#endif
