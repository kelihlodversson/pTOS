/*
 * vdi_service.c - narrow, checked supervisor operations for x32 VDI
 *
 * Copyright (C) 2026 The pTOS development team.
 * This file is distributed under the GPL, version 2 or any later version.
 */
#include "emutos.h"
#include "vdi_service.h"
#include "vdi_defs.h"
#include "lineavars.h"
#include "gemerror.h"
#include "string.h"
#include "biosbind.h"
#include "intmath.h"
#include "gsxdefs.h"
#include "procmem.h"
#include "../../../bdos/kproc.h"
#if CONF_WITH_CUIS
#include "../../../aes/cuis.h"
#endif
#if CONF_WITH_USER_CALLBACKS
#include "../../../bdos/user_callback.h"

/* Only these checked adapters can be named by a kernel OLD capability.
 * Arguments from a user-invoked OLD stub still need renderer bounds. */
static WORD callback_x(WORD x)
{
    return (WORD)max(0L, min((LONG)x, min((LONG)linea_vars.V_REZ_HZ - 1, 32767L)));
}

static WORD callback_y(WORD y)
{
    return (WORD)max(0L, min((LONG)y, min((LONG)linea_vars.V_REZ_VT - 1, 32767L)));
}

static void kernel_timer(int ms)
{
    linea_vars.tim_addr(ms >= 0 && ms <= 1000 ? ms : 0);
}

static void kernel_button(WORD status)
{
    linea_vars.user_but(status);
}

static ULONG kernel_motion(WORD x, WORD y)
{
    return linea_vars.user_mot(callback_x(x), callback_y(y));
}

static void kernel_cursor(WORD x, WORD y)
{
    linea_vars.user_cur(callback_x(x), callback_y(y));
}

static void kernel_wheel(WORD n, WORD amount)
{
#if CONF_WITH_EXTENDED_MOUSE
    extern void (*user_wheel)(WORD, WORD);

    if (n >= 0 && n <= 1 && amount >= -127 && amount <= 127)
        user_wheel(n, amount);
#else
    (void)n;
    (void)amount;
#endif
}

static const USER_CALLBACK_NATIVE kernel_callbacks[VDI_CALLBACK_COUNT] = {
    { .i32_void = kernel_timer }, { .i16_void = kernel_button },
    { .i16_i16_u32 = kernel_motion }, { .i16_i16_void = kernel_cursor },
    { .i16_i16_void = kernel_wheel }
};
static const UWORD callback_signatures[VDI_CALLBACK_COUNT] = {
    USER_CALLBACK_I32_VOID, USER_CALLBACK_I16_VOID, USER_CALLBACK_I16_I16_U32,
    USER_CALLBACK_I16_I16_VOID, USER_CALLBACK_I16_I16_VOID
};
static USER_CALLBACK *callbacks[VDI_CALLBACK_COUNT];
static struct {
    LONG a, b;
    BOOL pending;
} events[VDI_CALLBACK_COUNT];

/* IRQ-safe, bounded coalescing. No user code, allocation or process switch. */
void vdi_service_callback_event(UWORD signature, LONG a, LONG b)
{
    if (signature < VDI_CALLBACK_COUNT) {
        events[signature].a = a;
        events[signature].b = b;
        events[signature].pending = TRUE;
    }
}

void vdi_service_callback_work(void)
{
    UQUAD flags;
    ULONG result;
    LONG a, b;
    UWORD signature;

    /* One bounded pass per user boundary. */
    for (signature = 0; signature < VDI_CALLBACK_COUNT; signature++) {
        __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
        if (!events[signature].pending) {
            if (flags & 0x200)
                __asm__ volatile ("sti" ::: "memory");
            continue;
        }
        a = events[signature].a;
        b = events[signature].b;
        events[signature].pending = FALSE;
        if (flags & 0x200)
            __asm__ volatile ("sti" ::: "memory");
        if (user_callback_invoke(callbacks[signature], callback_signatures[signature], a, b, &result) == E_OK &&
            signature == VDI_CALLBACK_MOTION) {
            WORD x = (WORD)HIWORD(result), y = (WORD)LOWORD(result);

            /* IRQs continue tracking motion during the user handler. Its
             * result is stale if a newer packet has already moved the cursor.
             * Keep the check, stores and notification in one IRQ-free region. */
            __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
            if (!events[VDI_CALLBACK_MOTION].pending) {
                if (x < 0) x = 0;
                if (y < 0) y = 0;
                if (x >= linea_vars.V_REZ_HZ) x = linea_vars.V_REZ_HZ - 1;
                if (y >= linea_vars.V_REZ_VT) y = linea_vars.V_REZ_VT - 1;
                linea_vars.GCURX = x;
                linea_vars.GCURY = y;
                linea_vars.user_cur(x, y);
                vdi_service_callback_event(VDI_CALLBACK_CURSOR, x, y);
            }
            if (flags & 0x200)
                __asm__ volatile ("sti" ::: "memory");
        }
    }
}
#endif

