/*
 * virt_screen.c - QEMU virt (ARM) XRGB8888 test framebuffer (issue #91)
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version. See doc/license.txt for details.
 *
 * Test hook only (CONF_WITH_VDI_TRUECOLOR32_TEST): QEMU's 'virt' board
 * has no display hardware, so the framebuffer is a guest-RAM buffer. It
 * is never displayed; the point is to exercise the 32bpp XRGB8888
 * truecolor VDI backend end-to-end. Issue #68's ramfb driver replaces
 * this buffer later.
 */

#include "config.h"
#ifndef MACHINE_VIRT_ARM
#error This file must only be compiled for the QEMU virt (ARM) target
#endif

#include "portab.h"
#include "screen.h"
#include "screen_mode.h"
#include "biosmem.h"
#include "tosvars.h"
#include "kprint.h"
#include "virt_mmu.h"

#define VIRT_TC32_WIDTH  640
#define VIRT_TC32_HEIGHT 480
#define VIRT_TC32_PITCH  (VIRT_TC32_WIDTH * 4)

void virt_arm_screen_init(void)
{
    UBYTE *screen_start = balloc_stram(VIRT_TC32_PITCH * VIRT_TC32_HEIGHT, TRUE);

    v_bas_ad = screen_start;
    kprintf("virt-arm tc32: %dx%d XRGB8888 framebuffer at phys 0x%08lx (%d bytes)\n",
            VIRT_TC32_WIDTH, VIRT_TC32_HEIGHT,
            virt_to_phys(screen_start), VIRT_TC32_PITCH * VIRT_TC32_HEIGHT);
}

void virt_arm_get_current_mode_desc(SCREEN_MODE_DESC *desc)
{
    desc->width = VIRT_TC32_WIDTH;
    desc->height = VIRT_TC32_HEIGHT;
    desc->pitch = VIRT_TC32_PITCH;
    desc->bits_per_pixel = 32;
    desc->layout = SCREEN_LAYOUT_PACKED;
    desc->color_model = SCREEN_COLOR_TRUECOLOR;
    desc->pixel_format = SCREEN_PIXEL_XRGB8888;
}

void virt_arm_screen_report(void)
{
    kprintf("virt-arm tc32: %dx%d XRGB8888 framebuffer at phys 0x%08lx (%d bytes)\n",
            VIRT_TC32_WIDTH, VIRT_TC32_HEIGHT,
            virt_to_phys(v_bas_ad), VIRT_TC32_PITCH * VIRT_TC32_HEIGHT);
}
