/* Bind the original alert layout to per-call resource storage. */
#include "alert_context.h"
#include "gemlayout.h"
#define ALERT_3D 1
#define fm_alert_tree aes_alert_tree
#define rs_trees (aes_alert_context->trees)
#define gl_hchar (aes_alert_context->metrics.hchar)
#define gl_rscreen (aes_alert_context->screen)
#define rs_obfix(tree,obj) rs_obfix_metrics(tree,obj,aes_alert_context->metrics.wchar,aes_alert_context->metrics.hchar,aes_alert_context->metrics.width)
#define ob_center(tree,rect) ob_center_metrics(tree,rect,aes_alert_context->metrics.width,aes_alert_context->metrics.height,aes_alert_context->metrics.hbox)
static void aes_alert_set_rect(OBJECT *tree, WORD object, const GRECT *rect)
{
    tree[object].ob_x = rect->g_x; tree[object].ob_y = rect->g_y;
    tree[object].ob_width = rect->g_w; tree[object].ob_height = rect->g_h;
}
#define ob_setxywh aes_alert_set_rect
