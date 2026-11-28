/* Bridge built-in FreeMiNT XFS source drivers through the existing PFS/SFT.
 * GPL version 2 or later. See doc/license.txt. */
#include "emutos.h"
#include "xfs_pfs.h"
#include "bdosstub.h"
#include "fs.h"
#include "gemerror.h"
#include "kheap.h"
#include "kproc.h"
#include "mem.h"
#include "string.h"

/* One independently owned cookie per directory owner. The search's cookie
 * also owns its DIR, so an active cursor never gets copied with a PFS token. */
typedef struct {
    DIR dir;
    LONG position;
    BOOL opened;
} XFS_DIRECTORY;

#if !CONF_WITH_KHEAP
typedef char XFS_DIRECTORY_fits_osmem[(sizeof(XFS_DIRECTORY) <= OSM_PAYLOAD_BYTES) ? 1 : -1];
typedef char XFS_FILEPTR_fits_osmem[(sizeof(FILEPTR) <= OSM_PAYLOAD_BYTES) ? 1 : -1];
#endif

static LONG xfs_result(long result)
{
    return result < INT32_MIN || result > INT32_MAX ? ERANGE : (LONG)result;
}

#if CONF_WITH_MFASTCALL
/* The m68k SDK reads indirect cdecl pointer results in A0 in a
 * fastcall function. The XFS stack ABI returns them in D0. */
FILESYS *ptos_xfs_call_init(PTOS_XFS_INIT init, struct kerinfo *services);
DEVDRV *ptos_xfs_call_getdev(DEVDRV * _cdecl (*getdev)(fcookie *, long *),
                            fcookie *cookie, long *special);
void * _cdecl ptos_xfs_cdecl_kmalloc(ulong bytes);
void _cdecl ptos_xfs_cdecl_kfree(void *address);
void *ptos_xfs_alloc(ulong bytes);
void ptos_xfs_free(void *address);
#else
static
#endif
void *ptos_xfs_alloc(ulong bytes)
{
#if CONF_WITH_KHEAP
    return bytes <= UINT32_MAX ? kalloc((ULONG)bytes) : NULL;
#else
    return bytes && bytes <= OSM_PAYLOAD_BYTES ? xmgetblk(MEMTYPE_XFS) : NULL;
#endif
}

#if !CONF_WITH_MFASTCALL
static
#endif
void ptos_xfs_free(void *address)
{
#if CONF_WITH_KHEAP
    kfree(address);
#else
    if (address)
        xmfreblk(address);
#endif
}

FILESYS *ptos_xfs_init_driver(PTOS_XFS_INIT init)
{
    static struct kerinfo services = {
        .default_perm = 0666,
#if CONF_WITH_MFASTCALL
        .kmalloc = ptos_xfs_cdecl_kmalloc,
        .kfree = ptos_xfs_cdecl_kfree
#else
        .kmalloc = ptos_xfs_alloc,
        .kfree = ptos_xfs_free
#endif
    };

#if CONF_WITH_MFASTCALL
    return init ? ptos_xfs_call_init(init, &services) : NULL;
#else
    return init ? init(&services) : NULL;
#endif
}

static const struct ptos_xfs_mount *xfs_mount(const struct pfs_ops *ops)
{
    /* Mount ops is the first member, and only installed for XFS drives. */
    return (const struct ptos_xfs_mount *)ops;
}

static UWORD xfs_extension_flags(const struct ptos_xfs_extensions *ext)
{
    return ext && ext->size >= offsetof(struct ptos_xfs_extensions, flags) + sizeof(ext->flags)
        ? ext->flags : 0;
}

static UWORD xfs_flags(const struct pfs_ops *ops)
{
    return xfs_extension_flags(xfs_mount(ops)->extensions);
}

static fcookie *xfs_dir_cookie(const PFSCOOKIE *cookie)
{
    XFS_DIRECTORY *directory = (XFS_DIRECTORY *)cookie->index;

    return &directory->dir.fc;
}

static void xfs_publish(PFSCOOKIE *out, struct pfs_ops *ops, void *object)
{
    out->fs = ops;
    out->index = (intptr_t)object;
    out->aux = 0;
    out->pos = 0;
}

