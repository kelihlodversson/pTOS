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
#define VIRTIO_GPU_CMD_RESOURCE_2D    0x0101UL
#define VIRTIO_GPU_CMD_SET_SCANOUT    0x0103UL
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH 0x0104UL
#define VIRTIO_GPU_CMD_TRANSFER_2D    0x0105UL
#define VIRTIO_GPU_CMD_ATTACH_BACKING 0x0106UL
#define VIRTIO_GPU_RESP_OK_NODATA     0x1100UL
#define VIRTIO_GPU_FORMAT_B8G8R8X8    2UL
#define VIRTIO_GPU_WIDTH 640
#define VIRTIO_GPU_HEIGHT 480
#define VIRTIO_GPU_PITCH (VIRTIO_GPU_WIDTH * 4UL)
#define VIRTIO_GPU_SIZE (VIRTIO_GPU_PITCH * VIRTIO_GPU_HEIGHT)
#define VIRTIO_GPU_POLL_LIMIT 100000UL

typedef struct { ULONG type, flags; UQUAD fence_id; ULONG ctx_id, padding; } GPU_HDR;
typedef struct { ULONG x, y, width, height; } GPU_RECT;
typedef struct { GPU_HDR hdr; ULONG resource_id, format, width, height; } GPU_RESOURCE_2D;
typedef struct { GPU_HDR hdr; ULONG resource_id, nr_entries, addr_lo, addr_hi, length, entry_padding; } GPU_ATTACH;
typedef struct { GPU_HDR hdr; GPU_RECT rect; ULONG scanout_id, resource_id; } GPU_SCANOUT;
typedef struct { GPU_HDR hdr; GPU_RECT rect; UQUAD offset; ULONG resource_id, padding; } GPU_TRANSFER;
typedef struct { GPU_HDR hdr; GPU_RECT rect; ULONG resource_id, padding; } GPU_FLUSH;
typedef union { GPU_RESOURCE_2D resource; GPU_ATTACH attach; GPU_SCANOUT scanout; GPU_TRANSFER transfer; GPU_FLUSH flush; } GPU_COMMAND;

static VIRTIO_DEV gpu_dev;
static GPU_COMMAND gpu_command __attribute__((aligned(VIRTIO_CACHE_LINE)));
static GPU_HDR gpu_response __attribute__((aligned(VIRTIO_CACHE_LINE)));
static BOOL gpu_is_present, gpu_in_flight, gpu_flush_pending;

static void gpu_header(GPU_HDR *hdr, ULONG type)
{
    hdr->type = cpu2le32(type); hdr->flags = 0; hdr->fence_id = 0; hdr->ctx_id = 0; hdr->padding = 0;
}

static void gpu_rect(GPU_RECT *rect)
{
    rect->x = 0; rect->y = 0; rect->width = cpu2le32(VIRTIO_GPU_WIDTH); rect->height = cpu2le32(VIRTIO_GPU_HEIGHT);
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

static BOOL gpu_wait(void)
{
    ULONG count;
    for (count = 0; count < VIRTIO_GPU_POLL_LIMIT; count++) {
        virtio_poll(&gpu_dev);
        if (gpu_dev.done) {
            gpu_in_flight = FALSE;
            virtio_invalidate_buffer(&gpu_response, sizeof(gpu_response));
            return le2cpu32(gpu_response.type) == VIRTIO_GPU_RESP_OK_NODATA;
        }
    }
    gpu_in_flight = FALSE;
    return FALSE;
}

static BOOL gpu_submit_wait(ULONG size) { gpu_submit(size); return gpu_wait(); }

static BOOL gpu_create_resource(UBYTE *framebuffer)
{
    gpu_header(&gpu_command.resource.hdr, VIRTIO_GPU_CMD_RESOURCE_2D);
    gpu_command.resource.resource_id = cpu2le32(1); gpu_command.resource.format = cpu2le32(VIRTIO_GPU_FORMAT_B8G8R8X8);
    gpu_command.resource.width = cpu2le32(VIRTIO_GPU_WIDTH); gpu_command.resource.height = cpu2le32(VIRTIO_GPU_HEIGHT);
    if (!gpu_submit_wait(sizeof(gpu_command.resource))) return FALSE;
    gpu_header(&gpu_command.attach.hdr, VIRTIO_GPU_CMD_ATTACH_BACKING);
    gpu_command.attach.resource_id = cpu2le32(1); gpu_command.attach.nr_entries = cpu2le32(1);
    gpu_command.attach.addr_lo = cpu2le32((ULONG)framebuffer + gpu_dev.phys_offset); gpu_command.attach.addr_hi = 0;
    gpu_command.attach.length = cpu2le32(VIRTIO_GPU_SIZE); gpu_command.attach.entry_padding = 0;
    if (!gpu_submit_wait(sizeof(gpu_command.attach))) return FALSE;
    gpu_header(&gpu_command.scanout.hdr, VIRTIO_GPU_CMD_SET_SCANOUT);
    gpu_rect(&gpu_command.scanout.rect); gpu_command.scanout.scanout_id = 0; gpu_command.scanout.resource_id = cpu2le32(1);
    return gpu_submit_wait(sizeof(gpu_command.scanout));
}

void virtio_gpu_init(void)
{
    UBYTE *framebuffer;
    WORD slot;
    gpu_is_present = FALSE; gpu_in_flight = FALSE; gpu_flush_pending = FALSE;
    if (!virtio_find_device(VIRTIO_ID_GPU, 0, &gpu_dev, &slot) || !virtio_setup_queue(&gpu_dev)) return;
    framebuffer = balloc_stram(VIRTIO_GPU_SIZE, TRUE);
    if (!gpu_create_resource(framebuffer)) { KDEBUG(("virtio_gpu: setup failed at slot %d\n", slot)); return; }
    v_bas_ad = framebuffer; gpu_is_present = TRUE;
    KDEBUG(("virtio_gpu: %dx%d B8G8R8X8 scanout at slot %d\n", VIRTIO_GPU_WIDTH, VIRTIO_GPU_HEIGHT, slot));
}

BOOL virtio_gpu_present(void) { return gpu_is_present; }

void virtio_gpu_get_current_mode_desc(SCREEN_MODE_DESC *desc)
{
    desc->width = VIRTIO_GPU_WIDTH; desc->height = VIRTIO_GPU_HEIGHT; desc->pitch = VIRTIO_GPU_PITCH;
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
    virtio_flush_buffer(v_bas_ad, VIRTIO_GPU_SIZE);
    gpu_header(&gpu_command.transfer.hdr, VIRTIO_GPU_CMD_TRANSFER_2D); gpu_rect(&gpu_command.transfer.rect);
    gpu_command.transfer.offset = 0; gpu_command.transfer.resource_id = cpu2le32(1); gpu_command.transfer.padding = 0;
    gpu_submit(sizeof(gpu_command.transfer)); gpu_flush_pending = TRUE;
}
#endif
