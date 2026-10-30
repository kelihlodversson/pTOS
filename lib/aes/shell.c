/* Standard GEM shell startup/environment/file lookup, without shell services.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gem.h"
#include "shell_context.h"
#include "resource_context.h"
#include "application.h"
#include "transport.h"
#include "gemerror.h"
#include "ptos.h"
#include "string.h"

SHELL_CONTEXT *aes_shell;
char aes_shell_command[AES_SHELL_PATH], aes_shell_tail[128];
char *aes_shell_environment;
static BOOL startup_ready;
static char empty_environment[2];

static ULONG bounded_length(const char *text, ULONG limit)
{
    ULONG length = 0;
    if (!text) return limit;
    while (length < limit && text[length]) length++;
    return length;
}
BOOL aes_shell_copy(char *dest, const char *src, ULONG size)
{
    ULONG length = bounded_length(src,size);
    if (length == size) return FALSE;
    memcpy(dest,src,length+1); return TRUE;
}
BOOL aes_shell_append(char *dest, const char *src, ULONG size)
{
    ULONG length = bounded_length(dest,size);
    return length < size && aes_shell_copy(dest+length,src,size-length);
}
BOOL aes_shell_path_valid(const char *path, const char *name)
{
    ULONG length = 0, n = bounded_length(name,AES_SHELL_PATH), separator;
    while (path[length] && path[length] != ';' && path[length] != ',') length++;
    separator = !length || (path[length-1] != '\\' && path[length-1] != ':');
    return length < AES_SHELL_PATH && n < AES_SHELL_PATH && length+n+separator < AES_SHELL_PATH;
}
static BOOL environment_valid(void)
{
    ULONG i;
    if (!aes_shell_environment) return FALSE;
    for (i = 0; i < AES_SHELL_ENV-1; i++)
        if (!aes_shell_environment[i] && !aes_shell_environment[i+1]) return TRUE;
    return FALSE;
}

/* DT_INIT runs before the application's constructors or main can change cwd.
 * Startup metadata already belongs to this process through the existing ABI. */
void aes_library_init(void)
{
    PD *basepage = (PD *)(uintptr_t)__ptos_getauxval(0x5000); /* DL_AT_PTOS_BASEPAGE */
    const char *command = (const char *)(uintptr_t)__ptos_getauxval(31); /* AT_EXECFN */
    char path[AES_SHELL_PATH];
    ULONG i;
    aes_resources_init();
    startup_ready = FALSE; aes_shell_command[0] = aes_shell_tail[0] = 0;
    aes_shell_environment = basepage && basepage->p_env ? USERPTR_TO_PTR(basepage->p_env) : empty_environment;
    if (!basepage || bounded_length(command,sizeof(path)) == sizeof(path) || !*command ||
        !*aes_resource_directory() || (UBYTE)basepage->p_cmdlin[0] > 127) return;
    memcpy(path,command,strlen(command)+1);
    for (i = 0; path[i]; i++) if (path[i] == '/') path[i] = '\\';
    if (!aes_shell_copy(aes_shell_command,aes_resource_directory(),sizeof(aes_shell_command)) ||
        !aes_shell_append(aes_shell_command,aes_shell_name(path),sizeof(aes_shell_command))) return;
    memcpy(aes_shell_tail,basepage->p_cmdlin,sizeof(aes_shell_tail));
    startup_ready = TRUE;
}
void aes_shell_set_dta(void *dta)
{
    /* Fsetdta is void; its getter verifies installation and restoration. */
    (void)aes_gemdos_call(0x1a,(uintptr_t)dta,0,0);
    if (aes_gemdos_call(0x2f,0,0,0) != (uintptr_t)dta) aes_shell->ok = FALSE;
}
WORD aes_shell_first(char *path, WORD attributes)
{
    if (!aes_shell->ok || !*path) return EFILNF;
    return (WORD)aes_gemdos_call(0x4e,(uintptr_t)path,attributes,0);
}
WORD mt_shel_read(char *command, char *tail, WORD *global)
{
    if (!command || !tail || !startup_ready || aes_application_session(global) < 0) return 0;
    aes_shell_read_core(command,tail); return 1;
}
WORD shel_read(char *command, char *tail) { return mt_shel_read(command,tail,aes_global); }
WORD mt_shel_envrn(char **result, const char *prefix, WORD *global)
{
    char *found;
    if (!result || bounded_length(prefix,AES_SHELL_ENV) >= AES_SHELL_ENV ||
        aes_application_session(global) < 0 || !environment_valid()) return 0;
    aes_shell_env_core(&found,prefix); *result = found; return 1;
}
WORD shel_envrn(char **result, const char *prefix) { return mt_shel_envrn(result,prefix,aes_global); }
WORD mt_shel_find(char *path, WORD *global)
{
    SHELL_CONTEXT c = { 0 }, *saved = aes_shell;
    char staged[AES_SHELL_PATH];
    UQUAD dta;
    WORD result;
    if (aes_application_session(global) < 0 || !path || !*path ||
        !aes_shell_copy(staged,path,sizeof(staged)) || !environment_valid()) return 0;
    dta = aes_gemdos_call(0x2f,0,0,0);
    if ((QUAD)dta < 0) return 0;
    c.original_dta = (void *)(uintptr_t)dta; c.ok = TRUE; aes_shell = &c;
    result = aes_shell_find_core(staged);
    /* Retry the original DTA even if a failed search/restore set ok false. */
    aes_shell_set_dta(c.original_dta);
    aes_shell = saved;
    if (!result || !c.ok) return 0;
    strcpy(path,staged); return 1;
}
WORD shel_find(char *path) { return mt_shel_find(path,aes_global); }
