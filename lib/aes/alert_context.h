/* Stack-owned alerts, including nested USERDEF calls. */
#ifndef LIBAES_ALERT_CONTEXT_H
#define LIBAES_ALERT_CONTEXT_H
#include "libaes.h"
#include "layout.h"
#include "gem_rsc.h"
typedef struct aes_alert_call {
    OBJECT objects[10];
    OBJECT *trees[2];
    char lines[MAX_LINENUM][MAX_LINELEN+1];
    char buttons[MAX_BUTNUM][MAX_BUTLEN+1];
    GRECT screen;
    LAYOUT_METRICS metrics;
} ALERT_CONTEXT;
OBJECT *aes_alert_tree(ALERT_CONTEXT *aes_alert_context, WORD button, char *text, GRECT *rect);
typedef struct aes_error_call {
    AESGLOBAL *global;
    BOOL ok;
} ERROR_CONTEXT;
WORD aes_error_core(ERROR_CONTEXT *aes_error, WORD error);
WORD aes_error_show(ERROR_CONTEXT *aes_error, WORD string, WORD *number, WORD button);
#endif
