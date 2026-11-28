/* The production bridge with modeled mount admission and heap accounting. */
#include <stdio.h>
#include <stdlib.h>
#include "../fs/xfs_test.c"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "xfs:%d: %s\n", __LINE__, #condition); abort(); \
} } while (0)

static struct pfs_ops *registered;
static PD owner;
PD *run = &owner;
static unsigned allocations;
static BOOL fail_alloc;
static unsigned created_mode;

void *kalloc(ULONG size)
{
    void *address = fail_alloc ? NULL : malloc(size);

    if (address)
        allocations++;
    return address;
}

void kfree(void *address)
{
    if (address) {
        CHECK(allocations);
        allocations--;
        free(address);
    }
}

WORD kproc_pid(PD *process)
{
    CHECK(process == run);
    return 7;
}

size_t strlcpy(char *out, const char *in, size_t capacity)
{
    size_t size = strlen(in);

    if (capacity) {
        size_t copied = size < capacity - 1 ? size : capacity - 1;

        memcpy(out, in, copied);
        out[copied] = 0;
    }
    return size;
}

LONG pfs_register_drive(WORD drive, struct pfs_ops *ops)
{
    PFSCOOKIE root;
    LONG rc;

    if (registered)
        return EACCDN;
    rc = ops->root(ops, drive, &root);
    if (rc < 0)
        return rc;
    ops->release(&root);
    registered = ops;
    return E_OK;
}

static void test_mount_admission(void)
{
    struct ptos_xfs_mount mount = {0};
    struct ptos_xfs_extensions unsupported = test_extensions;

    fail_alloc = TRUE;
    CHECK(ptos_xfs_mount_drive(17, &mount, &test_driver, &test_extensions) == ENSMEM);
    CHECK(!mount.driver && !registered && !allocations);
    fail_alloc = FALSE;
    unsupported.version++;
    CHECK(ptos_xfs_mount_drive(17, &mount, &test_driver, &unsupported) == EINVFN);
    unsupported = test_extensions;
    unsupported.size = offsetof(struct ptos_xfs_extensions, flags) - 1;
    CHECK(ptos_xfs_mount_drive(17, &mount, &test_driver, &unsupported) == EINVFN);
    test_driver.fsflags = FS_NO_C_CACHE;
    CHECK(ptos_xfs_mount_drive(17, &mount, &test_driver, NULL) == EINVFN);
    test_driver.fsflags = 0;
    CHECK(xfs_test_init() == E_OK && registered && !allocations);
    CHECK(xfs_test_init() == EACCDN && !allocations);
}

static long _cdecl create_regular(fcookie *dir, const char *name, unsigned mode,
                                   int attr, fcookie *out)
{
    CHECK(test_node(dir) == &nodes[1] && !strcmp(name, "NEW.TXT") && !attr);
    created_mode = mode;
    test_cookie(out, dir->dev, &nodes[3]);
    return E_OK;
}

static long _cdecl stat_created(fcookie *cookie, XATTR *attr)
{
    CHECK(test_node(cookie) == &nodes[3]);
    memset(attr, 0, sizeof(*attr));
    attr->mode = created_mode;
    return E_OK;
}

static void test_recreate_attributes(PFSCOOKIE *dir)
{
    const char *name = "DATA.TXT";
    PFSCOOKIE file;
    PFSATTR attrs;
    unsigned before = allocations;
    UWORD dos_attr = 0;

    data_size = 3;
    test_driver.chattr = NULL;
    CHECK(registered->create(dir, name, 0, &file) == E_OK);
    CHECK(registered->release_file(&file) == E_OK);
    data_size = 3;
    CHECK(registered->create(dir, name, FA_HIDDEN, &file) == EACCDN);
    CHECK(data_size == 3 && !data_attr && !open_files && !node_refs[2] && allocations == before);
    test_driver.chattr = test_chattr;

    open_error = EWRITF;
    CHECK(registered->create(dir, name, FA_HIDDEN, &file) == EWRITF);
    CHECK(data_size == 3 && !data_attr && !open_files && !node_refs[2] && allocations == before);
    open_error = 0;
    CHECK(registered->create(dir, name, FA_HIDDEN | FA_RO, &file) == E_OK);
    CHECK(registered->fstat(&file, &attrs) == E_OK &&
          !attrs.size && attrs.dos_attr == (FA_HIDDEN | FA_RO));
    CHECK(registered->write(&file, 0, 1, (const UBYTE *)"x") == 1);
    CHECK(registered->release_file(&file) == E_OK);
    CHECK(registered->create(dir, name, 0, &file) == EACCDN && data_size == 1);
    CHECK(registered->chattr(dir, name, TRUE, &dos_attr) == E_OK);

    file.index = -1;
    CHECK(registered->create(dir, name, FA_VOL, &file) == EACCDN);
    CHECK(file.index == -1 && !data_size && !data_attr && !open_files &&
          !node_refs[2] && allocations == before);
}

