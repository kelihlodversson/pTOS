#
# fs/build.mk - objects making up the pluggable filesystem layer
#
# This directory is only built at all when CONF_WITH_PLUGGABLE_FS is set
# (see optional-dirs-$(CONF_WITH_PLUGGABLE_FS) in the top level Makefile),
# so the files below are unconditional within it.
#

obj-y += pfs.o fatfs_pfs.o

obj-$(CONF_WITH_PLUGGABLE_FS_TEST) += pfs_test.o
