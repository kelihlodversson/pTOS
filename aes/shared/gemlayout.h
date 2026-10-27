/* Caller-local GEM layout using explicit workstation metrics. */
#ifndef GEMLAYOUT_H
#define GEMLAYOUT_H
#include "portab.h"
#include "obdefs.h"
void rs_obfix_metrics(OBJECT *tree, WORD object, WORD wchar, WORD hchar, WORD screen_width);
void ob_center_metrics(OBJECT *tree, GRECT *rect, WORD screen_width, WORD screen_height, WORD hbox);
#endif