int main(void)
{
    PFSCOOKIE root, dir, file, child;
    PFSATTR attrs;
    LONG parent_pos = 0, child_pos = 0;
    char name[13];
    UBYTE buffer[3] = {0};
    unsigned i;

    test_mount_admission();
    CHECK(registered->root(registered, 17, &root) == E_OK);
    CHECK(registered->lookup(&root, "FIXTURE\\.\\..\\FIXTURE", &dir) == E_OK);
    CHECK(node_refs[0] == 1 && node_refs[1] == 1);
    CHECK(registered->lookup(&dir, "DATA.TXT\\BAD", &child) == EPTHNF);
    CHECK(!node_refs[2]);
    registered->release(&root);

    /* Model filesystems that store the mode verbatim on a fresh inode. */
    test_driver.creat = create_regular;
    test_driver.getxattr = stat_created;
    CHECK(registered->create(&dir, "NEW.TXT", 0, &file) == E_OK);
    CHECK(created_mode == 0100666 && node_refs[3] == 1);
    CHECK(registered->release_file(&file) == E_OK && !node_refs[3]);
    test_driver.getxattr = test_stat;
    test_driver.creat = NULL;

    test_recreate_attributes(&dir);

    open_error = EWRITF;
    CHECK(registered->open(&dir, "DATA.TXT", RO_MODE, &file) == EWRITF);
    CHECK(!node_refs[2] && !open_files);
    open_error = 0;
    CHECK(registered->open(&dir, "DATA.TXT", 0x10, &file) == EINVFN);
    CHECK(registered->open(&dir, "DATA.TXT", RO_MODE, &file) == E_OK);
    CHECK(registered->write(&file, 0, 1, (const UBYTE *)"x") == EACCDN);
    CHECK(registered->release_file(&file) == E_OK);
    CHECK(registered->create(&dir, "DATA.TXT", 0, &file) == E_OK);
    CHECK(open_files == 1 && node_refs[2] == 1);
    CHECK(registered->write(&file, 0, 2, (const UBYTE *)"xy") == 2);
    file.pos = 2;
    CHECK(registered->seek(&file, -1, 2) == 1);
    CHECK(registered->fstat(&file, &attrs) == E_OK && attrs.size == 2);
    CHECK(registered->read(&file, 1, 1, buffer) == 1 && buffer[0] == 'y');
    CHECK(registered->ioctl(&file, 0, NULL, 0) == EINVFN);
    CHECK(registered->ready(&file, FS_READY_READ | FS_READY_WRITE) ==
          (FS_READY_READ | FS_READY_WRITE));
    close_error = EWRITF;
    CHECK(registered->release_file(&file) == EWRITF);
    CHECK(!open_files && !node_refs[2] && !file.index);

    CHECK(registered->readdir(&dir, &parent_pos, name, sizeof(name), &attrs) == E_OK);
    CHECK(!strcmp(name, "DATA.TXT") && parent_pos == 1);
    CHECK(registered->clone_iterator(&dir, parent_pos, &child, &child_pos) == E_OK);
    CHECK(open_dirs == 2 && child_pos == 1 && node_refs[1] == 2);
    CHECK(registered->readdir(&child, &child_pos, name, sizeof(name), &attrs) == E_OK);
    CHECK(!strcmp(name, "NULL.TXT"));
    CHECK(registered->close_iterator(&child, child_pos) == E_OK);
    CHECK(open_dirs == 1 && registered->iterator_valid(&dir, parent_pos));
    CHECK(registered->readdir(&dir, &parent_pos, name, sizeof(name), &attrs) == E_OK);
    CHECK(!strcmp(name, "NULL.TXT"));
    CHECK(registered->readdir(&dir, &parent_pos, name, sizeof(name), &attrs) == ENMFIL);
    CHECK(registered->close_iterator(&dir, parent_pos) == E_OK);

    for (i = 0; i < sizeof(nodes) / sizeof(nodes[0]); i++)
        CHECK(!node_refs[i]);
    CHECK(!allocations && !open_dirs);
    puts("XFS source ABI, full-width cookies, rollback and independent DIR ownership: PASS");
    return 0;
}
