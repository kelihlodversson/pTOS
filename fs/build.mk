#
# fs/build.mk - the filesystem layer
#
# fatfs_pfs.c contains the always-built FAT cores and off-path wrappers.
# pfs.c contains the optional pluggable dispatch machinery, and pfs_test.o
# is built only with the self-test driver.
#
obj-y += fatfs_pfs.o
obj-$(CONF_WITH_PLUGGABLE_FS) += pfs.o
obj-$(CONF_WITH_XFS) += xfs_pfs.o
ifeq ($(CONF_WITH_XFS),y)
obj-$(CONF_WITH_MFASTCALL) += xfs_abi.o
endif
obj-$(CONF_WITH_PLUGGABLE_FS_TEST) += pfs_test.o
ifeq ($(CONF_WITH_PLUGGABLE_FS_TEST),y)
obj-$(CONF_WITH_XFS) += xfs_test.o
endif

obj-$(CONF_WITH_VIRTIO_9P) += virtio_9p_xfs.o