static LONG xfs_release(fcookie *cookie)
{
    return cookie->fs && cookie->fs->release ? xfs_result(cookie->fs->release(cookie)) : E_OK;
}

static LONG xfs_validate(FILESYS *driver, fcookie *cookie)
{
    if (cookie->fs == driver)
        return E_OK;
    xfs_release(cookie);
    return ENSAME;
}

static LONG xfs_dup(fcookie *source, fcookie *target)
{
    FILESYS *driver = source->fs;
    LONG rc;

    if (!driver->dupcookie) {
        *target = *source; /* Admitted only when there is no release right. */
        return E_OK;
    }
    rc = xfs_result(driver->dupcookie(target, source));
    return rc < 0 ? rc : xfs_validate(driver, target);
}

static LONG xfs_root(struct pfs_ops *ops, WORD drive, PFSCOOKIE *out)
{
    FILESYS *driver = xfs_mount(ops)->driver;
    XFS_DIRECTORY *directory;
    LONG rc;

    directory = ptos_xfs_alloc(sizeof(*directory));
    if (!directory)
        return ENSMEM;
    memset(directory, 0, sizeof(*directory));
    rc = xfs_result(driver->root(drive, &directory->dir.fc));
    if (rc >= 0)
        rc = xfs_validate(driver, &directory->dir.fc);
    if (rc < 0)
        ptos_xfs_free(directory);
    else
        xfs_publish(out, ops, directory);
    return rc;
}

static LONG xfs_lookup_component(fcookie *dir, const char *name, fcookie *out)
{
    LONG rc = xfs_result(dir->fs->lookup(dir, name, out));

    return rc < 0 ? rc : xfs_validate(dir->fs, out);
}

static LONG xfs_descend(fcookie *start, const char *path, fcookie *out)
{
    char name[LEN_ZPATH];
    fcookie current = {0};
    LONG rc;

    if (!*path)
        return xfs_dup(start, out);
    while (*path) {
        size_t length = 0;
        fcookie child = {0};
        XATTR attr;

        while (*path == SLASH || *path == '/')
            path++;
        if (!*path)
            break;
        while (*path && *path != SLASH && *path != '/') {
            if (length == sizeof(name) - 1) {
                xfs_release(&current);
                return ERANGE;
            }
            name[length++] = *path++;
        }
        name[length] = '\0';
        rc = xfs_lookup_component(current.fs ? &current : start, name, &child);
        xfs_release(&current);
        if (rc < 0)
            return rc == EFILNF ? EPTHNF : rc;

        rc = xfs_result(child.fs->getxattr(&child, &attr));
        if (rc < 0 || (attr.mode & XFS_IFMT) != XFS_IFDIR) {
            xfs_release(&child);
            return rc < 0 ? rc : EPTHNF;
        }
        current = child;
    }
    if (!current.fs)
        return xfs_dup(start, out);
    *out = current;
    return E_OK;
}

static LONG xfs_lookup_dir(const PFSCOOKIE *dir, const char *path, PFSCOOKIE *out)
{
    XFS_DIRECTORY *directory;
    LONG rc;

    directory = ptos_xfs_alloc(sizeof(*directory));
    if (!directory)
        return ENSMEM;
    memset(directory, 0, sizeof(*directory));
    rc = xfs_descend(xfs_dir_cookie(dir), path, &directory->dir.fc);
    if (rc < 0)
        ptos_xfs_free(directory);
    else
        xfs_publish(out, dir->fs, directory);
    return rc;
}

static LONG xfs_lookup(PFSCOOKIE *dir, const char *path, PFSCOOKIE *out)
{
    return xfs_lookup_dir(dir, path, out);
}

static LONG xfs_close_directory(PFSCOOKIE *fc, LONG position)
{
    XFS_DIRECTORY *directory = (XFS_DIRECTORY *)fc->index;
    LONG rc = E_OK, released;

    (void)position;
    fc->index = 0;
    if (directory->opened)
        rc = xfs_result(directory->dir.fc.fs->closedir(&directory->dir));
    released = xfs_release(&directory->dir.fc);
    ptos_xfs_free(directory);
    return rc < 0 ? rc : released;
}

