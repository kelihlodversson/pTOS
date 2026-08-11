#
# usb/build.mk - objects making up the USB stack
#
# The USB code is conceptually part of the BIOS and is allowed to use the
# BIOS private headers; see usb_copts in the top level Makefile.
#

obj-y += usb.o ucd.o udd.o usb_api.o usb_hub.o udd_mouse.o udd_keyboard.o

obj-$(CONF_WITH_USB_DWC2) += ucd_dwc2.o
obj-$(CONF_WITH_USB_XHCI) += ucd_xhci.o
