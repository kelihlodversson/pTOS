/* Initialize per-context shell metadata from the loader's launch record. */
#include "app_context.h"
#include "transport.h"
#include "resource_context.h"
#include "ptos.h"
#include "launch.h"
#include "gemerror.h"
#include "string.h"

/* Prefer the loader snapshot, captured before executable hooks can change
 * cwd. Without it, AT_EXECFN is a best-effort resource-directory fallback. */
static void capture_directory(AES_SHELL_STATE *state, const DL_LAUNCH *launch)
{
    const char *name = (const char *)(uintptr_t)__ptos_getauxval(31), *p, *base;
    char cwd[AES_SHELL_PATH], directory[AES_SHELL_PATH];
    LONG drive;
    size_t prefix, length, i, at = 0;
    state->directory[0] = 0;
    if (launch) {
        if (launch->ready != 1) return;
        name = launch->command;
        for (i = 0, prefix = 0; i < AES_SHELL_PATH && name[i]; i++)
            if (name[i] == '\\' || name[i] == '/') prefix = i+1;
        if (i == AES_SHELL_PATH || !prefix) return;
        memcpy(state->directory,name,prefix); state->directory[prefix] = 0;
        return;
    }
    if (!name) return;
    length = strlen(name);
    if (length >= AES_SHELL_PATH) return;
    base = name;
    for (p = name; *p; p++) if (*p == '\\' || *p == '/' || *p == ':') base = p+1;
    prefix = base-name;
    drive = (LONG)aes_gemdos_call(0x19,0,0,0);
    if (length >= 2 && name[1] == ':') {
        drive = (name[0] >= 'a' && name[0] <= 'z' ? name[0]-'a' : name[0]-'A');
        name += 2; prefix = prefix >= 2 ? prefix-2 : 0;
    }
    if (drive < 0 || drive > 25) return;
    directory[at++] = 'A'+drive; directory[at++] = ':';
    if (*name != '\\' && *name != '/') {
        if ((LONG)aes_gemdos_call(0x47,(uintptr_t)cwd,drive+1,0) != E_OK) return;
        length = strlen(cwd);
        if (length+at+prefix+2 >= AES_SHELL_PATH) return;
        if (!length || (cwd[0] != '\\' && cwd[0] != '/')) directory[at++] = '\\';
        for (i = 0; i < length; i++) directory[at++] = cwd[i] == '/' ? '\\' : cwd[i];
        if (directory[at-1] != '\\') directory[at++] = '\\';
    }
    if (at+prefix >= AES_SHELL_PATH) return;
    for (i = 0; i < prefix; i++) directory[at++] = name[i] == '/' ? '\\' : name[i];
    directory[at] = 0;
    memcpy(state->directory,directory,at+1);
}

static void capture_shell(AES_SHELL_STATE *state)
{
    const DL_LAUNCH *launch = (const DL_LAUNCH *)(uintptr_t)__ptos_getauxval(DL_AT_PTOS_LAUNCH);
    ULONG length;
    /* AT_PAGESZ is mandatory in the pTOS startup vector. libptos exposes no
     * auxiliary values before its initializer, which runs after executable
     * preinitializers. Leave capture pending until that dependency is ready. */
    if (!__ptos_getauxval(6)) return;
    state->capture_pending = FALSE;
    capture_directory(state,launch);
    state->environment = launch && launch->environment ?
        (char *)(uintptr_t)launch->environment : state->empty_environment;
    state->environment_bytes = launch && launch->environment ? launch->environment_size : sizeof(state->empty_environment);
    if (!launch || launch->ready != 1 || !*launch->command || launch->tail[0] > 127) return;
    for (length = 0; length < sizeof(state->command) && launch->command[length]; length++) ;
    if (length == sizeof(state->command)) return;
    memcpy(state->command,launch->command,length+1);
    memcpy(state->tail,launch->tail,sizeof(state->tail));
    state->startup_ready = TRUE;
}

void aes_shell_init(AES_APP_CONTEXT *context)
{
    AES_SHELL_STATE *state = &context->shell;
    memset(state,0,sizeof(*state));
    state->environment = state->empty_environment;
    state->environment_bytes = sizeof(state->empty_environment);
    state->capture_pending = TRUE;
    capture_shell(state);
}

AES_SHELL_STATE *aes_shell_state(AESGLOBAL *global)
{
    AES_SHELL_STATE *state = &aes_app_context(global)->shell;
    if (state->capture_pending) capture_shell(state);
    return state;
}

void aes_shell_reset(AESGLOBAL *global)
{
    AES_SHELL_STATE *state = &aes_app_context(global)->shell;
    SHELL_SEARCH *search = state->search.next, *next;
    state->search.next = NULL;
    while (search) {
        next = search->next;
        (void)aes_resource_discard(search);
        search = next;
    }
}
