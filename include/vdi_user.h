/*
 * vdi_user.h - typed x32 VDI parameter contract
 *
 * Copyright (C) 2026 The pTOS development team.
 * This file is distributed under the GPL, version 2 or any later version.
 */
#ifndef VDI_USER_H
#define VDI_USER_H

#include "portab.h"
#include "userptr.h"

/* Function types, so USERPTR_T() produces a typed function pointer in x32
 * code and a 32-bit user address in the LP64 kernel. No slot is a native
 * kernel pointer. Delivery of these callbacks belongs to #432. */
typedef void VDI_TIMER_CALLBACK(int milliseconds);
typedef void VDI_BUTTON_CALLBACK(WORD status);
typedef ULONG VDI_MOTION_CALLBACK(WORD x, WORD y);
typedef void VDI_CURSOR_CALLBACK(WORD x, WORD y);
typedef void VDI_WHEEL_CALLBACK(WORD number, WORD amount);

struct font_head;

typedef struct {
    USERPTR_T(void) fd_addr;
    WORD fd_w, fd_h, fd_wdwidth, fd_stand, fd_nplanes;
    WORD fd_r1, fd_r2, fd_r3;
} VDI_USER_FDB;

typedef struct {
    WORD code, nptsin, nptsout, nintin, nintout, subcode, handle;
    WORD reserved;                 /* zero; native x32 pointer alignment */
    union {
        struct { ULONG reserved[3]; } arrays;
        struct {
            USERPTR_T(VDI_USER_FDB) source;
            USERPTR_T(VDI_USER_FDB) destination;
        } raster;
        struct {
            USERPTR_T(WORD) scratch;
            WORD scratch_offset;
            WORD reserved;
            USERPTR_T(struct font_head) first_font;
        } font_load;
        struct {
            USERPTR_T(VDI_TIMER_CALLBACK) new_handler;
            USERPTR_T(VDI_TIMER_CALLBACK) old_handler;
        } timer;
        struct {
            USERPTR_T(VDI_BUTTON_CALLBACK) new_handler;
            USERPTR_T(VDI_BUTTON_CALLBACK) old_handler;
        } button;
        struct {
            USERPTR_T(VDI_MOTION_CALLBACK) new_handler;
            USERPTR_T(VDI_MOTION_CALLBACK) old_handler;
        } motion;
        struct {
            USERPTR_T(VDI_CURSOR_CALLBACK) new_handler;
            USERPTR_T(VDI_CURSOR_CALLBACK) old_handler;
        } cursor;
        struct {
            USERPTR_T(VDI_WHEEL_CALLBACK) new_handler;
            USERPTR_T(VDI_WHEEL_CALLBACK) old_handler;
        } wheel;
    } args;
} VDI_USER_CONTROL;

typedef struct {
    USERPTR_T(VDI_USER_CONTROL) contrl;
    USERPTR_T(WORD) intin;
    USERPTR_T(WORD) ptsin;          /* consecutive x,y WORD coordinate pairs */
    USERPTR_T(WORD) intout;
    USERPTR_T(WORD) ptsout;
} VDI_USER_PB;

/* These are layout discriminators, not permission to execute an operation.
 * The service must independently validate ranges, lengths and ownership. */
enum vdi_user_kind {
    VDI_USER_UNSUPPORTED = -1,
    VDI_USER_ARRAYS,
    VDI_USER_RASTER,
    VDI_USER_FONT_LOAD,
    VDI_USER_TIMER,
    VDI_USER_BUTTON,
    VDI_USER_MOTION,
    VDI_USER_CURSOR,
    VDI_USER_WHEEL
};

enum vdi_user_kind vdi_user_control_kind(WORD code, WORD subcode);
/* Size only: does not dereference or validate fd_addr. packed_bytes is 0
 * for planar data, 2 for RGB565 or 4 for 32-bit pixels. Screen forms have
 * fd_addr == 0 and no user bitmap span; display access is service-owned. */
BOOL vdi_user_bitmap_bytes(const VDI_USER_FDB *form, UWORD packed_bytes,
                           ULONG *bytes);

#if defined(__x86_64__) && defined(__ILP32__)
LONG vdi(VDI_USER_PB *parameter_block);
#endif

/* Both the LP64 kernel view and real x32 code must have identical layouts.
 * Do not PACK these structs: the contract uses native x32 alignment. */