extern PD *run;

static PD *owners[NUM_VDI_HANDLES + 1];
static BOOL physical_owned, physical_active;
#if CONF_WITH_CUIS
static BOOL service_transition;
BOOL vdi_service_preserve_console(void) { return service_transition; }
#if CONF_WITH_X86_64_MEMTEST
static volatile ULONG *lifecycle_pixel;
static ULONG lifecycle_opens, lifecycle_idle_closes;
static BOOL lifecycle_unchanged;
void vdi_service_test_watch(volatile ULONG *pixel)
{
    lifecycle_pixel = pixel;
    lifecycle_opens = lifecycle_idle_closes = 0;
    lifecycle_unchanged = TRUE;
}
BOOL vdi_service_test_preserved(void)
{
    return lifecycle_unchanged && lifecycle_opens && lifecycle_idle_closes;
}
static ULONG lifecycle_before(void)
{
    ULONG saved = lifecycle_pixel ? *lifecycle_pixel : 0;
    if (lifecycle_pixel) *lifecycle_pixel = 0x005a7193UL;
    return saved;
}
static void lifecycle_after(ULONG saved)
{
    if (lifecycle_pixel) {
        if (*lifecycle_pixel != 0x005a7193UL) lifecycle_unchanged = FALSE;
        *lifecycle_pixel = saved;
    }
}
#endif
#endif

/* Native AES can open the physical workstation between typed clients.
 * Record that transition rather than inferring it from a stale handle table. */
void vdi_service_physical_state(BOOL opened)
{
    physical_active = opened;
    physical_owned = FALSE;
    if (!opened)
        memset(owners, 0, sizeof(owners));
}

static void invoke(VDICONTROL *control, WORD *intin, WORD *ptsin,
                    WORD *intout, WORD *ptsout)
{
    VDIPB saved = linea_vars.local_pb;
    VDIPB block;

    block.contrl = control;
    block.intin = intin;
    block.ptsin = ptsin;
    block.intout = intout;
    block.ptsout = ptsout;
    GSX_ENTRY(0x73, &block);
    /* No user pointers or pointers to this stack may outlive the call. */
    linea_vars.local_pb = saved;
}

static void physical_open(void)
{
#if CONF_WITH_CUIS && CONF_WITH_X86_64_MEMTEST
    ULONG saved_pixel;
#endif
    VDICONTROL control = { 0 };
    WORD input[11] = { 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 2 };
    WORD integers[45], points[12];

    if (physical_active)
        return;
    control.code = 1;
    control.nintin = 11;
#if CONF_WITH_CUIS && CONF_WITH_X86_64_MEMTEST
    saved_pixel = lifecycle_before();
    lifecycle_opens++;
#endif
#if CONF_WITH_CUIS
    service_transition = TRUE;
#endif
    invoke(&control, input, points, integers, points);
#if CONF_WITH_CUIS
    service_transition = FALSE;
#endif
    physical_owned = physical_active = TRUE;
#if CONF_WITH_CUIS && CONF_WITH_X86_64_MEMTEST
    lifecycle_after(saved_pixel);
#endif
}