static void xfs_release_directory(PFSCOOKIE *fc)
{
    xfs_close_directory(fc, 0);
}

static LONG xfs_stat(const struct pfs_ops *ops, fcookie *fc, PFSATTR *out)
{
    XATTR attr;
    LONG rc = xfs_result(fc->fs->getxattr(fc, &attr));

    if (rc < 0)
        return rc;
    if (attr.size < 0 && (sizeof(long) > sizeof(ULONG) ||
                          !(xfs_flags(ops) & PTOS_XFS_UNSIGNED_DOS_FIELDS)))
        return ERANGE;
    out->size = (ulong)attr.size > UINT32_MAX ? UINT32_MAX : (ULONG)attr.size;
    out->dos_attr = attr.attr;
    out->date = attr.mdate;
    out->time = attr.mtime;
    return E_OK;
}

static LONG xfs_readdir(PFSCOOKIE *fc, LONG *position, char *name, int size,
                        PFSATTR *attr)
{
    XFS_DIRECTORY *directory = (XFS_DIRECTORY *)fc->index;
    FILESYS *driver = directory->dir.fc.fs;
    fcookie entry = {0};
    LONG rc;

    if (!driver->opendir || !driver->readdir || !driver->closedir)
        return EINVFN;
    if (*position != directory->position || *position == INT32_MAX)
        return ENMFIL;
    if (!directory->opened) {
        directory->dir.flags = TOS_SEARCH;
        directory->dir.fd = -1;
        rc = xfs_result(driver->opendir(&directory->dir, TOS_SEARCH));
        if (rc < 0)
            return rc;
        directory->opened = TRUE;
    }
    rc = xfs_result(driver->readdir(&directory->dir, name, size, &entry));
    if (rc < 0)
        return rc;
    *position = ++directory->position;
    rc = xfs_validate(driver, &entry);
    if (rc < 0)
        return rc;
    rc = xfs_stat(fc->fs, &entry, attr);
    xfs_release(&entry);
    return rc;
}

static BOOL xfs_iterator_valid(const PFSCOOKIE *fc, LONG position)
{
    const XFS_DIRECTORY *directory = (const XFS_DIRECTORY *)fc->index;

    return directory && directory->opened && position == directory->position;
}

static LONG xfs_iterator_clone(const PFSCOOKIE *source, LONG position,
                               PFSCOOKIE *out, LONG *cursor)
{
    const struct ptos_xfs_extensions *ext = xfs_mount(source->fs)->extensions;
    const XFS_DIRECTORY *original = (const XFS_DIRECTORY *)source->index;
    XFS_DIRECTORY *directory;
    PFSCOOKIE copy;
    LONG rc;

    if (!ext || !ext->clone_dir)
        return EINVFN;
    if (!xfs_iterator_valid(source, position))
        return E_CHNG;
    rc = xfs_lookup_dir(source, "", &copy);
    if (rc < 0)
        return rc;
    directory = (XFS_DIRECTORY *)copy.index;
    directory->dir.flags = original->dir.flags;
    directory->dir.fd = -1;
    rc = xfs_result(ext->clone_dir(&directory->dir, &original->dir));
    if (rc < 0) {
        xfs_release_directory(&copy);
        return rc;
    }
    directory->opened = TRUE;
    directory->position = position;
    *out = copy;
    *cursor = position;
    return E_OK;
}

static LONG xfs_close_file(FILEPTR *file)
{
    LONG rc, released;

    file->links = 0;
#if CONF_WITH_KPROC
    rc = xfs_result(file->dev->close(file, run ? kproc_pid(run) : 0));
#else
    rc = xfs_result(file->dev->close(file, 0));
#endif
    released = xfs_release(&file->fc);
    ptos_xfs_free(file);
    return rc < 0 ? rc : released;
}

