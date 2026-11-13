/* Standard GEM shell startup/environment/file lookup, without shell services.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "libaes.h"
#include "shell_context.h"
#include "resource_context.h"
#include "application.h"
#include "app_context.h"
#include "transport.h"
#include "gemerror.h"
#include "lifecycle.h"
#include "shell_arguments.h"
#include "string.h"

SHELL_CONTEXT *aes_shell;
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
static BOOL environment_valid(const AES_SHELL_STATE *state)
{
    ULONG i;
    if (!state->environment || state->environment_bytes < 2 || state->environment_bytes > AES_SHELL_ENV) return FALSE;
    for (i = 0; i+1 < state->environment_bytes; i++)
        if (!state->environment[i] && !state->environment[i+1]) return TRUE;
    return FALSE;
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
WORD mt_shel_read(char *command, char *tail, AESGLOBAL *global)
{
    SHELL_CONTEXT c = { 0 }, *saved = aes_shell;
    if (!command || !tail || aes_application_session(global) < 0) return 0;
    c.state = &aes_app_context(global)->shell;
    if (!c.state->startup_ready) return 0;
    aes_shell = &c;
    aes_shell_read_core(command,tail);
    aes_shell = saved; return 1;
}

static WORD application_kind(const char *path)
{
    ULONG length = strlen(path), i;
    char extension[4];
    if (length < 4 || path[length-4] != '.') return -1;
    for (i = 0; i < 3; i++) {
        char c = path[length-3+i];
        extension[i] = c >= 'a' && c <= 'z' ? c-'a'+'A' : c;
    }
    extension[3] = 0;
    if (!strcmp(extension,"PRG") || !strcmp(extension,"APP") || !strcmp(extension,"GTP") || !strcmp(extension,"ACC")) return GEMAPP;
    if (!strcmp(extension,"TOS") || !strcmp(extension,"TTP")) return TOSAPP;
    return -1;
}
/* Resolve against the caller before choosing a different child directory.
 * Absolute executable metadata also keeps interpreter/resource startup from
 * interpreting a relative command a second time in the child's new cwd. */
static BOOL shell_absolute(char *result, const char *source)
{
    char current[AES_SHELL_PATH];
    LONG drive, status;
    ULONG i;
    if (!source || !*source) return FALSE;
    if (source[1] == ':') {
        char letter = source[0];
        if (letter >= 'a' && letter <= 'z') letter -= 'a'-'A';
        drive = letter-'A'; source += 2;
    } else drive = (LONG)aes_gemdos_call(0x19,0,0,0);
    if (drive < 0 || drive >= 26) return FALSE;
    result[0] = 'A'+drive; result[1] = ':'; result[2] = '\\'; result[3] = 0;
    if (*source != '\\' && *source != '/') {
        status = (LONG)aes_gemdos_call(0x47,(uintptr_t)current,drive+1,0);
        if (status != E_OK) return FALSE;
        if (*current && !aes_shell_append(result,current+(*current == '\\'),AES_SHELL_PATH)) return FALSE;
        i = strlen(result);
        if (result[i-1] != '\\' && !aes_shell_append(result,"\\",AES_SHELL_PATH)) return FALSE;
    } else source++;
    if (!aes_shell_append(result,source,AES_SHELL_PATH)) return FALSE;
    for (i = 2; result[i]; i++) if (result[i] == '/') result[i] = '\\';
    return TRUE;
}
WORD mt_shel_write(WORD mode, WORD graphical, WORD execution, const void *command, const char *tail, AESGLOBAL *global)
{
    char path[AES_SHELL_PATH], absolute[AES_SHELL_PATH], directory[AES_SHELL_PATH];
    const char *requested_directory = NULL;
    BOOL set_directory;
    SHELL_ARGUMENTS arguments;
    const char *environment = NULL;
    LONG pid = 0, session, watch = 0;
    UWORD extensions = (UWORD)mode & 0xff00;
    BOOL parse;
    AES_SHELL_STATE *state;
    WORD kind = GEMAPP;
    mode = (UWORD)mode & 255;
    if (extensions & ~(SW_ENVIRON|SW_DEFDIR)) return 0;
    if (mode == SWM_SHUTDOWN || mode == SWM_REZCHANGE || mode == SWM_NEWMSG || mode == SWM_AESMSG)
        return extensions ? 0 : aes_lifecycle_write(mode,graphical,execution,command,global);
    if (mode != SWM_LAUNCH && mode != SWM_LAUNCHNOW) return 0;
    if (mode == SWM_LAUNCH ? (execution != CL_NORMAL && execution != CL_PARSE) :
        (graphical != GEMAPP && graphical != TOSAPP) ||
        (execution != SHW_IMMED && execution != SHW_PARALLEL && execution != SHW_CHAIN) ||
        (execution == SHW_CHAIN && graphical == TOSAPP)) return 0;
    session = aes_application_session(global);
    if (session < 0 || !command || !tail || (UBYTE)tail[0] > 127) return 0;
    state = &aes_app_context(global)->shell;
    if (extensions) {
        const SHELW *extended = command;
        if (extensions & SW_ENVIRON) environment = extended->env;
        if (extensions & SW_DEFDIR) requested_directory = extended->defdir;
        command = extended->newcmd;
    }
    if (!aes_shell_copy(path,command,sizeof(path)) || !*path) return 0;
    if (!mt_shel_find(path,global)) return 0;
    set_directory = (extensions & SW_DEFDIR) || mode == SWM_LAUNCH || execution == SHW_IMMED;
    if (set_directory) {
        ULONG end;
        if (!shell_absolute(absolute,path)) return 0;
        strcpy(path,absolute);
        if (requested_directory) {
            if (!shell_absolute(directory,requested_directory)) return 0;
        } else {
            strcpy(directory,path);
            end = strlen(directory);
            while (end > 3 && directory[end-1] != '\\') end--;
            directory[end] = 0;
        }
    }
    parse = mode == SWM_LAUNCH && execution == CL_PARSE;
    if (!aes_shell_arguments(path,tail,parse,
            environment ? environment : parse || (UBYTE)tail[0] == 127 ? state->environment : NULL,
            environment ? 32766UL : state->environment_bytes,&arguments,global)) return 0;
    if (mode == SWM_LAUNCHNOW && execution == SHW_CHAIN) {
        pid = cuis_call6(set_directory ? CUIS_PROCESS_DEFER_DIRECTORY : CUIS_PROCESS_DEFER,session,(uintptr_t)path,
            (uintptr_t)arguments.tail,(uintptr_t)arguments.environment,set_directory ? (uintptr_t)directory : 0,0) == E_OK ? 1 : 0;
        goto finished;
    }
    kind = mode == SWM_LAUNCH ? application_kind(path) : graphical;
    if (kind < 0) goto finished;
    if (mode == SWM_LAUNCH || execution == SHW_IMMED) {
        if (aes_child_messages(session) < 0) goto finished;
        watch = cuis_call6(CUIS_CHILD_WATCH,session,CUIS_CHILD_RESERVE,0,0,0,0);
        if (watch <= 0) goto finished;
    }
    if (set_directory) {
        pid = cuis_call6(kind == TOSAPP ? CUIS_CONSOLE_LAUNCH_DIRECTORY : CUIS_CHILD_LAUNCH_DIRECTORY,
            session,watch,(uintptr_t)path,(uintptr_t)arguments.tail,(uintptr_t)arguments.environment,(uintptr_t)directory);
    } else if (watch || kind == TOSAPP) {
        /* The ordinary fixed-address loader installs the observer at spawn,
         * atomically with launch, before the child can execute or be reaped. */
        pid = cuis_call6(CUIS_CHILD_LAUNCH,session,watch,(uintptr_t)path,(uintptr_t)arguments.tail,(uintptr_t)arguments.environment,kind == TOSAPP);
    } else {
        pid = (LONG)aes_gemdos_call4(0x4b,PE_ASYNC_LOADGO,(uintptr_t)path,(uintptr_t)arguments.tail,(uintptr_t)arguments.environment);
    }
finished:
    if (arguments.environment) (void)aes_resource_discard(arguments.environment);
    return pid > 0 && pid <= 32767 ? (WORD)pid : 0;
}

