/* Standard GEM desktop-default and clipboard metadata, shared across clients.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gem.h"
#include "application.h"
#include "transport.h"
#include "gemerror.h"
#include "string.h"

/* Classic AES bounds include the terminating NUL. The shell buffer uses slot 0.
 * Keep the default command/directory together so readers cannot see a mixture. */
#define DEFAULT_CONTEXT_SLOT 1
#define SCRAP_CONTEXT_SLOT 2
#define DEFAULT_COMMAND_BYTES 13
#define DEFAULT_DIRECTORY_BYTES 114
#define SCRAP_PATH_BYTES 128
typedef struct {
    char command[DEFAULT_COMMAND_BYTES];
    char directory[DEFAULT_DIRECTORY_BYTES];
} DEFAULT_DESKTOP;

static ULONG string_bytes(const char *text, ULONG capacity)
{
    ULONG i;
    if (text)
        for (i = 0; i < capacity; i++)
            if (!text[i]) return i+1;
    return 0;
}

static BOOL copy_string(char *dest, const char *source, ULONG capacity)
{
    ULONG bytes = string_bytes(source,capacity);
    if (!bytes) return FALSE;
    memcpy(dest,source,bytes);
    return TRUE;
}

static BOOL context_transfer(LONG session, ULONG slot, void *data, ULONG bytes, BOOL write)
{
    LONG size;
    BOOL ok;
    if (session < 0 || cuis_call6(CUIS_DISPLAY_LOCK,session,1,1,0,0,0) != E_OK) return FALSE;
    if (write) {
        ok = cuis_call6(CUIS_CONTEXT,session,slot,CUIS_CONTEXT_WRITE,(uintptr_t)data,bytes,0) == E_OK;
    } else {
        /* An unconfigured record is empty. Ignore surplus bytes after a valid
         * fixed prefix: opaque context storage grows but does not shrink. */
        size = cuis_call6(CUIS_CONTEXT,session,slot,CUIS_CONTEXT_SIZE,0,0,0);
        ok = size == 0 || (size >= (LONG)bytes && (ULONG)size <= CUIS_CONTEXT_BYTES);
        if (ok && size)
            ok = cuis_call6(CUIS_CONTEXT,session,slot,CUIS_CONTEXT_READ,(uintptr_t)data,bytes,0) == E_OK;
    }
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,0,0,0,0,0) != E_OK) {
        /* A failed release leaves nesting unchanged; retry cleanup once. */
        (void)cuis_call6(CUIS_DISPLAY_LOCK,session,0,0,0,0,0);
        ok = FALSE;
    }
    return ok;
}

void mt_shel_rdef(char *command, char *directory, short *global)
{
    DEFAULT_DESKTOP staged = { { 0 }, { 0 } };
    if (!command || !directory ||
        !context_transfer(aes_application_session(global),DEFAULT_CONTEXT_SLOT,&staged,sizeof(staged),FALSE) ||
        !string_bytes(staged.command,sizeof(staged.command)) ||
        !string_bytes(staged.directory,sizeof(staged.directory))) return;
    strcpy(command,staged.command);
    strcpy(directory,staged.directory);
}
void shel_rdef(char *command, char *directory) { mt_shel_rdef(command,directory,aes_global); }

void mt_shel_wdef(const char *command, const char *directory, short *global)
{
    DEFAULT_DESKTOP staged = { { 0 }, { 0 } };
    LONG session = aes_application_session(global);
    if (session < 0 || !copy_string(staged.command,command,sizeof(staged.command)) ||
        !copy_string(staged.directory,directory,sizeof(staged.directory))) return;
    (void)context_transfer(session,DEFAULT_CONTEXT_SLOT,&staged,sizeof(staged),TRUE);
}
void shel_wdef(const char *command, const char *directory) { mt_shel_wdef(command,directory,aes_global); }

short mt_scrp_read(char *path, short *global)
{
    char staged[SCRAP_PATH_BYTES] = { 0 };
    if (!path || !context_transfer(aes_application_session(global),SCRAP_CONTEXT_SLOT,staged,sizeof(staged),FALSE) ||
        !string_bytes(staged,sizeof(staged))) return 0;
    strcpy(path,staged);
    /* Atari TOS succeeds even for an empty path and never probes the disk. */
    return 1;
}
short scrp_read(char *path) { return mt_scrp_read(path,aes_global); }

short mt_scrp_write(const char *path, short *global)
{
    char staged[SCRAP_PATH_BYTES] = { 0 };
    LONG session = aes_application_session(global);
    if (session < 0 || !copy_string(staged,path,sizeof(staged))) return 0;
    return context_transfer(session,SCRAP_CONTEXT_SLOT,staged,sizeof(staged),TRUE) ? 1 : 0;
}
short scrp_write(const char *path) { return mt_scrp_write(path,aes_global); }