static void physical_close_if_idle(void)
{
#if CONF_WITH_CUIS && CONF_WITH_X86_64_MEMTEST
    ULONG saved_pixel = lifecycle_before();
    lifecycle_idle_closes++;
#endif
#if !CONF_WITH_CUIS
    VDICONTROL control = { 0 };
    WORD input = 0, integers[45], points[12];
    int i;

    if (!physical_owned || !physical_active)
        return;
    for (i = 2; i <= NUM_VDI_HANDLES; i++)
        if (owners[i])
            return;
    control.code = 2;
    control.handle = 1;
    invoke(&control, &input, points, integers, points);
    physical_active = FALSE;
#else
    /* Display lifetime belongs to kernel display coordination, not the
     * last virtual handle, allocation failure or terminating client. */
#endif
#if CONF_WITH_CUIS && CONF_WITH_X86_64_MEMTEST
    lifecycle_after(saved_pixel);
#endif
}

#if CONF_WITH_CUIS && CONF_WITH_X86_64_MEMTEST
/* Fixtures deliberately change Line-A dimensions. Reset only a service-owned
 * idle display at a kernel-controlled boundary, without console transitions. */
BOOL vdi_service_test_reset(void)
{
    VDICONTROL control = { 0 };
    WORD input = 0, integers[45], points[12];
    int i;
    if (!physical_active) return TRUE;
    if (!physical_owned) return FALSE;
    for (i = 2; i <= NUM_VDI_HANDLES; i++)
        if (owners[i]) return FALSE;
    control.code = 2;
    control.handle = 1;
    service_transition = TRUE;
    invoke(&control, &input, points, integers, points);
    service_transition = FALSE;
    return TRUE;
}
#endif

static BOOL owned(UQUAD handle)
{
    return handle >= 2 && handle <= NUM_VDI_HANDLES &&
           owners[handle] == run && get_vwk_by_handle((WORD)handle) != NULL;
}

static void close_handle(WORD handle)
{
    VDICONTROL control = { 0 };
    WORD input = 0, integers[45], points[12];

    control.code = 101;
    control.handle = handle;
    invoke(&control, &input, points, integers, points);
    owners[handle] = NULL;
}

void vdi_service_process_exit(PD *pd)
{
    WORD handle;
#if CONF_WITH_USER_CALLBACKS
    UWORD signature;

    for (signature = 0; signature < VDI_CALLBACK_COUNT; signature++)
        if (user_callback_owner(callbacks[signature], pd))
            user_callback_drop(&callbacks[signature]);
#endif

    for (handle = 2; handle <= NUM_VDI_HANDLES; handle++)
        if (owners[handle] == pd)
            close_handle(handle);
    physical_close_if_idle();
}

static LONG open_workstation(UQUAD input, UQUAD output)
{
    VDICONTROL control = { 0 };
    VDI_SERVICE_WORKSTATION result;
    WORD work_in[11];

    /* Back the destination before opening anything: an OOM while backing
     * lazy user memory ends the process and must not strand a new Vwk. */
    if (!kproc_copy_from_user(work_in, input, sizeof(work_in)) ||
        !kproc_validate_user_write(output, sizeof(result)))
        return EIMBA;
    if (work_in[10] != 2)
        return EINVAL;                  /* native raster-coordinate convention */
    physical_open();
    control.code = 100;
    control.nintin = 11;
    invoke(&control, work_in, result.ptsout, result.intout, result.ptsout);
    if (!control.handle) {
        physical_close_if_idle();
        return ENSMEM;
    }
    result.handle = control.handle;
    owners[result.handle] = run;
    if (!kproc_copy_to_user(output, &result, sizeof(result))) {
        close_handle(result.handle);
        physical_close_if_idle();
        return EIMBA;
    }
    return E_OK;
}

#if CONF_WITH_CUIS
static void bounded_invoke(VDICONTROL *control, WORD *input, WORD *points,
                            WORD *output, WORD *outpoints);
#endif

