/* Test-only XFS driver installed alongside FAT and virtio-9p.
 * GPL version 2 or later. See doc/license.txt. */
#include "emutos.h"
#include "xfs_pfs.h"
#include "fs.h"
#include "gemerror.h"
#include "string.h"

#define UNUSED(x) (void)(x)
#define EBADARG ERANGE
long _cdecl null_open(FILEPTR *);
long _cdecl null_close(FILEPTR *, int);
long _cdecl null_read(FILEPTR *, char *, long);
long _cdecl null_write(FILEPTR *, const char *, long);
long _cdecl null_lseek(FILEPTR *, long, int);
#include "../tests/xfs/freemint-null.inc"
#undef EBADARG
#undef UNUSED

#define FIXTURE_TRUNC 0x0400

typedef struct {
    const char *name;
    unsigned mode;
    unsigned references;
} XFS_TEST_NODE;

static XFS_TEST_NODE nodes[] = {
    { "", 0040755, 0 }, { "FIXTURE", 0040755, 0 },
    { "DATA.TXT", 0100666, 0 }, { "NULL.TXT", 0100666, 0 }
};
static FILESYS test_driver;
static char data[64];
static long data_size;
static unsigned open_files, open_dirs;
static LONG open_error, close_error;

static XFS_TEST_NODE *test_node(const fcookie *cookie)
{
    return (XFS_TEST_NODE *)(intptr_t)cookie->index;
}

static void test_cookie(fcookie *out, unsigned short drive, XFS_TEST_NODE *node)
{
    out->fs = &test_driver;
    out->dev = drive;
    out->aux = 0;
    out->index = (long)(intptr_t)node;
    node->references++;
}

static long _cdecl test_root(int drive, fcookie *out)
{
    test_cookie(out, drive, &nodes[0]);
    return E_OK;
}

static long _cdecl test_lookup(fcookie *dir, const char *name, fcookie *out)
{
    XFS_TEST_NODE *node = test_node(dir), *target = NULL;
    unsigned i;

    if (!strcmp(name, "."))
        target = node;
    else if (!strcmp(name, ".."))
        target = &nodes[0];
    else if (node == &nodes[0] && !strcmp(name, nodes[1].name))
        target = &nodes[1];
    else if (node == &nodes[1])
        for (i = 2; i < sizeof(nodes) / sizeof(nodes[0]); i++)
            if (!strcmp(name, nodes[i].name))
                target = &nodes[i];
    if (!target)
        return EFILNF;
    test_cookie(out, dir->dev, target);
    return E_OK;
}

static long _cdecl test_dup(fcookie *out, fcookie *cookie)
{
    test_cookie(out, cookie->dev, test_node(cookie));
    return E_OK;
}

static long _cdecl test_release(fcookie *cookie)
{
    XFS_TEST_NODE *node = test_node(cookie);

    if (!node || !node->references)
        return EINTRN;
    node->references--;
    cookie->index = 0;
    return E_OK;
}

static long _cdecl test_stat(fcookie *cookie, XATTR *attr)
{
    const XFS_TEST_NODE *node = test_node(cookie);

    memset(attr, 0, sizeof(*attr));
    attr->mode = node->mode;
    if (node < &nodes[2])
        attr->attr = FA_SUBDIR;
    else if (node == &nodes[2])
        attr->size = data_size;
    return E_OK;
}

static long _cdecl test_open(FILEPTR *file)
{
    if (open_error)
        return open_error;
    if (file->links != 1 || file->devinfo != file->fc.index)
        return EINTRN;
    if (file->flags & FIXTURE_TRUNC)
        data_size = 0;
    open_files++;
    return E_OK;
}

static long _cdecl test_close(FILEPTR *file, int pid)
{
    (void)pid;
    if (!open_files || file->links)
        return EINTRN;
    open_files--;
    return close_error;
}

static long _cdecl test_read(FILEPTR *file, char *buffer, long size)
{
    if (size > data_size - file->pos)
        size = data_size - file->pos;
    if (size <= 0)
        return 0;
    memcpy(buffer, data + file->pos, (size_t)size);
    file->pos += size;
    return size;
}

static long _cdecl test_write(FILEPTR *file, const char *buffer, long size)
{
    if (size > (long)sizeof(data) - file->pos)
        return ERANGE;
    memcpy(data + file->pos, buffer, (size_t)size);
    file->pos += size;
    if (file->pos > data_size)
        data_size = file->pos;
    return size;
}

static long _cdecl test_seek(FILEPTR *file, long offset, int whence)
{
    QUAD target = offset;

    if (whence == 1)
        target += file->pos;
    else if (whence == 2)
        target += data_size;
    if (target < 0 || target > data_size)
        return ERANGE;
    file->pos = (long)target;
    return file->pos;
}

static DEVDRV data_device = {
    .open = test_open, .read = test_read, .write = test_write,
    .lseek = test_seek, .close = test_close
};
static DEVDRV null_device = {
    .open = null_open, .read = null_read, .write = null_write,
    .lseek = null_lseek, .close = null_close
};

static DEVDRV * _cdecl test_getdev(fcookie *cookie, long *special)
{
    *special = cookie->index;
    return test_node(cookie) == &nodes[2] ? &data_device : &null_device;
}

static long _cdecl test_opendir(DIR *dir, int flags)
{
    if (flags != TOS_SEARCH)
        return EINVAL;
    dir->fsstuff[0] = 1;
    open_dirs++;
    return E_OK;
}

static long _cdecl test_readdir(DIR *dir, char *name, int size, fcookie *out)
{
    unsigned index = dir->index;
    XFS_TEST_NODE *node;

    if (!dir->fsstuff[0])
        return EIHNDL;
    if (test_node(&dir->fc) == &nodes[0]) {
        if (index != 0)
            return ENMFIL;
        node = &nodes[1];
    } else {
        if (index >= 2)
            return ENMFIL;
        node = &nodes[2 + index];
    }
    if (strlcpy(name, node->name, size) >= (size_t)size)
        return ERANGE;
    test_cookie(out, dir->fc.dev, node);
    dir->index++;
    return E_OK;
}

static long _cdecl test_closedir(DIR *dir)
{
    if (!dir->fsstuff[0] || !open_dirs)
        return EIHNDL;
    dir->fsstuff[0] = 0;
    open_dirs--;
    return E_OK;
}

static long _cdecl test_clone_dir(DIR *out, const DIR *source)
{
    if (!source->fsstuff[0])
        return EIHNDL;
    out->index = source->index;
    return test_opendir(out, source->flags);
}

static FILESYS test_driver = {
    .root = test_root, .lookup = test_lookup, .getxattr = test_stat,
    .getdev = test_getdev, .release = test_release, .dupcookie = test_dup,
    .opendir = test_opendir, .readdir = test_readdir, .closedir = test_closedir
};
static const struct ptos_xfs_extensions test_extensions = {
    .version = PTOS_XFS_EXT_VERSION, .size = sizeof(test_extensions),
    .clone_dir = test_clone_dir
};

static FILESYS * _cdecl test_init(struct kerinfo *kernel)
{
    void *probe = kernel->kmalloc(16);

    if (!probe)
        return NULL;
    kernel->kfree(probe);
    return &test_driver;
}

LONG xfs_test_init(void);
LONG xfs_test_init(void)
{
    static struct ptos_xfs_mount mount;

    return ptos_xfs_mount_drive('R' - 'A', &mount,
                               ptos_xfs_init_driver(test_init), &test_extensions);
}
