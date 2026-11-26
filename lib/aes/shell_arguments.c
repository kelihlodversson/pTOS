/* Standard GEM command syntax is parsed once, at the libaes boundary. */
#include "shell_arguments.h"
#include "resource_context.h"
#include "pexecve.h"
#include "../ldso/client/ptos.h"
#include "string.h"

#define FLAT_ENV_LIMIT 32766UL

typedef struct {
    ULONG bytes, count;
} SPLIT;

static BOOL split(const char *text, ULONG length, char *strings,
                  const char **slots, const SPLIT *capacity, SPLIT *result)
{
    ULONG at = 0;

    result->bytes = result->count = 0;
    while (at < length) {
        char quote = 0;
        while (at < length && (text[at] == ' ' || text[at] == '\t')) at++;
        if (at == length) break;
        if (result->count >= PEXECVE_MAX_ENTRIES - 1) return FALSE;
        if (capacity && result->count == capacity->count) return FALSE;
        if (slots) slots[result->count] = strings + result->bytes;
        while (at < length) {
            char c = text[at++];
            if (!c) return FALSE;
            if (quote) {
                if (c == quote) {
                    if (at < length && text[at] == quote) at++;
                    else { quote = 0; continue; }
                }
            } else if (c == '\'' || c == '"') { quote = c; continue; }
            else if (c == ' ' || c == '\t') break;
            if (capacity && result->bytes == capacity->bytes) return FALSE;
            if (strings) strings[result->bytes] = c;
            result->bytes++;
        }
        if (quote) return FALSE;
        if (capacity && result->bytes == capacity->bytes) return FALSE;
        if (strings) strings[result->bytes] = 0;
        result->bytes++;
        result->count++;
    }
    return TRUE;
}

static BOOL flat_environment(const char *environment, ULONG *count)
{
    ULONG pos, length;
    *count = 0;
    if (!environment) return TRUE;
    if (!environment[0] && environment[1]) return FALSE;
    for (length = 0; length+1 < FLAT_ENV_LIMIT; length++)
        if (!environment[length] && !environment[length+1]) break;
    if (length+1 == FLAT_ENV_LIMIT) return FALSE;
    for (pos = 0; environment[pos] && strncmp(environment+pos,"ARGV=",5);
         pos += strlen(environment+pos)+1) {
        if (*count == PEXECVE_MAX_ENTRIES) return FALSE;
        (*count)++;
    }
    return TRUE;
}

const char *const *aes_shell_environ(void)
{
    static const char *const empty[] = { NULL };
    return environ ? (const char *const *)environ : empty;
}

BOOL aes_shell_arguments(const char *path, const char *tail, BOOL parse,
                         const char *environment, SHELL_ARGUMENTS *out, AESGLOBAL *global)
{
    SPLIT measured, written;
    ULONG length = (UBYTE)tail[0], envc, i;
    const char **slots;
    char *strings;

    if (length > 127 || (length == 127 && !parse)) return FALSE;
    if (length == 127) {
        for (length = 0; length < PEXECVE_ARG_MAX && tail[length+1]; length++) ;
        if (length == PEXECVE_ARG_MAX) return FALSE;
    }
    if (!split(tail+1,length,NULL,NULL,NULL,&measured) ||
        !flat_environment(environment,&envc) ||
        measured.count+1+envc > PEXECVE_MAX_ENTRIES) return FALSE;
    slots = aes_temporary_alloc((measured.count+2+envc+1)*sizeof(*slots)+measured.bytes,global);
    if (!slots) return FALSE;
    out->storage = slots;
    strings = (char *)(slots+measured.count+2+envc+1);
    slots[0] = path;
    if (!split(tail+1,length,strings,slots+1,&measured,&written) ||
        written.count != measured.count || written.bytes != measured.bytes) {
        (void)aes_resource_discard(slots);
        out->storage = NULL;
        return FALSE;
    }
    slots[measured.count+1] = NULL;
    out->argv = slots;
    slots += measured.count+2;
    if (environment) {
        for (i = 0; i < envc; i++) {
            slots[i] = environment;
            environment += strlen(environment)+1;
        }
        slots[envc] = NULL;
        out->envp = slots;
    } else {
        out->envp = aes_shell_environ();
    }
    if (!parse) memcpy(out->classic_tail,tail,length+1);
    return TRUE;
}