static LONG coordinate_call(ULONG fn, UQUAD handle, UQUAD value, UQUAD address)
{
    VDICONTROL control = { 0 };
    WORD points[VDI_SERVICE_MAX_POINTS * 2];
    WORD input, output[45], outpoints[12];
    ULONG words, i;

    control.handle = (WORD)handle;
    if (fn == VDI_SERVICE_COLOR) {
        if (value > 32767)
            return EINVAL;
        control.code = 17;
        control.nintin = 1;
        input = (WORD)value;
        words = 0;
    } else if (fn == VDI_SERVICE_CLIP) {
        if (value > 1)
            return EINVAL;
        control.code = 129;
        control.nintin = 1;
        control.nptsin = 2;
        input = (WORD)value;
        words = 4;
    } else {
        if (value < 2 || value > VDI_SERVICE_MAX_POINTS)
            return EINVAL;
        control.code = 6;
        control.nptsin = (WORD)value;
        input = 0;
        words = (ULONG)value * 2;
    }
    if (words && !kproc_copy_from_user(points, address, words * sizeof(WORD)))
        return EIMBA;
    for (i = 0; i < words; i++)
        if (points[i] < -MAX_COORDINATE || points[i] > MAX_COORDINATE)
            return EINVAL;
    if (fn == VDI_SERVICE_LINE) {
#if CONF_WITH_CUIS
        LONG policy = cuis_vdi_policy();
        if (policy < 0) return policy;
        bounded_invoke(&control, &input, points, output, outpoints);
#else
        Vwk *vwk = get_vwk_by_handle(control.handle);
        VwkClip saved = *VDI_CLIP(vwk);
        VwkClip bounded = {
            0, min((LONG)linea_vars.V_REZ_HZ - 1, 32767L),
            0, min((LONG)linea_vars.V_REZ_VT - 1, 32767L)
        };
        WORD clip = vwk->clip;

        /* Legacy VDI permits unchecked drawing with clip == FALSE.
         * User requests always need framebuffer bounds, independently
         * of the optional workstation rectangle. Keep its saved state. */
        if (clip) {
            if (saved.xmn_clip > bounded.xmn_clip)
                bounded.xmn_clip = saved.xmn_clip;
            if (saved.xmx_clip < bounded.xmx_clip)
                bounded.xmx_clip = saved.xmx_clip;
            if (saved.ymn_clip > bounded.ymn_clip)
                bounded.ymn_clip = saved.ymn_clip;
            if (saved.ymx_clip < bounded.ymx_clip)
                bounded.ymx_clip = saved.ymx_clip;
        }
        if (bounded.xmn_clip > bounded.xmx_clip || bounded.ymn_clip > bounded.ymx_clip)
            return E_OK;               /* empty screen/workstation intersection */
        *VDI_CLIP(vwk) = bounded;
        vwk->clip = TRUE;
        invoke(&control, &input, points, output, outpoints);
        *VDI_CLIP(vwk) = saved;
        vwk->clip = clip;
        linea_vars.CLIP = clip;
        linea_vars.XMINCL = saved.xmn_clip;
        linea_vars.XMAXCL = saved.xmx_clip;
        linea_vars.YMINCL = saved.ymn_clip;
        linea_vars.YMAXCL = saved.ymx_clip;
#endif
    } else {
        invoke(&control, &input, points, output, outpoints);
    }
    return fn == VDI_SERVICE_COLOR ? output[0] : E_OK;
}

static LONG vector_call(UQUAD code, UQUAD handler, UQUAD output)
{
    VDI_SERVICE_VECTOR_RESULT result = { 0 };
    ASPACE *aspace = kproc_user_aspace(run);
    UWORD protection;
#if CONF_WITH_USER_CALLBACKS
    UWORD signature;
    LONG rc;
#endif

    switch (code) {
    case 118: case 125: case 126: case 127: case 134:
        break;
    default:
        return EINVFN;
    }
    if (!kproc_validate_user_write(output, sizeof(result)))
        return EIMBA;
    if (handler && (handler > 0xffffffffULL || !aspace ||
        !aspace_translate(aspace, handler, NULL, &protection) ||
        (protection & (ASPACE_PROT_USER | ASPACE_PROT_EXEC)) !=
                     (ASPACE_PROT_USER | ASPACE_PROT_EXEC)))
        return EIMBA;
    if (code == 118)
        result.tick_ms = Tickcal();
#if CONF_WITH_USER_CALLBACKS
    switch (code) {
    case 118: signature = VDI_CALLBACK_TIMER; break;
    case 125: signature = VDI_CALLBACK_BUTTON; break;
    case 126: signature = VDI_CALLBACK_MOTION; break;
    case 127: signature = VDI_CALLBACK_CURSOR; break;
    default: signature = VDI_CALLBACK_WHEEL; break;
    }
    if (!user_callback_work_register(vdi_service_callback_work))
        return ENSMEM;
    rc = user_callback_exchange(&callbacks[signature], callback_signatures[signature],
                                handler, &kernel_callbacks[signature], &result.old_handler);
    if (rc != E_OK)
        return rc;
    return kproc_copy_to_user(output, &result, sizeof(result)) ? E_OK : EIMBA;
#else
    /* No user registration exists yet, so its old user value is zero.
     * Never expose or replace the live kernel vector. #432 owns delivery. */
    if (!kproc_copy_to_user(output, &result, sizeof(result)))
        return EIMBA;
    return handler ? EINVFN : E_OK;
#endif
}

