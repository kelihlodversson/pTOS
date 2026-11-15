/* Fault injection through the production resolver and disk guards (#944). */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "string.h"
#include "fs.h"
#include "proc.h"
#include "bdosstub.h"
#include "gemerror.h"
#undef assert
#include <assert.h>

jmp_buf errbuf;
long errcode, rwerr;
int errdrv;
WORD bootdev = 2;
static PD parent;
PD *run = &parent;

static const char config[] = "[binfmt.test]\nmagic=4d46\ninterpreter=A:/INTERP.TOS\n";
static const UBYTE foreign[] = "MFpayload", native[] = "\177ELFpayload";
static struct { const UBYTE *data; long size, at; int kind; BOOL open; } files[16];
static int allocations, handles, interpreter_opens, source_opens, scenario;
static BOOL interrupted, changed;
enum { NORMAL, CLOSE_PROBE, CLOSE_PREFIX, MEDIA_LOOKUP, READ_THEN_CLOSE, CLOSE_FORMAT, CWD_ERROR };

static void disk_error(long code, int drive)
{
    errcode = rwerr = code; errdrv = drive;
    longjmp(errbuf, 1);
}

void mark_bcbs_invalid(int drive)
{
    assert(drive >= 0 && drive < 26);
}

void *kalloc(ULONG size)
{
    void *p = calloc(1, size);
    assert(p); allocations++;
    return p;
}

void kfree(void *p)
{
    if (p) { assert(allocations > 0); allocations--; free(p); }
}

int kcprintf(const char *format, ...)
{
    (void)format;
    return 0;
}

void elf_set_load_into_caller(BOOL enabled) { (void)enabled; }

LONG elf_pgmhdrld(FH handle, PGMHDR01 *header, BOOL *foreign_format)
{
    (void)handle; (void)foreign_format;
    memset(header, 0, sizeof(*header));
    return 0;
}

long xopen(char *path, WORD mode)
{
    const UBYTE *data;
    long size;
    int h, kind;
    assert(mode == 0);
    if (!strcmp(path, "C:\\PTOS.INI")) { data = (const UBYTE *)config; size = sizeof(config) - 1; kind = 0; }
    else if (!strcmp(path, "C:\\APP.TOS") || !strcmp(path, "APP.TOS") ||
             !strcmp(path, "\\APP.TOS") || !strcmp(path, "D:APP.TOS") || !strcmp(path, "D:\\APP.TOS")) {
        data = foreign; size = sizeof(foreign) - 1; kind = 1; source_opens++;
    } else if (!strcmp(path, "A:\\INTERP.TOS")) {
        interpreter_opens++;
        if (scenario == MEDIA_LOOKUP && !changed) disk_error(E_CHNG, 0);
        data = native; size = sizeof(native) - 1; kind = 2;
    } else return EFILNF;
    for (h = 6; h < 16 && files[h].open; h++);
    assert(h < 16);
    files[h].data = data; files[h].size = size; files[h].at = 0;
    files[h].kind = kind; files[h].open = TRUE; handles++;
    return h;
}

long xread(WORD handle, long size, void *buffer)
{
    long n;
    assert(handle >= 6 && handle < 16 && files[handle].open);
    if (scenario == READ_THEN_CLOSE && files[handle].kind == 2 && !interrupted) {
        interrupted = TRUE;
        disk_error(EREADF, 0);
    }
    n = files[handle].size - files[handle].at;
    if (n > size) n = size;
    memcpy(buffer, files[handle].data + files[handle].at, n);
    files[handle].at += n;
    return n;
}

long xclose(WORD handle)
{
    int kind;
    assert(handle >= 6 && handle < 16 && files[handle].open);
    kind = files[handle].kind;
    if (!interrupted && kind == 1 &&
        ((scenario == CLOSE_PROBE && source_opens == 1) ||
         (scenario == CLOSE_PREFIX && source_opens == 2))) {
        interrupted = TRUE;
        disk_error(EREADF, 1);    /* flushing another drive */
    }
    if (scenario == READ_THEN_CLOSE && kind == 2 && interrupted) {
        scenario = NORMAL;
        disk_error(E_CHNG, 1);     /* must not replace the original drive/error */
    }
    files[handle].open = FALSE; handles--;
    return scenario == CLOSE_FORMAT && kind == 1 ? EPLFMT : E_OK;
}

