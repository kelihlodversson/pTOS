/* Optional per-call helper parameters for the userspace AES backend.
 * Classic AES keeps its existing internal function signatures. */
#ifndef AES_SHARED_CONTEXT_H
#define AES_SHARED_CONTEXT_H
#ifdef LIBAES_CONTEXTS
struct painter;
struct aesglobal;
struct aes_form_call;
struct aes_modal_call;
struct aes_alert_call;
struct aes_error_call;
struct aes_fsel_call;
struct resource_context;
struct aes_shell_call;
#define AES_RENDER_PARAM struct painter *aes_painter,
#define AES_RENDER_PARAM0 struct painter *aes_painter
#define AES_RENDER_ARG aes_painter,
#define AES_RENDER_ARG0 aes_painter
#define AES_FORM_PARAM struct aes_form_call *aes_form_context,
#define AES_FORM_PARAM0 struct aes_form_call *aes_form_context
#define AES_FORM_ARG aes_form_context,
#define AES_FORM_ARG0 aes_form_context
#define AES_MODAL_PARAM struct aes_modal_call *aes_modal,
#define AES_MODAL_PARAM0 struct aes_modal_call *aes_modal
#define AES_MODAL_ARG aes_modal,
#define AES_MODAL_ARG0 aes_modal
#define AES_ALERT_PARAM struct aes_alert_call *aes_alert_context,
#define AES_ALERT_PARAM0 struct aes_alert_call *aes_alert_context
#define AES_ALERT_ARG aes_alert_context,
#define AES_ALERT_ARG0 aes_alert_context
#define AES_ERROR_PARAM struct aes_error_call *aes_error,
#define AES_ERROR_PARAM0 struct aes_error_call *aes_error
#define AES_ERROR_ARG aes_error,
#define AES_ERROR_ARG0 aes_error
#define AES_FSEL_PARAM struct aes_fsel_call *aes_fsel,
#define AES_FSEL_PARAM0 struct aes_fsel_call *aes_fsel
#define AES_FSEL_ARG aes_fsel,
#define AES_FSEL_ARG0 aes_fsel
#define AES_RESOURCE_PARAM struct resource_context *aes_resource,
#define AES_RESOURCE_PARAM0 struct resource_context *aes_resource
#define AES_RESOURCE_ARG aes_resource,
#define AES_RESOURCE_ARG0 aes_resource
#define AES_SHELL_PARAM struct aes_shell_call *aes_shell,
#define AES_SHELL_PARAM0 struct aes_shell_call *aes_shell
#define AES_SHELL_ARG aes_shell,
#define AES_SHELL_ARG0 aes_shell
#define AES_MENU_PARAM struct aesglobal *aes_menu_global,
#define AES_MENU_PARAM0 struct aesglobal *aes_menu_global
#define AES_MENU_ARG aes_menu_global,
#define AES_MENU_ARG0 aes_menu_global
#define AES_WALK_PARAM void *walk_context,
#define AES_WALK_PARAM0 void *walk_context
#define AES_WALK_ARG walk_context,
#define AES_WALK_ARG0 walk_context
#else
#define AES_RENDER_PARAM
#define AES_RENDER_PARAM0 void
#define AES_RENDER_ARG
#define AES_RENDER_ARG0
#define AES_FORM_PARAM
#define AES_FORM_PARAM0 void
#define AES_FORM_ARG
#define AES_FORM_ARG0
#define AES_MODAL_PARAM
#define AES_MODAL_PARAM0 void
#define AES_MODAL_ARG
#define AES_MODAL_ARG0
#define AES_ALERT_PARAM
#define AES_ALERT_PARAM0 void
#define AES_ALERT_ARG
#define AES_ALERT_ARG0
#define AES_ERROR_PARAM
#define AES_ERROR_PARAM0 void
#define AES_ERROR_ARG
#define AES_ERROR_ARG0
#define AES_FSEL_PARAM
#define AES_FSEL_PARAM0 void
#define AES_FSEL_ARG
#define AES_FSEL_ARG0
#define AES_RESOURCE_PARAM
#define AES_RESOURCE_PARAM0 void
#define AES_RESOURCE_ARG
#define AES_RESOURCE_ARG0
#define AES_SHELL_PARAM
#define AES_SHELL_PARAM0 void
#define AES_SHELL_ARG
#define AES_SHELL_ARG0
#define AES_MENU_PARAM
#define AES_MENU_PARAM0 void
#define AES_MENU_ARG
#define AES_MENU_ARG0
#define AES_WALK_PARAM
#define AES_WALK_PARAM0 void
#define AES_WALK_ARG
#define AES_WALK_ARG0
#endif
#endif