static LONG xfs_open_file(PFSCOOKIE *dir, const char *name, UWORD flags,
                          UWORD dos_attr, PFSCOOKIE *out)
{
    fcookie *parent = xfs_dir_cookie(dir);
    FILESYS *driver = parent->fs;
    FILEPTR *file;
    XATTR attr;
    LONG rc;
    BOOL created = FALSE;
    BOOL change_attr = FALSE;

    file = ptos_xfs_alloc(sizeof(*file));
    if (!file)
        return ENSMEM;
    memset(file, 0, sizeof(*file));
    rc = xfs_lookup_component(parent, name, &file->fc);
    if (rc == EFILNF && (flags & XFS_CREAT) && driver->creat) {
        rc = xfs_result(driver->creat(parent, name, XFS_IFREG | 0666, dos_attr, &file->fc));
        if (rc >= 0) {
            created = TRUE;
            rc = xfs_validate(driver, &file->fc);
        }
    }
    if (rc < 0)
        goto failed;

    if (!(xfs_flags(dir->fs) & PTOS_XFS_LEGACY_OPEN)) {
        rc = xfs_result(driver->getxattr(&file->fc, &attr));
        if (rc < 0)
            goto release;
        if ((attr.mode & XFS_IFMT) != XFS_IFREG ||
            (!created && (flags & MODE_FAC) != RO_MODE && (attr.attr & FA_RO))) {
            rc = EACCDN;
            goto release;
        }
        change_attr = !created && (flags & XFS_TRUNC) && (UWORD)attr.attr != dos_attr;
        if (change_attr && !driver->chattr) {
            rc = EACCDN;
            goto release;
        }
    }

#if CONF_WITH_MFASTCALL
    file->dev = ptos_xfs_call_getdev(driver->getdev, &file->fc, &file->devinfo);
#else
    file->dev = driver->getdev(&file->fc, &file->devinfo);
#endif
    if (!file->dev || !file->dev->open || !file->dev->close) {
        rc = !file->dev && file->devinfo < 0 ? xfs_result(file->devinfo) : EINVFN;
        goto release;
    }
    file->links = 1;
    file->flags = flags;
    rc = xfs_result(file->dev->open(file));
    if (rc < 0)
        goto release;
    /* Open the writable file before applying attributes such as FA_RO. */
    if (change_attr) {
        rc = xfs_result(driver->chattr(&file->fc, dos_attr));
        if (rc < 0) {
            xfs_close_file(file);
            return rc;
        }
    }
    xfs_publish(out, dir->fs, file);
    return E_OK;

release:
    xfs_release(&file->fc);
failed:
    ptos_xfs_free(file);
    return rc;
}

static LONG xfs_open(PFSCOOKIE *dir, const char *name, WORD mode, PFSCOOKIE *out)
{
    /* Admit the usual GEMDOS access modes and explicit deny-none.
     * Kernel share-denial, append and nonblocking semantics need a
     * separate admission contract before their flags can be accepted. */
    if (!(xfs_flags(dir->fs) & PTOS_XFS_LEGACY_OPEN) &&
        (((UWORD)mode & ~(MODE_FAC | XFS_DENY_NONE)) || (mode & MODE_FAC) > RW_MODE))
        return EINVFN;
    return xfs_open_file(dir, name, mode, 0, out);
}

static LONG xfs_create(PFSCOOKIE *dir, const char *name, UWORD attr, PFSCOOKIE *out)
{
    return xfs_open_file(dir, name, RW_MODE | XFS_CREAT | XFS_TRUNC, attr, out);
}

static LONG xfs_release_file(PFSCOOKIE *fc)
{
    FILEPTR *file = (FILEPTR *)fc->index;

    fc->index = 0;
    file->pos = fc->pos;
    return xfs_close_file(file);
}

static LONG xfs_transfer_result(FILEPTR *file, long result, LONG position, LONG len)
{
    LONG rc = result > len ? EGENRL : xfs_result(result);

    file->pos = rc > 0 ? position + rc : position;
    return rc;
}

static LONG xfs_read(PFSCOOKIE *fc, LONG pos, LONG len, UBYTE *buf)
{
    FILEPTR *file = (FILEPTR *)fc->index;
    long result;

    if ((file->flags & MODE_FAC) == WO_MODE || !file->dev->read)
        return EACCDN;
    if (len < 0 || pos < 0 || len > INT32_MAX - pos)
        return ERANGE;
    file->pos = pos;
    result = file->dev->read(file, (char *)buf, len);
    return xfs_transfer_result(file, result, pos, len);
}