static LONG pixel_call(UQUAD handle, UQUAD x, UQUAD y, UQUAD address)
{
    VDICONTROL control = { 0 };
    WORD points[2], input = 0, output[2], outpoints[12];

    /* Screen dimensions are unsigned WORDs; renderer coordinates are
     * signed WORDs. Reject a value that would wrap before narrowing. */
    if (x > 32767 || y > 32767 ||
        x >= (UWORD)linea_vars.V_REZ_HZ || y >= (UWORD)linea_vars.V_REZ_VT)
        return EINVAL;
    if (!kproc_validate_user_write(address, sizeof(output)))
        return EIMBA;
    points[0] = (WORD)x;
    points[1] = (WORD)y;
    control.code = 105;
    control.handle = (WORD)handle;
    control.nptsin = 1;
    invoke(&control, &input, points, output, outpoints);
    return kproc_copy_to_user(address, output, sizeof(output)) ? E_OK : EIMBA;
}

#if CONF_WITH_CUIS
/* GDP bars expand two input points into a five-point perimeter in place.
 * Keep original geometry untouched across disjoint clip passes and provide
 * enough space for both that expansion and the largest checked polyline. */
static void invoke_region(VDICONTROL *control, WORD *input, const WORD *points,
                           WORD *output, WORD *outpoints)
{
    WORD scratch[VDI_SERVICE_MAX_POINTS * 2];
    memcpy(scratch, points, (ULONG)control->nptsin * 2 * sizeof(WORD));
    invoke(control, input, scratch, output, outpoints);
}
#endif

/* Drawing always intersects the virtual clip with the physical screen,
 * including when the client has disabled its optional clip rectangle. */
static void bounded_invoke(VDICONTROL *control, WORD *input, WORD *points,
                            WORD *output, WORD *outpoints)
{
    Vwk *vwk = get_vwk_by_handle(control->handle);
    VwkClip saved = *VDI_CLIP(vwk);
    VwkClip clip = { 0, min((LONG)linea_vars.V_REZ_HZ - 1, 32767L),
                    0, min((LONG)linea_vars.V_REZ_VT - 1, 32767L) };
    WORD enabled = vwk->clip;

    if (enabled) {
        clip.xmn_clip = max(clip.xmn_clip, saved.xmn_clip);
        clip.xmx_clip = min(clip.xmx_clip, saved.xmx_clip);
        clip.ymn_clip = max(clip.ymn_clip, saved.ymn_clip);
        clip.ymx_clip = min(clip.ymx_clip, saved.ymx_clip);
    }
    if (clip.xmn_clip <= clip.xmx_clip && clip.ymn_clip <= clip.ymx_clip) {
#if CONF_WITH_CUIS
        if (cuis_vdi_policy() == 1) {
            CUIS_RECT region;
            ULONG index;
            for (index = 0; cuis_vdi_clip(index, &region); index++) {
                VwkClip bounded;
                LONG right = min((LONG)clip.xmx_clip, region.x + region.width - 1);
                LONG bottom = min((LONG)clip.ymx_clip, region.y + region.height - 1);
                LONG left = max((LONG)clip.xmn_clip, region.x);
                LONG top = max((LONG)clip.ymn_clip, region.y);
                if (left > right || top > bottom) continue;
                bounded.xmn_clip = (WORD)left; bounded.xmx_clip = (WORD)right;
                bounded.ymn_clip = (WORD)top; bounded.ymx_clip = (WORD)bottom;
                *VDI_CLIP(vwk) = bounded;
                vwk->clip = TRUE;
                invoke_region(control, input, points, output, outpoints);
            }
        } else
#endif
        {
            *VDI_CLIP(vwk) = clip;
            vwk->clip = TRUE;
#if CONF_WITH_CUIS
            invoke_region(control, input, points, output, outpoints);
#else
            invoke(control, input, points, output, outpoints);
#endif
        }
    }
    *VDI_CLIP(vwk) = saved;
    vwk->clip = enabled;
    linea_vars.CLIP = enabled;
    linea_vars.XMINCL = saved.xmn_clip;
    linea_vars.XMAXCL = saved.xmx_clip;
    linea_vars.YMINCL = saved.ymn_clip;
    linea_vars.YMAXCL = saved.ymx_clip;
}

