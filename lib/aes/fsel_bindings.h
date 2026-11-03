/* Bind the original native selector to one userspace invocation. */
#include "fsel_context.h"
#include "resource_context.h"
#include "gemlayout.h"
#include "gemfslib.h"
#include "gemobjtree.h"
#include "gemobed.h"
#define FSEL_3D 1
#define FSEL_OK() (aes_fsel->ok)
#define fs_start aes_fsel_start
#define fs_input aes_fsel_core
#define fs_dta (aes_fsel->dta)
#define gl_rfs (aes_fsel->bounds)
#define ad_fsnames (aes_fsel->names)
#define g_fslist (aes_fsel->list)
#define nm_files (aes_fsel->count)
#define drvbits (aes_fsel->drives)
#define rs_trees (aes_fsel->trees)
#define gl_rcenter (aes_fsel->center)
#define gl_wchar (aes_fsel->metrics.wchar)
#define gl_hbox (aes_fsel->metrics.hbox)
#define ob_center(tree,rect) ob_center_metrics(tree,rect,aes_fsel->metrics.width,aes_fsel->metrics.height,aes_fsel->metrics.hbox)
#define ob_draw aes_fsel_draw
#define ob_change aes_fsel_change
#define ob_actxywh aes_fsel_rect
#define gsx_sclip aes_fsel_clip
#define gsx_gclip aes_fsel_get_clip
#define gsx_mxmy aes_fsel_pointer
#define set_mouse_to_arrow() aes_fsel_cursor(ARROW)
#define set_mouse_to_hourglass() aes_fsel_cursor(HOURGLASS)
#define fm_do aes_fsel_form
#define fm_dial aes_fsel_dial
#define fm_own(begin) ((void)(begin))
#define gr_slidebox aes_fsel_slide
#define fm_error aes_fsel_error
#define fm_show aes_fsel_show
#define rs_str(index) ((char *)rs_fstr[index])
#define dos_alloc_anyram aes_temporary_alloc
#define dos_free aes_resource_discard
#define dos_gdta aes_fsel_get_dta
#define dos_sdta aes_fsel_set_dta
#define dos_gdrv aes_fsel_drive
#define dos_sfirst aes_fsel_first
#define dos_snext aes_fsel_next
#define bb_screen aes_fsel_copy_screen