WORD mt_shel_envrn(char **result, const char *prefix, AESGLOBAL *global)
{
    char *found;
    SHELL_CONTEXT c = { 0 }, *saved = aes_shell;
    if (!result || bounded_length(prefix,AES_SHELL_ENV) >= AES_SHELL_ENV ||
        aes_application_session(global) < 0) return 0;
    c.state = &aes_app_context(global)->shell;
    if (!environment_valid(c.state)) return 0;
    aes_shell = &c;
    aes_shell_env_core(&found,prefix);
    aes_shell = saved; *result = found; return 1;
}

/* Kernel directory-search caches are keyed by DTA address. Reuse one stable
 * slot per simultaneously active lookup; varying caller stack depths must
 * not evict the application's own Fsfirst/Fsnext search. */
static SHELL_SEARCH *acquire_search(AESGLOBAL *global)
{
    SHELL_SEARCH *search = &aes_app_context(global)->shell.search;
    for (;;) {
        if (!search->busy) { search->busy = TRUE; return search; }
        if (!search->next) {
            search->next = aes_temporary_alloc(sizeof(*search),global);
            if (!search->next) return NULL;
            memset(search->next,0,sizeof(*search));
        }
        search = search->next;
    }
}

WORD mt_shel_find(char *path, AESGLOBAL *global)
{
    SHELL_CONTEXT c = { 0 }, *saved = aes_shell;
    char staged[AES_SHELL_PATH];
    UQUAD dta;
    WORD result;
    SHELL_SEARCH *search;
    if (aes_application_session(global) < 0 || !path || !*path ||
        !aes_shell_copy(staged,path,sizeof(staged))) return 0;
    c.state = &aes_app_context(global)->shell;
    if (!environment_valid(c.state)) return 0;
    dta = aes_gemdos_call(0x2f,0,0,0);
    if ((QUAD)dta < 0) return 0;
    search = acquire_search(global);
    if (!search) return 0;
    c.dta = &search->dta;
    c.original_dta = (void *)(uintptr_t)dta; c.ok = TRUE; aes_shell = &c;
    result = aes_shell_find_core(staged);
    /* Retry the original DTA even if a failed search/restore set ok false. */
    aes_shell_set_dta(c.original_dta);
    aes_shell = saved; search->busy = FALSE;
    if (!result || !c.ok) return 0;
    strcpy(path,staged); return 1;
}
