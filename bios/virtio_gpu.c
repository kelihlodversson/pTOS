/* virtio-gpu 2D framebuffer for QEMU virt boards. */
#include "config.h"

#if CONF_WITH_VIRTIO_GPU
#include "portab.h"
#include "biosmem.h"
#include "endian.h"
#include "kprint.h"
#include "screen.h"
#include "screen_mode.h"
#include "tosvars.h"
#include "virtio.h"

#define VIRTIO_ID_GPU                 16
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO 0x0100UL
#define VIRTIO_GPU_CMD_RESOURCE_2D    0x0101UL
#define VIRTIO_GPU_CMD_SET_SCANOUT    0x0103UL
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH 0x0104UL
#define VIRTIO_GPU_CMD_TRANSFER_2D    0x0105UL
#define VIRTIO_GPU_CMD_ATTACH_BACKING 0x0106UL
#define VIRTIO_GPU_RESP_OK_NODATA     0x1100UL
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO 0x1101UL
#define VIRTIO_GPU_FORMAT_B8G8R8X8    2UL
#define VIRTIO_GPU_POLL_LIMIT 100000UL

typedef struct { ULONG type, flags; UQUAD fence_id; ULONG ctx_id, padding; } GPU_HDR;
typedef struct { ULONG x, y, width, height; } GPU_RECT;
typedef struct { GPU_HDR hdr; ULONG resource_id, format, width, height; } GPU_RESOURCE_2D;
typedef struct { GPU_HDR hdr; ULONG resource_id, nr_entries, addr_lo, addr_hi, length, entry_padding; } GPU_ATTACH;
typedef struct { GPU_HDR hdr; GPU_RECT rect; ULONG scanout_id, resource_id; } GPU_SCANOUT;
typedef struct { GPU_HDR hdr; GPU_RECT rect; UQUAD offset; ULONG resource_id, padding; } GPU_TRANSFER;
typedef struct { GPU_HDR hdr; GPU_RECT rect; ULONG resource_id, padding; } GPU_FLUSH;
typedef struct { GPU_RECT rect; ULONG enabled, flags; } GPU_DISPLAY_MODE;
typedef struct { GPU_HDR hdr; GPU_DISPLAY_MODE modes[16]; } GPU_DISPLAY_INFO;
typedef union { GPU_RESOURCE_2D resource; GPU_ATTACH attach; GPU_SCANOUT scanout; GPU_TRANSFER transfer; GPU_FLUSH flush; } GPU_COMMAND;
typedef union { GPU_HDR hdr; GPU_DISPLAY_INFO display; } GPU_RESPONSE;

static VIRTIO_DEV gpu_dev;
static GPU_COMMAND gpu_command __attribute__((aligned(VIRTIO_CACHE_LINE)));
static GPU_RESPONSE gpu_response __attribute__((aligned(VIRTIO_CACHE_LINE)));
static BOOL gpu_is_present, gpu_in_flight, gpu_flush_pending;
static ULONG gpu_width, gpu_height, gpu_pitch, gpu_size;

static void gpu_header(GPU_HDR *hdr, ULONG type)
{
    hdr->type = cpu2le32(type); hdr->flags = 0; hdr->fence_id = 0; hdr->ctx_id = 0; hdr->padding = 0;
}

static void gpu_rect(GPU_RECT *rect)
{
    rect->x = 0; rect->y = 0; rect->width = cpu2le32(gpu_width); rect->height = cpu2le32(gpu_height);
}

static void gpu_submit(ULONG size)
{
    virtio_desc_set(&gpu_dev, 0, (ULONG)&gpu_command + gpu_dev.phys_offset, size, VIRTIO_DESC_F_NEXT, 1);
    virtio_desc_set(&gpu_dev, 1, (ULONG)&gpu_response + gpu_dev.phys_offset, sizeof(gpu_response), VIRTIO_DESC_F_WRITE, 0);
    virtio_submit(&gpu_dev, 0);
    virtio_flush_buffer(&gpu_command, size);
    virtio_notify(&gpu_dev);
    gpu_in_flight = TRUE;
}

static BOOL gpu_wait(ULONG response_type)
{
    ULONG count;
    for (count = 0; count < VIRTIO_GPU_POLL_LIMIT; count++) {
        virtio_poll(&gpu_dev);
        if (gpu_dev.done) {
            gpu_in_flight = FALSE;
            virtio_invalidate_buffer(&gpu_response, sizeof(gpu_response));
            return le2cpu32(gpu_response.hdr.type) == response_type;
        }
    }
    gpu_in_flight = FALSE;
    return FALSE;
}

static BOOL gpu_submit_wait(ULONG size, ULONG response_type)
{
    gpu_submit(size);
    return gpu_wait(response_type);
}

