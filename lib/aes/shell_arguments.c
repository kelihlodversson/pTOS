/* Standard AES4 command parsing and process-local environment staging. */
#include "shell_arguments.h"
#include "shell_context.h"
#include "resource_context.h"
#include "string.h"
#define ENV_LIMIT 32766UL
typedef struct { ULONG bytes, nulls, count; } SPLIT;

static BOOL put(char *out, ULONG *at, ULONG capacity, char value)
{
    if (*at >= capacity) return FALSE;
    if (out) out[*at] = value;
    (*at)++;
    return TRUE;
}
static BOOL number(char *out, ULONG *at, ULONG capacity, ULONG value)
{
    char digits[10];
    ULONG n = 0;
    do { digits[n++] = '0'+value%10; value /= 10; } while (value);
    while (n) if (!put(out,at,capacity,digits[--n])) return FALSE;
    return TRUE;
}
static BOOL split(const char *text, ULONG length, char *args, ULONG capacity,
                   char *nulls, ULONG null_capacity, SPLIT *result)
{
    ULONG at = 0, begin;
    char quote, c;
    memset(result,0,sizeof(*result));
    while (at < length) {
        while (at < length && (text[at] == ' ' || text[at] == '\t')) at++;
        if (at == length) break;
        quote = 0; begin = result->bytes;
        while (at < length) {
            c = text[at++];
            if (!c) return FALSE;
            if (quote) {
                if (c == quote) {
                    if (at < length && text[at] == quote) at++;
                    else { quote = 0; continue; }
                }
            } else if (c == '\'' || c == '"') { quote = c; continue; }
            else if (c == ' ' || c == '\t') break;
            if (!put(args,&result->bytes,capacity,c)) return FALSE;
        }
        if (quote) return FALSE;
        if (begin == result->bytes) {
            if ((result->nulls && !put(nulls,&result->nulls,null_capacity,',')) ||
                !number(nulls,&result->nulls,null_capacity,result->count+1) ||
                !put(args,&result->bytes,capacity,' ')) return FALSE;
        }
        if (!put(args,&result->bytes,capacity,0)) return FALSE;
        result->count++;
    }
    return TRUE;
}

BOOL aes_shell_arguments(const char *path, const char *tail, BOOL parse,
                         const char *environment, ULONG limit, SHELL_ARGUMENTS *out)
{
    ULONG size, prefix, at, n, command, header, null_at, arg_at;
    SPLIT measured, written;
    char *staged;
    memset(out,0,sizeof(*out));
    if (!tail || (UBYTE)tail[0] > 127) return FALSE;
    n = (UBYTE)tail[0];
    if (!parse) memcpy(out->tail,tail,n == 127 ? 1 : n+1);
    if (!parse && !environment) return n != 127;
    if (!environment || limit < 2) return FALSE;
    if (!environment[0] && environment[1]) return FALSE;
    if (limit > ENV_LIMIT) limit = ENV_LIMIT;
    for (size = 0; size+1 < limit; size++)
        if (!environment[size] && !environment[size+1]) break;
    if (size+1 == limit) return FALSE;
    size += 2;
    prefix = 0;
    while (prefix < size && environment[prefix]) {
        if (!strncmp(environment+prefix,"ARGV=",5)) break;
        prefix += strlen(environment+prefix)+1;
    }
    if (!parse) {
        if (n == 127 && (prefix == size-1 || !environment[prefix] ||
            !environment[prefix+strlen(environment+prefix)+1])) return FALSE;
        staged = aes_temporary_alloc(size+1);
        if (!staged) return FALSE;
        memcpy(staged,environment,size); staged[size] = 0;
        out->environment = staged; return TRUE;
    }
    if (n == 127) {
        for (n = 0; n < ENV_LIMIT && tail[n+1]; n++) ;
        if (n == ENV_LIMIT) return FALSE;
    }
    if (!split(tail+1,n,NULL,ENV_LIMIT,NULL,ENV_LIMIT,&measured)) return FALSE;
    header = measured.nulls ? 10+measured.nulls : 5;
    command = strlen(path)+1;
    if (prefix+header+1+command+measured.bytes+1 > ENV_LIMIT) return FALSE;
    staged = aes_temporary_alloc(prefix+header+1+command+measured.bytes+2);
    if (!staged) return FALSE;
    memcpy(staged,environment,prefix);
    memcpy(staged+prefix,measured.nulls ? "ARGV=NULL:" : "ARGV=",measured.nulls ? 10 : 5);
    null_at = prefix+(measured.nulls ? 10 : 5);
    at = prefix+header; staged[at++] = 0;
    memcpy(staged+at,path,command); at += command; arg_at = at;
    if (!split(tail+1,n,staged+arg_at,measured.bytes,staged+null_at,measured.nulls,&written) ||
        written.bytes != measured.bytes || written.nulls != measured.nulls || written.count != measured.count) {
        (void)aes_resource_discard(staged); return FALSE;
    }
    staged[arg_at+written.bytes] = staged[arg_at+written.bytes+1] = 0;
    out->tail[0] = 127; out->environment = staged;
    return TRUE;
}