static LONG xfs_write(PFSCOOKIE *fc, LONG pos, LONG len, const UBYTE *buf)
{
    FILEPTR *file = (FILEPTR *)fc->index;
    long result;

    if ((file->flags & MODE_FAC) == RO_MODE || !file->dev->write)
        return EACCDN;
    if (len < 0 || pos < 0 || len > INT32_MAX - pos)
        return ERANGE;
    file->pos = pos;
    result = file->dev->write(file, (const char *)buf, len);
    return xfs_transfer_result(file, result, pos, len);
}

static LONG xfs_seek(PFSCOOKIE *fc, LONG offset, WORD whence)
{
    FILEPTR *file = (FILEPTR *)fc->index;
    LONG rc;

    if (!file->dev->lseek)
        return EINVFN;
    file->pos = fc->pos;
    rc = xfs_result(file->dev->lseek(file, offset, whence));
    file->pos = rc < 0 ? fc->pos : rc;
    return rc;
}

static LONG xfs_fstat(PFSCOOKIE *fc, PFSATTR *out)
{
    FILEPTR *file = (FILEPTR *)fc->index;

    return xfs_stat(fc->fs, &file->fc, out);
}

static LONG xfs_ready(const PFSCOOKIE *fc, UWORD events)
{
    const struct ptos_xfs_extensions *ext = xfs_mount(fc->fs)->extensions;
    const FILEPTR *file = (const FILEPTR *)fc->index;
    LONG ready = 0;

    if (ext && ext->ready)
        return xfs_result(ext->ready(file, events));
    if (file->dev->read)
        ready |= FS_READY_READ;
    if (file->dev->write)
        ready |= FS_READY_WRITE;
    return events & ready;
}

static LONG xfs_ioctl(PFSCOOKIE *fc, UWORD command, void *buffer, ULONG size)
{
    const struct ptos_xfs_extensions *ext = xfs_mount(fc->fs)->extensions;

    if (!ext || !ext->ioctl)
        return EINVFN;
    return xfs_result(ext->ioctl((FILEPTR *)fc->index, command, buffer, size));
}

static LONG xfs_ioctl_spec(const PFSCOOKIE *fc, UWORD command, PFS_IOCTL_SPEC *spec)
{
    const struct ptos_xfs_extensions *ext = xfs_mount(fc->fs)->extensions;

    if (!ext || !ext->ioctl_spec || !ext->ioctl)
        return EINVFN;
    return xfs_result(ext->ioctl_spec((const FILEPTR *)fc->index, command, spec));
}

static LONG xfs_mkdir(PFSCOOKIE *dir, const char *name)
{
    fcookie *fc = xfs_dir_cookie(dir);

    return fc->fs->mkdir ? xfs_result(fc->fs->mkdir(fc, name, 0777)) : EACCDN;
}

static LONG xfs_rmdir(PFSCOOKIE *dir, const char *name)
{
    fcookie *fc = xfs_dir_cookie(dir);

    return fc->fs->rmdir ? xfs_result(fc->fs->rmdir(fc, name)) : EACCDN;
}

static LONG xfs_remove(PFSCOOKIE *dir, const char *name)
{
    fcookie *fc = xfs_dir_cookie(dir);

    return fc->fs->remove ? xfs_result(fc->fs->remove(fc, name)) : EACCDN;
}

static LONG xfs_rename(PFSCOOKIE *from, const char *oldname,
                       PFSCOOKIE *to, const char *newname)
{
    fcookie *old_dir = xfs_dir_cookie(from);
    fcookie *new_dir = xfs_dir_cookie(to);
    char name[LEN_ZPATH];

    if (!old_dir->fs->rename)
        return EACCDN;
    if (old_dir->dev != new_dir->dev)
        return ENSAME;
    if (strlcpy(name, oldname, sizeof(name)) >= sizeof(name))
        return ERANGE;
    return xfs_result(old_dir->fs->rename(old_dir, name, new_dir, newname));
}