static LONG draw_call(WORD handle, UQUAD input, UQUAD output)
{
    VDI_SERVICE_DRAW_REQUEST request;
    VDI_SERVICE_DRAW_RESULT result = { 0 };
    VDICONTROL control = { 0 };
    LONG rc, i;

    if ((input & 1) || (output & 1) ||
        !kproc_copy_from_user(&request, input, sizeof(request)) ||
        !kproc_validate_user_write(output, sizeof(result)))
        return EIMBA;
    rc = vdi_user_draw_shape(&request, &result.nintout, &result.nptsout);
    if (rc)
        return rc;
    for (i = 0; i < request.nptsin * 2; i++)
        if (request.ptsin[i] < -MAX_COORDINATE || request.ptsin[i] > MAX_COORDINATE)
            return EINVAL;
    control.code = request.code;
    control.subcode = request.subcode;
    control.handle = handle;
    control.nintin = request.nintin;
    control.nptsin = request.nptsin;
    if (request.code == 8 || request.code == 11 || request.code == 114) {
#if CONF_WITH_CUIS
        rc = cuis_vdi_policy();
        if (rc < 0) return rc;
#endif
        bounded_invoke(&control, request.intin, request.ptsin, result.intout, result.ptsout);
    } else
        invoke(&control, request.intin, request.ptsin, result.intout, result.ptsout);
    return kproc_copy_to_user(output, &result, sizeof(result)) ? E_OK : EIMBA;
}

#if CONF_WITH_CUIS
/* A screen copy may read any source pixel. Split destination rows in memmove
 * order so overlapping source/destination ranges remain correct even across
 * disjoint CUIS clip pieces. Native VDI handles overlap within each segment. */
static void cuis_screen_copy(VDICONTROL *control, WORD *mode, const WORD *xy,
                              WORD *integers, WORD *points)
{
    CUIS_RECT regions[CUIS_CLIP_LIMIT], region;
    ULONG count = 0, i, j;
    Vwk *vwk = get_vwk_by_handle(control->handle);
    VwkClip saved = *VDI_CLIP(vwk), clip;
    WORD enabled = vwk->clip, row_xy[8];
    LONG row, step, end, left, right, y;
    BOOL reverse = xy[4] > xy[0];

    /* Sorting by x once orders disjoint row segments for horizontal overlap. */
    for (i = 0; cuis_vdi_clip(i, &region); i++) {
        j = count;
        while (j && (reverse ? regions[j - 1].x < region.x : regions[j - 1].x > region.x)) {
            regions[j] = regions[j - 1];
            j--;
        }
        regions[j] = region;
        count++;
    }
    if (!count) return;
    step = xy[5] > xy[1] ? -1 : 1;
    row = step < 0 ? xy[7] - xy[5] : 0;
    end = step < 0 ? -1 : xy[7] - xy[5] + 1;
    for (; row != end; row += step) {
        y = xy[5] + row;
        if (enabled && (y < saved.ymn_clip || y > saved.ymx_clip)) continue;
        for (i = 0; i < count; i++) {
            region = regions[i];
            if (y < region.y || y >= region.y + region.height) continue;
            left = max((LONG)xy[4], region.x);
            right = min((LONG)xy[6], region.x + region.width - 1);
            if (enabled) {
                left = max(left, (LONG)saved.xmn_clip);
                right = min(right, (LONG)saved.xmx_clip);
            }
            if (left > right) continue;
            /* Restrict the native copy itself, rather than relying on source
             * clipping, which would wrongly reject screenshot-style reads. */
            row_xy[0] = (WORD)(xy[0] + left - xy[4]);
            row_xy[2] = (WORD)(xy[0] + right - xy[4]);
            row_xy[1] = row_xy[3] = (WORD)(xy[1] + row);
            row_xy[4] = (WORD)left; row_xy[6] = (WORD)right;
            row_xy[5] = row_xy[7] = (WORD)y;
            clip.xmn_clip = (WORD)left; clip.xmx_clip = (WORD)right;
            clip.ymn_clip = clip.ymx_clip = (WORD)y;
            *VDI_CLIP(vwk) = clip;
            vwk->clip = TRUE;
            invoke(control, mode, row_xy, integers, points);
        }
    }
    *VDI_CLIP(vwk) = saved;
    vwk->clip = enabled;
    linea_vars.CLIP = enabled;
    linea_vars.XMINCL = saved.xmn_clip;
    linea_vars.XMAXCL = saved.xmx_clip;
    linea_vars.YMINCL = saved.ymn_clip;
    linea_vars.YMAXCL = saved.ymx_clip;
}
#endif

