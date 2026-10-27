/* Caller-local GEM form navigation, independent of drawing/dispatch. */
#ifndef GEMFORMKEY_H
#define GEMFORMKEY_H
#include "portab.h"
#include "obdefs.h"
enum { FM_FORWARD, FM_BACKWARD, FM_DEFLT };
WORD fm_find_field(const OBJECT *tree, WORD object, WORD direction);
BOOL fm_keybd_step(const OBJECT *tree, WORD object, WORD *key, WORD *next);
#endif
