/* Bind the native AES query/search core to process-local startup and DTA. */
#include "shell_context.h"
#include "resource_context.h"
#define sh_read aes_shell_read_core
#define sh_name aes_shell_name
#define sh_envrn aes_shell_env_core
#define sh_find aes_shell_find_core
#define SH_COMMAND aes_shell_command
#define SH_TAIL aes_shell_tail
#define SH_ENVIRONMENT aes_shell_environment
#define SH_ENV_ENTRY(p) (strncmp(p,"ARGV=",5) != 0)
#define SH_APPDIR aes_resource_directory()
#define SH_WORK (aes_shell->work)
#define SH_DTA aes_shell_dta
#define SH_WORK_BYTES AES_SHELL_PATH
#define SH_COPY aes_shell_copy
#define SH_APPEND aes_shell_append
#define SH_PATH_VALID aes_shell_path_valid
/* Keep the last terminator as the next cursor; never scan the next env var. */
#define SH_PATH_NEXT(end) (*(end) ? (end)+1 : (end))
/* x32 startup has no old TOS boot-drive-byte PATH initialization bug. */
#define SH_EMPTY_PATH(path) return 0
#define dos_gdta() (aes_shell->original_dta)
#define dos_sdta aes_shell_set_dta
#define dos_sfirst aes_shell_first