static LONG screen_copy(WORD handle, UQUAD input)
{
    VDI_SERVICE_SCREEN_REQUEST request;
    VDICONTROL control = { 0 };
    FDB screen = { 0 };
    WORD integers[45], points[12];
    LONG i;
#if CONF_WITH_CUIS
    LONG policy = cuis_vdi_policy();
    if (policy < 0) return policy;
#endif

    if ((input & 1) || !kproc_copy_from_user(&request, input, sizeof(request)))
        return EIMBA;
    if (request.reserved || request.mode < 0 || request.mode > 15)
        return EINVAL;
    for (i = 0; i < 8; i += 4)
        if (request.xy[i] < 0 || request.xy[i + 1] < 0 ||
            request.xy[i + 2] < request.xy[i] || request.xy[i + 3] < request.xy[i + 1] ||
            request.xy[i + 2] - request.xy[i] > 32766 ||
            request.xy[i + 3] - request.xy[i + 1] > 32766 ||
            request.xy[i + 2] >= linea_vars.V_REZ_HZ ||
            request.xy[i + 3] >= linea_vars.V_REZ_VT)
            return EINVAL;
    if (request.xy[2] - request.xy[0] != request.xy[6] - request.xy[4] ||
        request.xy[3] - request.xy[1] != request.xy[7] - request.xy[5])
        return EINVAL;
    control.code = 109;
    control.handle = handle;
    control.nintin = 1;
    control.nptsin = 4;
    control.ptr1 = control.ptr2 = &screen;
#if CONF_WITH_CUIS
    if (policy == 1)
        cuis_screen_copy(&control, &request.mode, request.xy, integers, points);
    else
#endif
        bounded_invoke(&control, &request.mode, request.xy, integers, points);
    return E_OK;
}

LONG vdi_service(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    if (!kproc_user_aspace(run))
        return EACCDN;
    if (fn == VDI_SERVICE_OPEN)
        return open_workstation(a, b);
    if (fn > VDI_SERVICE_SCREEN_COPY)
        return EINVFN;
    if (!owned(a))
        return EIHNDL;
    switch (fn) {
    case VDI_SERVICE_CLOSE:
        close_handle((WORD)a);
        physical_close_if_idle();
        return E_OK;
    case VDI_SERVICE_LINE:
    case VDI_SERVICE_CLIP:
    case VDI_SERVICE_COLOR:
        return coordinate_call(fn, a, b, c);
    case VDI_SERVICE_CHECK:
        return E_OK;
    case VDI_SERVICE_VECTOR:
        return vector_call(b, c, d);
    case VDI_SERVICE_PIXEL:
        return pixel_call(a, b, c, d);
    case VDI_SERVICE_DRAW:
        return d ? EINVAL : draw_call((WORD)a, b, c);
    case VDI_SERVICE_SCREEN_COPY:
        return (c || d) ? EINVAL : screen_copy((WORD)a, b);
    default:
        return EINVFN;
    }
}