static LONG xfs_chattr(PFSCOOKIE *dir, const char *name, BOOL set, UWORD *flags)
{
    fcookie *parent = xfs_dir_cookie(dir);
    fcookie fc = {0};
    PFSATTR attr;
    LONG rc = xfs_lookup_component(parent, name, &fc);

    if (rc < 0)
        return rc;
    if (set)
        rc = fc.fs->chattr ? xfs_result(fc.fs->chattr(&fc, *flags)) : EACCDN;
    else {
        rc = xfs_stat(dir->fs, &fc, &attr);
        if (rc >= 0)
            *flags = attr.dos_attr;
    }
    xfs_release(&fc);
    return rc;
}

static LONG xfs_dfree(struct pfs_ops *ops, WORD drive, ULONG out[4])
{
    PFSCOOKIE root;
    fcookie *directory;
    long counts[4];
    LONG rc;
    int i;

    if (!xfs_mount(ops)->driver->dfree)
        return EINVFN;
    rc = xfs_root(ops, drive, &root);
    if (rc < 0)
        return rc;
    directory = xfs_dir_cookie(&root);
    rc = xfs_result(directory->fs->dfree(directory, counts));
    if (rc >= 0) {
        for (i = 0; i < 4; i++) {
            if ((counts[i] < 0 && (sizeof(long) > sizeof(ULONG) ||
                                  !(xfs_flags(ops) & PTOS_XFS_UNSIGNED_DOS_FIELDS))) ||
                (ulong)counts[i] > UINT32_MAX) {
                rc = ERANGE;
                break;
            }
        }
        if (rc >= 0)
            for (i = 0; i < 4; i++)
                out[i] = (ULONG)counts[i];
    }
    xfs_release_directory(&root);
    return rc;
}

static const struct pfs_ops xfs_ops = {
    .root = xfs_root,
    .lookup = xfs_lookup,
    .open = xfs_open,
    .create = xfs_create,
    .read = xfs_read,
    .write = xfs_write,
    .readdir = xfs_readdir,
    .mkdir = xfs_mkdir,
    .rmdir = xfs_rmdir,
    .remove = xfs_remove,
    .rename = xfs_rename,
    .chattr = xfs_chattr,
    .dfree = xfs_dfree,
    .release = xfs_release_directory,
    .fstat = xfs_fstat,
    .release_file = xfs_release_file,
    .clone_iterator = xfs_iterator_clone,
    .iterator_valid = xfs_iterator_valid,
    .close_iterator = xfs_close_directory,
    .ready = xfs_ready,
    .ioctl = xfs_ioctl,
    .seek = xfs_seek,
    .ioctl_spec = xfs_ioctl_spec,
    .full_open_mode = TRUE
};

LONG ptos_xfs_mount_drive(WORD drive, struct ptos_xfs_mount *mount, FILESYS *driver,
                          const struct ptos_xfs_extensions *ext)
{
    const long supported = FS_CASESENSITIVE | FS_NOXBIT | FS_LONGPATH |
                           FS_REENTRANT_L1 | FS_REENTRANT_L2 | FS_EXT_1 | FS_EXT_2;
    LONG rc;
    UWORD flags;

    if (!mount || !driver)
        return EINVAL;
    if (mount->driver)
        return EACCDN;
    if (!driver->root || !driver->lookup || !driver->getxattr || !driver->getdev ||
        (driver->release && !driver->dupcookie) || (driver->fsflags & ~supported))
        return EINVFN;
    if (ext && (ext->version != PTOS_XFS_EXT_VERSION ||
                ext->size < offsetof(struct ptos_xfs_extensions, flags)))
        return EINVFN;
    flags = xfs_extension_flags(ext);
    if (flags & ~PTOS_XFS_EXT_FLAGS)
        return EINVFN;
    mount->ops = xfs_ops;
    if (flags & PTOS_XFS_LEGACY_OPEN)
        mount->ops.full_open_mode = FALSE;
    if (flags & PTOS_XFS_NO_DOS_ATTRIB)
        mount->ops.chattr = NULL;
    mount->driver = driver;
    mount->extensions = ext;
    rc = pfs_register_drive(drive, &mount->ops);
    if (rc < 0) {
        mount->driver = NULL;
        mount->extensions = NULL;
    }
    return rc;
}
