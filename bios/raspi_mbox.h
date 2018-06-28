/*
 * raspi_mbox.h mailbox interface for communicating with the raspberry pi mailbox
 *
 * Copyright (C) 2013-2018 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef RASPI_MBOX_H
#   define RASPI_MBOX_H
#   ifdef MACHINE_RPI

#define MAILBOX_CHANNEL_PM      0   /* power management */
#define MAILBOX_CHANNEL_FB      1   /* frame buffer */
#define MAILBOX_PROP_OUT        8   /* property tags (ARM to VC) */

ULONG raspi_mbox_read(UBYTE channel);
void raspi_mbox_write(UBYTE channel, ULONG data);

typedef struct {
    ULONG size;     /* The size of the buffer in bytes (including the header values, the end tag and padding) */
    ULONG status;   /* buffer request/response code */
    #define CODE_REQUEST            0x00000000
    #define CODE_RESPONSE_SUCCESS    0x80000000
    #define CODE_RESPONSE_FAILURE    0x80000001
    UBYTE tags[0];   /* tag array. Finish with ULONG 0x0 end tag */
}   prop_buffer_t;

typedef struct {
    ULONG tag_id;           /* tag identifier */
    ULONG value_buf_size;   /* value buffer size in bytes */
    ULONG value_length;     /* Request code (bit 31 unset) / Response code (bit 31 clear) */
    #define VALUE_LENGTH_RESPONSE    (1 << 31)
    /*UBYTE value[0]; */    /* Value, (needs to be padded to align tag with 32 bits) */
}   prop_tag_t;

typedef struct {
    prop_tag_t tag;
    ULONG      value;
}   prop_tag_1u32_t;

typedef struct {
    prop_tag_t tag;
    ULONG      value1;
    ULONG      value2;
}   prop_tag_2u32_t;

typedef struct
{
    prop_tag_t tag;
    union
    {
        ULONG   width;            /* should be >= 16 */
        ULONG   nResponse;
        #define CURSOR_RESPONSE_VALID    0    /* response */
    };
    ULONG   height;        /* should be >= 16 */
    ULONG   pad;
    ULONG   pixels;        /* physical address, format 32bpp ARGB */
    ULONG   hotspot_x;
    ULONG   hotspot_y;
} prop_tag_cursor_info_t;

typedef struct
{
    prop_tag_t tag;
    union
    {
        ULONG enable;
    #define CURSOR_ENABLE_INVISIBLE      0
    #define CURSOR_ENABLE_VISIBLE        1
        ULONG response;
    };
    ULONG   pos_x;
    ULONG   pos_y;
    ULONG   flags;
    #define CURSOR_FLAGS_DISP_COORDS    0
    #define CURSOR_FLAGS_FB_COORDS      1
} prop_tag_cursor_state_t;

typedef struct
{
    prop_tag_t tag;
    union
    {
        ULONG    offset;        /* first palette index to set (0-255) */
        ULONG    result;
        #define SET_PALETTE_VALID    0
    };
    ULONG    length;        /* number of palette entries to set (1-256) */
    ULONG    palette[256];  /* RGBA values, offset to offset+length-1 */
} prop_tag_palette_t;

#define PROPTAG_END                     0x00000000

#define PROPTAG_GET_FIRMWARE_REVISION   0x00000001
#define PROPTAG_SET_CURSOR_INFO         0x00008010
#define PROPTAG_SET_CURSOR_STATE        0x00008011
#define PROPTAG_GET_BOARD_MODEL         0x00010001
#define PROPTAG_GET_BOARD_REVISION      0x00010002
#define PROPTAG_GET_MAC_ADDRESS         0x00010003
#define PROPTAG_GET_BOARD_SERIAL        0x00010004
#define PROPTAG_GET_ARM_MEMORY          0x00010005
#define PROPTAG_GET_VC_MEMORY           0x00010006
#define PROPTAG_SET_POWER_STATE         0x00028001
#define PROPTAG_GET_CLOCK_RATE          0x00030002
#define PROPTAG_GET_MAX_CLOCK_RATE      0x00030004
#define PROPTAG_GET_TEMPERATURE         0x00030006
#define PROPTAG_GET_MIN_CLOCK_RATE      0x00030007
#define PROPTAG_GET_TURBO               0x00030009
#define PROPTAG_GET_MAX_TEMPERATURE     0x0003000A
#define PROPTAG_GET_EDID_BLOCK          0x00030020
#define PROPTAG_SET_CLOCK_RATE          0x00038002
#define PROPTAG_SET_TURBO               0x00038009
#define PROPTAG_ALLOCATE_BUFFER         0x00040001
#define PROPTAG_GET_DISPLAY_DIMENSIONS  0x00040003
#define PROPTAG_GET_PITCH               0x00040008
#define PROPTAG_GET_TOUCHBUF            0x0004000F
#define PROPTAG_GET_GPIO_VIRTBUF        0x00040010
#define PROPTAG_SET_PHYS_WIDTH_HEIGHT   0x00048003
#define PROPTAG_SET_VIRT_WIDTH_HEIGHT   0x00048004
#define PROPTAG_SET_DEPTH               0x00048005
#define PROPTAG_SET_VIRTUAL_OFFSET      0x00048009
#define PROPTAG_SET_PALETTE             0x0004800B
#define PROPTAG_WAIT_FOR_VSYNC          0x0004800E
#define PROPTAG_SET_TOUCHBUF            0x0004801F
#define PROPTAG_SET_GPIO_VIRTBUF        0x00048020
#define PROPTAG_GET_COMMAND_LINE        0x00050001

BOOL raspi_prop_get_tag(ULONG tag_id, void *tag, ULONG tag_size, ULONG request_param_size);
BOOL raspi_prop_get_tags(void *tags, ULONG tags_size);

#   endif /* MACHINE_RPI */
#endif /* RASPI_MBOX_H */