#if defined(__x86_64__)
#define VDI_USER_ASSERT(name, expr) typedef char vdi_user_assert_##name[(expr) ? 1 : -1]
VDI_USER_ASSERT(control_size, sizeof(VDI_USER_CONTROL) == 28);
VDI_USER_ASSERT(control_alignment, __alignof__(VDI_USER_CONTROL) == 4);
VDI_USER_ASSERT(code, offsetof(VDI_USER_CONTROL, code) == 0);
VDI_USER_ASSERT(nptsin, offsetof(VDI_USER_CONTROL, nptsin) == 2);
VDI_USER_ASSERT(nptsout, offsetof(VDI_USER_CONTROL, nptsout) == 4);
VDI_USER_ASSERT(nintin, offsetof(VDI_USER_CONTROL, nintin) == 6);
VDI_USER_ASSERT(nintout, offsetof(VDI_USER_CONTROL, nintout) == 8);
VDI_USER_ASSERT(subcode, offsetof(VDI_USER_CONTROL, subcode) == 10);
VDI_USER_ASSERT(handle, offsetof(VDI_USER_CONTROL, handle) == 12);
VDI_USER_ASSERT(args, offsetof(VDI_USER_CONTROL, args) == 16);
VDI_USER_ASSERT(raster_source, offsetof(VDI_USER_CONTROL, args.raster.source) == 16);
VDI_USER_ASSERT(raster_destination, offsetof(VDI_USER_CONTROL, args.raster.destination) == 20);
VDI_USER_ASSERT(font_scratch, offsetof(VDI_USER_CONTROL, args.font_load.scratch) == 16);
VDI_USER_ASSERT(font_offset, offsetof(VDI_USER_CONTROL, args.font_load.scratch_offset) == 20);
VDI_USER_ASSERT(font_first, offsetof(VDI_USER_CONTROL, args.font_load.first_font) == 24);
VDI_USER_ASSERT(timer_new, offsetof(VDI_USER_CONTROL, args.timer.new_handler) == 16);
VDI_USER_ASSERT(timer_old, offsetof(VDI_USER_CONTROL, args.timer.old_handler) == 20);
VDI_USER_ASSERT(button_old, offsetof(VDI_USER_CONTROL, args.button.old_handler) == 20);
VDI_USER_ASSERT(motion_old, offsetof(VDI_USER_CONTROL, args.motion.old_handler) == 20);
VDI_USER_ASSERT(cursor_old, offsetof(VDI_USER_CONTROL, args.cursor.old_handler) == 20);
VDI_USER_ASSERT(wheel_old, offsetof(VDI_USER_CONTROL, args.wheel.old_handler) == 20);
VDI_USER_ASSERT(pb_size, sizeof(VDI_USER_PB) == 20);
VDI_USER_ASSERT(pb_alignment, __alignof__(VDI_USER_PB) == 4);
VDI_USER_ASSERT(pb_contrl, offsetof(VDI_USER_PB, contrl) == 0);
VDI_USER_ASSERT(pb_intin, offsetof(VDI_USER_PB, intin) == 4);
VDI_USER_ASSERT(pb_ptsin, offsetof(VDI_USER_PB, ptsin) == 8);
VDI_USER_ASSERT(pb_intout, offsetof(VDI_USER_PB, intout) == 12);
VDI_USER_ASSERT(pb_ptsout, offsetof(VDI_USER_PB, ptsout) == 16);
VDI_USER_ASSERT(fdb_size, sizeof(VDI_USER_FDB) == 20);
VDI_USER_ASSERT(fdb_alignment, __alignof__(VDI_USER_FDB) == 4);
VDI_USER_ASSERT(fdb_addr, offsetof(VDI_USER_FDB, fd_addr) == 0);
VDI_USER_ASSERT(fdb_width, offsetof(VDI_USER_FDB, fd_w) == 4);
VDI_USER_ASSERT(fdb_height, offsetof(VDI_USER_FDB, fd_h) == 6);
VDI_USER_ASSERT(fdb_stride, offsetof(VDI_USER_FDB, fd_wdwidth) == 8);
VDI_USER_ASSERT(fdb_standard, offsetof(VDI_USER_FDB, fd_stand) == 10);
VDI_USER_ASSERT(fdb_planes, offsetof(VDI_USER_FDB, fd_nplanes) == 12);
VDI_USER_ASSERT(fdb_reserved1, offsetof(VDI_USER_FDB, fd_r1) == 14);
VDI_USER_ASSERT(fdb_reserved2, offsetof(VDI_USER_FDB, fd_r2) == 16);
VDI_USER_ASSERT(fdb_reserved3, offsetof(VDI_USER_FDB, fd_r3) == 18);
#undef VDI_USER_ASSERT
#endif

#endif