static BOOL gpu_get_display_info(void)
{
    gpu_header(&gpu_command.resource.hdr, VIRTIO_GPU_CMD_GET_DISPLAY_INFO);
    if (!gpu_submit_wait(sizeof(GPU_HDR), VIRTIO_GPU_RESP_OK_DISPLAY_INFO))
        return FALSE;

    if (!le2cpu32(gpu_response.display.modes[0].enabled))
        return FALSE;
    gpu_width = le2cpu32(gpu_response.display.modes[0].rect.width);
    gpu_height = le2cpu32(gpu_response.display.modes[0].rect.height);
    if (!gpu_width || !gpu_height || gpu_width > 32767UL || gpu_height > 32767UL)
        return FALSE;
    if (gpu_width > 0x3fffffffUL / 4UL)
        return FALSE;
    gpu_pitch = gpu_width * 4UL;
    if (gpu_height > 0xffffffffUL / gpu_pitch)
        return FALSE;
    gpu_size = gpu_pitch * gpu_height;
    return TRUE;
}

static BOOL gpu_create_resource(UBYTE *framebuffer)
{
    gpu_header(&gpu_command.resource.hdr, VIRTIO_GPU_CMD_RESOURCE_2D);
    gpu_command.resource.resource_id = cpu2le32(1); gpu_command.resource.format = cpu2le32(VIRTIO_GPU_FORMAT_B8G8R8X8);
    gpu_command.resource.width = cpu2le32(gpu_width); gpu_command.resource.height = cpu2le32(gpu_height);
    if (!gpu_submit_wait(sizeof(gpu_command.resource), VIRTIO_GPU_RESP_OK_NODATA)) return FALSE;
    gpu_header(&gpu_command.attach.hdr, VIRTIO_GPU_CMD_ATTACH_BACKING);
    gpu_command.attach.resource_id = cpu2le32(1); gpu_command.attach.nr_entries = cpu2le32(1);
    gpu_command.attach.addr_lo = cpu2le32((ULONG)framebuffer + gpu_dev.phys_offset); gpu_command.attach.addr_hi = 0;
    gpu_command.attach.length = cpu2le32(gpu_size); gpu_command.attach.entry_padding = 0;
    if (!gpu_submit_wait(sizeof(gpu_command.attach), VIRTIO_GPU_RESP_OK_NODATA)) return FALSE;
    gpu_header(&gpu_command.scanout.hdr, VIRTIO_GPU_CMD_SET_SCANOUT);
    gpu_rect(&gpu_command.scanout.rect); gpu_command.scanout.scanout_id = 0; gpu_command.scanout.resource_id = cpu2le32(1);
    return gpu_submit_wait(sizeof(gpu_command.scanout), VIRTIO_GPU_RESP_OK_NODATA);
}

void virtio_gpu_init(void)
{
    UBYTE *framebuffer;
    WORD slot;
    gpu_is_present = FALSE; gpu_in_flight = FALSE; gpu_flush_pending = FALSE;
    if (!virtio_find_device(VIRTIO_ID_GPU, 0, &gpu_dev, &slot) || !virtio_setup_queue(&gpu_dev)) return;
    if (!gpu_get_display_info()) { KDEBUG(("virtio_gpu: display query failed at slot %d\n", slot)); return; }
    framebuffer = balloc_stram(gpu_size, TRUE);
    if (!gpu_create_resource(framebuffer)) { KDEBUG(("virtio_gpu: setup failed at slot %d\n", slot)); return; }
    v_bas_ad = framebuffer; gpu_is_present = TRUE;
    KDEBUG(("virtio_gpu: %lux%lu B8G8R8X8 scanout at slot %d\n", gpu_width, gpu_height, slot));
}

BOOL virtio_gpu_present(void) { return gpu_is_present; }

void virtio_gpu_get_current_mode_desc(SCREEN_MODE_DESC *desc)
{
    desc->width = (UWORD)gpu_width; desc->height = (UWORD)gpu_height; desc->pitch = gpu_pitch;
    desc->bits_per_pixel = 32; desc->layout = SCREEN_LAYOUT_PACKED; desc->color_model = SCREEN_COLOR_TRUECOLOR;
    desc->pixel_format = SCREEN_PIXEL_XRGB8888; desc->shifter = SCREEN_SHIFTER_NONE;
}

void virtio_gpu_update(void)
{
    if (!gpu_is_present) return;
    if (gpu_in_flight) {
        virtio_poll(&gpu_dev);
        if (!gpu_dev.done) return;
        gpu_in_flight = FALSE;
        if (!gpu_flush_pending) return;
    }
    if (gpu_flush_pending) {
        gpu_header(&gpu_command.flush.hdr, VIRTIO_GPU_CMD_RESOURCE_FLUSH); gpu_rect(&gpu_command.flush.rect);
        gpu_command.flush.resource_id = cpu2le32(1); gpu_command.flush.padding = 0;
        gpu_submit(sizeof(gpu_command.flush)); gpu_flush_pending = FALSE; return;
    }
    virtio_flush_buffer(v_bas_ad, gpu_size);
    gpu_header(&gpu_command.transfer.hdr, VIRTIO_GPU_CMD_TRANSFER_2D); gpu_rect(&gpu_command.transfer.rect);
    gpu_command.transfer.offset = 0; gpu_command.transfer.resource_id = cpu2le32(1); gpu_command.transfer.padding = 0;
    gpu_submit(sizeof(gpu_command.transfer)); gpu_flush_pending = TRUE;
}
#endif