long xlseek(LONG offset, WORD handle, WORD mode)
{
    assert(mode == 0 && handle >= 6 && handle < 16 && files[handle].open);
    files[handle].at = offset;
    return offset;
}

long xgetdir(char *out, WORD drive)
{
    assert(drive == 3 || drive == 4);
    if (scenario == CWD_ERROR) disk_error(E_CHNG, drive - 1);
    strcpy(out, "\\SUB");
    return E_OK;
}

static void reset(int kind)
{
    assert(!allocations && !handles);
    scenario = kind; interrupted = changed = FALSE;
    interpreter_opens = source_opens = 0;
    errcode = rwerr = 0; errdrv = -1;
}

static LONG load_file(const char *name, BOOL qualify, const char *expected)
{
    static char child_environment[BINFMT_ENV];
    BINFMT_LAUNCH launch;
    PGMHDR01 header;
    FH handle;
    LONG rc = binfmt_load_header((char *)name, "", "X=1\0", &header, &handle, &launch, qualify);
    if (!rc) {
        char *path = child_environment + (launch.path - launch.environment);
        memcpy(child_environment, launch.environment, sizeof(child_environment));
        binfmt_release_environment(&launch);
        assert(!strcmp(path, "A:\\INTERP.TOS") && launch.tail && !launch.environment);
        assert(!strcmp(launch.tail + 1, expected));
        assert(xclose(handle) == 0);
    }
    binfmt_release(&launch);
    return rc;
}

static LONG load(void)
{
    return load_file("C:\\APP.TOS", FALSE, "C:\\APP.TOS");
}

static void failure(int kind, long code, int drive, int opens)
{
    reset(kind);
    if (!setjmp(errbuf)) {
        if (kind == CWD_ERROR) (void)load_file("APP.TOS", TRUE, "C:\\SUB\\APP.TOS");
        else (void)load();
        assert(!"disk error must reach the enclosing GEMDOS recovery");
    }
    assert(errcode == code && rwerr == code && errdrv == drive);
    assert(!allocations && !handles && interpreter_opens == opens);
}

int main(void)
{
    parent.p_curdrv = 2;
    binfmt_boot_init();
    reset(NORMAL);
    assert(load() == 0 && interpreter_opens == 1 && !allocations && !handles);
    failure(CLOSE_PROBE, EREADF, 1, 0);
    failure(CLOSE_PREFIX, EREADF, 1, 0);
    failure(READ_THEN_CLOSE, EREADF, 0, 1);
    reset(CLOSE_FORMAT);
    assert(load() == EPLFMT && !interpreter_opens && !allocations && !handles);
    failure(MEDIA_LOOKUP, E_CHNG, 0, 1);
    /* osif() remounts the failing drive before restarting the syscall. */
    changed = TRUE;
    assert(load() == 0 && interpreter_opens == 2 && !allocations && !handles);
    reset(NORMAL);
    assert(load_file("APP.TOS", TRUE, "C:\\SUB\\APP.TOS") == 0);
    assert(load_file("\\APP.TOS", TRUE, "C:\\APP.TOS") == 0);
    assert(load_file("D:APP.TOS", TRUE, "D:\\SUB\\APP.TOS") == 0);
    assert(load_file("D:\\APP.TOS", TRUE, "D:\\APP.TOS") == 0);
    assert(load_file("APP.TOS", FALSE, "APP.TOS") == 0);
    assert(!allocations && !handles);
    failure(CWD_ERROR, E_CHNG, 2, 0);
    puts("binfmt disk cleanup and media-change recovery: PASS");
    return 0;
}
