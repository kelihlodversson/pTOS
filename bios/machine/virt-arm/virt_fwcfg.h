/*
 * virt_fwcfg.h - minimal QEMU fw_cfg client for the ARM 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef VIRT_FWCFG_H
#define VIRT_FWCFG_H

#ifdef MACHINE_VIRT_ARM

/* Configures the "etc/ramfb" fw_cfg file (the device created by QEMU's
 * "-device ramfb"), handing it a DRM_FORMAT_XRGB8888 framebuffer at the
 * given physical address. Returns FALSE if fw_cfg is not present, the DMA
 * interface is unavailable, or no "etc/ramfb" file is registered (i.e. no
 * "-device ramfb" was given) -- callers must keep working without a visible
 * display in that case. */
BOOL virt_ramfb_configure(ULONG phys_addr, ULONG width, ULONG height, ULONG stride);

#endif /* MACHINE_VIRT_ARM */

#endif /* VIRT_FWCFG_H */
