/* Stack-owned alerts, including nested USERDEF calls. */
#ifndef LIBAES_ALERT_CONTEXT_H
#define LIBAES_ALERT_CONTEXT_H
#include "gem.h"
#include "layout.h"
#include "gem_rsc.h"
typedef struct {
    OBJECT objects[10];
    OBJECT *trees[2];
    char lines[MAX_LINENUM][MAX_LINELEN+1];
    char buttons[MAX_BUTNUM][MAX_BUTLEN+1];
    GRECT screen;
    LAYOUT_METRICS metrics;
} ALERT_CONTEXT;
extern ALERT_CONTEXT *aes_alert_context;
OBJECT *aes_alert_tree(WORD button, char *text, GRECT *rect);
WORD aes_error_core(WORD error);
WORD aes_error_show(WORD string, WORD *number, WORD button);
#endif
