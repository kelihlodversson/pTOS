/* Standard GEM shell startup/environment/file lookup, without shell services.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gem.h"
#include "shell_context.h"
#include "resource_context.h"
#include "application.h"
#include "transport.h"
#include "gemerror.h"
#include "ptos.h"
#include "launch.h"
#include "string.h"

SHELL_CONTEXT *aes_shell;
DTA aes_shell_dta;
char aes_shell_command[AES_SHELL_PATH], aes_shell_tail[128];
char *aes_shell_environment;
static BOOL startup_ready;
static ULONG environment_bytes;
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
    if (!aes_shell_environment || environment_bytes < 2 || environment_bytes > AES_SHELL_ENV) return FALSE;
    for (i = 0; i+1 < environment_bytes; i++)
        if (!aes_shell_environment[i] && !aes_shell_environment[i+1]) return TRUE;
    return FALSE;
}

/* The interpreter completed the optional launch record before executable
 * preinitializers. DT_INIT only consumes that saved state. */
void aes_library_init(void)
{
    const DL_LAUNCH *launch = (const DL_LAUNCH *)(uintptr_t)__ptos_getauxval(DL_AT_PTOS_LAUNCH);
    aes_resources_init();
    startup_ready = FALSE; aes_shell_command[0] = aes_shell_tail[0] = 0;
    aes_shell_environment = launch && launch->environment ?
        (char *)(uintptr_t)launch->environment : empty_environment;
    environment_bytes = launch && launch->environment ? launch->environment_size : sizeof(empty_environment);
    if (!launch || launch->ready != 1 || !*launch->command ||
        launch->tail[0] > 127 || !aes_shell_copy(aes_shell_command,launch->command,sizeof(aes_shell_command))) return;
    memcpy(aes_shell_tail,launch->tail,sizeof(aes_shell_tail));
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
    LONG result;
    if (!aes_shell->ok || !*path) return EFILNF;
    result = (LONG)aes_gemdos_call(0x4e,(uintptr_t)path,attributes,0);
    if (result < 0 && result != EFILNF && result != EPTHNF && result != ENMFIL) aes_shell->ok = FALSE;
    return (WORD)result;
}
WORD mt_shel_read(char *command, char *tail, WORD *global)
{
    if (!command || !tail || !startup_ready || aes_application_session(global) < 0) return 0;
    aes_shell_read_core(command,tail); return 1;
}
WORD shel_read(char *command, char *tail) { return mt_shel_read(command,tail,aes_global); }
static BOOL gem_application(const char *path)
{
    ULONG length = strlen(path), i;
    char extension[4];
    if (length < 4 || path[length-4] != '.') return FALSE;
    for (i = 0; i < 3; i++) {
        char c = path[length-3+i];
        extension[i] = c >= 'a' && c <= 'z' ? c-'a'+'A' : c;
    }
    extension[3] = 0;
    return !strcmp(extension,"PRG") || !strcmp(extension,"APP");
}
WORD mt_shel_write(WORD mode, WORD graphical, WORD execution, const void *command, const char *tail, WORD *global)
{
    char path[AES_SHELL_PATH], staged_tail[128] = { 0 };
    LONG pid, session, watch = 0, image;
    ULONG environment;
    if (mode != SWM_LAUNCH && mode != SWM_LAUNCHNOW) return 0;
    if (mode == SWM_LAUNCH ? execution != CL_NORMAL : graphical != GEMAPP || execution != SHW_PARALLEL) return 0;
    session = aes_application_session(global);
    if (session < 0 || !tail || (UBYTE)tail[0] >= 127 ||
        !aes_shell_copy(path,command,sizeof(path)) || !*path) return 0;
    /* Read only the length byte/payload; 127 denotes extended ARGV passing. */
    memcpy(staged_tail,tail,(UBYTE)tail[0]+1);
    if (!mt_shel_find(path,global)) return 0;
    if (mode == SWM_LAUNCH) {
        if (!gem_application(path) || aes_child_messages(session) < 0) return 0;
        watch = cuis_call6(CUIS_CHILD_WATCH,session,CUIS_CHILD_RESERVE,0,0,0,0);
        if (watch <= 0) return 0;
    }
    /* Load and bind before the child can run. A native wait, including one in
     * a reentrant callback, can never remove an unobserved completion. */
    if (watch) {
        image = (LONG)aes_gemdos_call4(0x4b,PE_LOAD,(uintptr_t)path,(uintptr_t)staged_tail,0);
        if (image <= 0) {
            (void)cuis_call6(CUIS_CHILD_WATCH,session,CUIS_CHILD_CANCEL,watch,0,0,0);
            return 0;
        }
        environment = (ULONG)(uintptr_t)((const PD *)(uintptr_t)(ULONG)image)->p_env;
        pid = cuis_call6(CUIS_CHILD_WATCH,session,CUIS_CHILD_BIND,watch,(ULONG)image,0,0);
        if (pid == E_OK) pid = (LONG)aes_gemdos_call4(0x4b,PE_ASYNC_GO_FREE,0,(ULONG)image,0);
        if (pid <= 0 || pid > 32767) {
            (void)cuis_call6(CUIS_CHILD_WATCH,session,CUIS_CHILD_CANCEL,watch,0,0,0);
            (void)aes_gemdos_call(0x49,(ULONG)image,0,0);
            (void)aes_gemdos_call(0x49,environment,0,0);
            return 0;
        }
    } else {
        /* Parallel mode needs no observer and inherits environment/files. */
        pid = (LONG)aes_gemdos_call4(0x4b,PE_ASYNC_LOADGO,(uintptr_t)path,(uintptr_t)staged_tail,0);
    }
    return pid > 0 && pid <= 32767 ? (WORD)pid : 0;
}
WORD shel_write(WORD mode, WORD graphical, WORD execution, const void *command, const char *tail)
{ return mt_shel_write(mode,graphical,execution,command,tail,aes_global); }
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
