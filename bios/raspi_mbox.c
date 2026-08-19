#include "config.h"
#ifndef MACHINE_RPI
#error This file must only be compiled for raspberry PI targets
#endif

#include "portab.h"
#include "raspi_io.h"
#include "raspi_int.h"
#include "raspi_mbox.h"
#include "raspi_memory.h"
#include "tosvars.h"
#include "string.h"
#include "kprint.h"
#include "asm.h"
#include "vectors.h"


#define MAILBOX_BASE        (ARM_IO_BASE + 0xB880)

#define MAILBOX0_READ       (*(volatile ULONG*)(MAILBOX_BASE + 0x00))
#define MAILBOX0_STATUS     (*(volatile ULONG*)(MAILBOX_BASE + 0x18))
#define MAILBOX_STATUS_EMPTY    0x40000000
#define MAILBOX1_WRITE      (*(volatile ULONG*)(MAILBOX_BASE + 0x20))
#define MAILBOX1_STATUS     (*(volatile ULONG*)(MAILBOX_BASE + 0x38))
#define MAILBOX_STATUS_FULL     0x80000000

/* Real VideoCore firmware always answers a mailbox request, including
 * with an error for a tag it does not support, but an emulator can
 * simply never reply to a request it does not implement (e.g. QEMU's
 * bcm2835-property device silently drops the hardware-cursor property
 * tags instead of returning an unsupported-tag response). Bound the
 * wait so that gap does not hang the whole boot: 500 ticks (2.5s at
 * 200Hz) matches the WAIT_TIMEOUT convention already used elsewhere
 * (e.g. AES's wait_for_accs()) and is far longer than any real
 * exchange takes. */
#define MBOX_READ_TIMEOUT   500

ULONG raspi_mbox_read(UBYTE channel)
{
    ULONG data;
    UBYTE read_channel;
    LONG deadline = hz_200 + MBOX_READ_TIMEOUT;

    while(1)
    {
        while(MAILBOX0_STATUS & MAILBOX_STATUS_EMPTY)
        {
            /* Loop until message is available or we give up waiting.
             * 0 is safe as a "gave up" sentinel: raspi_prop_get_tags()
             * only accepts a reply that matches the real, non-zero
             * gpu_buffer_address it sent, so this always falls through
             * to its existing failure path. */
            if (hz_200 - deadline >= 0)
                return 0;
        }
        data = MAILBOX0_READ;
        read_channel = data & 0xF;
        data >>= 4;
        if (read_channel == channel)
            return data;
    }
}

void raspi_mbox_write(UBYTE channel, ULONG data)
{
    while(MAILBOX1_STATUS & MAILBOX_STATUS_FULL)
    {
        // Loop until there is room in the fifo
    }
    // Write the message
    MAILBOX1_WRITE = ((data << 4) | (channel & 0xf));
}

BOOL raspi_prop_get_tag(ULONG tag_id, void *tag, ULONG tag_size, ULONG request_param_size)
{
	prop_tag_t *tag_header = (prop_tag_t *) tag;
	tag_header->tag_id = tag_id;
	tag_header->value_buf_size = tag_size - sizeof (prop_tag_t);
	tag_header->value_length = request_param_size & ~VALUE_LENGTH_RESPONSE;

    if (!raspi_prop_get_tags(tag, tag_size))
    {
        return FALSE;
    }

	if (!(tag_header->value_length & VALUE_LENGTH_RESPONSE))
	{
		return FALSE;
	}

	tag_header->value_length &= ~VALUE_LENGTH_RESPONSE;
	if (tag_header->value_length == 0)
	{
		return FALSE;
	}

	return TRUE;
}

BOOL raspi_prop_get_tags(void *tags, ULONG tags_size)
{
    prop_buffer_t* buffer = (prop_buffer_t*)raspi_get_coherent_buffer(COHERENT_TAG_MAILBOX);
	ULONG buffer_size = sizeof (prop_buffer_t) + tags_size + sizeof (ULONG);
    ULONG *end_tag = (ULONG *) (buffer->tags + tags_size);
    ULONG gpu_buffer_address = (GPU_MEM_BASE + (ULONG) buffer)>>4;

	buffer->size = buffer_size;
	buffer->status = CODE_REQUEST;
	memcpy (buffer->tags, tags, tags_size);
	*end_tag = PROPTAG_END;

	data_sync_barrier();

    raspi_mbox_write(MAILBOX_PROP_OUT, gpu_buffer_address);
	if (raspi_mbox_read(MAILBOX_PROP_OUT) != gpu_buffer_address)
	{
		return FALSE;
	}

	data_mem_barrier();

	if (buffer->status != CODE_RESPONSE_SUCCESS)
	{
		return FALSE;
	}

	memcpy (tags, buffer->tags, tags_size);

	return TRUE;
}
