#
# fs/build.mk - the filesystem layer
#
# fatfs_pfs.c (the built-in FAT implementation) is always built; pfs.c
# (the pluggable dispatch machinery) only when CONF_WITH_PLUGGABLE_FS is
# set, and pfs_test.o only with the self-test driver.  See
# docs/superpowers/specs/2026-08-11-invert-pluggable-fs-dispatch-design.md
#
obj-y += fatfs_pfs.o
obj-$(CONF_WITH_PLUGGABLE_FS) += pfs.o
obj-$(CONF_WITH_PLUGGABLE_FS_TEST) += pfs_test.o
