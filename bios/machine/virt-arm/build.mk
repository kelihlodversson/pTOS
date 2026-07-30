#
# bios/machine/virt-arm/build.mk - QEMU ARM 'virt' machine objects
#

# The startup code must be the very first object linked into the image.
obj-y += startup.o
