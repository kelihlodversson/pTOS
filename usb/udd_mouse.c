#include "usb_global.h"

#include "usb.h"
#include "usb_api.h"
#include "ikbd.h"               /* for call_mousevec() */
#include "tosvars.h"            /* for mousexvec() */

/****************************************************************************/
/*
 * BEGIN kernel interface
 */

extern unsigned long _PgmSize;

/*
 * old handler
 */
extern void (*old_ikbd_int) (void);

/*
 * interrupt wrapper routine
 */
extern void interrupt_ikbd (void);

static UBYTE mouse_packet[6];
void usb_mouse_timerc (void);

/*
 * END kernel interface
 */
/****************************************************************************/

/*
 * USB device interface
 */

static long mouse_ioctl (struct uddif *, short, long);
static long mouse_disconnect (struct usb_device *dev);
static long mouse_probe (struct usb_device *dev, unsigned int ifnum);

static char lname[] = "USB mouse class driver\0";

static struct uddif mouse_uif = {
	0,                          /* *next */
	USB_API_VERSION,            /* API */
	USB_DEVICE,                 /* class */
	lname,                      /* lname */
	"mouse",                    /* name */
	0,                          /* unit */
	0,                          /* flags */
	mouse_probe,                /* probe */
	mouse_disconnect,           /* disconnect */
	0,                          /* resrvd1 */
	mouse_ioctl,                /* ioctl */
	0,                          /* resrvd2 */
};

struct mse_data
{
	struct usb_device *pusb_dev;        /* this usb_device */
	unsigned char ep_in;        /* in endpoint */
	unsigned char ep_out;       /* out ....... */
	unsigned char ep_int;       /* interrupt . */
	long *irq_handle;            /* for USB int requests */
	unsigned long irqpipe;      /* pipe for release_irq */
	unsigned char irqmaxp;      /* max packed for irq Pipe */
	unsigned char irqinterval;  /* Intervall for IRQ Pipe */
	char data[8];
	char new[8];
};

static struct mse_data mse_data;

/*
 * --- Inteface functions
 * --------------------------------------------------
 */

static long mouse_ioctl (struct uddif *u, short cmd, long arg)
{
	return E_OK;
}

/*
 * -------------------------------------------------------------------------
 */

/*******************************************************************************
 *
 *
 */
static long
mouse_disconnect (struct usb_device *dev)
{
	if (dev == mse_data.pusb_dev)
	{
		mse_data.pusb_dev = NULL;
	}

	return 0;
}

static long
usb_mouse_irq (struct usb_device *dev)
{
	return 0;
}

void usb_mouse_timerc (void)
{
	long i, change = 0;
	long actlen = 0;
	long r;

	if (mse_data.pusb_dev == NULL)
		return;

	r = usb_bulk_msg (mse_data.pusb_dev,
					  mse_data.irqpipe,
					  mse_data.new,
					  mse_data.irqmaxp > 8 ? 8 : mse_data.irqmaxp,
					  &actlen, USB_CNTL_TIMEOUT * 5, 1);

	if ((r != 0) || (actlen < 3) || (actlen > 8))
	{
		return;
	}
	for (i = 0; i < actlen; i++)
	{
		if (mse_data.new[i] != mse_data.data[i])
		{
			change = 1;
			break;
		}
	}
	if (change)
	{
		char info, old_info;
		char delta_x, delta_y;
		char extra, old_extra;

		(void)old_extra;
		#if 0
		if ((actlen >= 6) && (mse_data.new[0] == 1))
		{					   /* report-ID */
			info = mse_data.new[1];
			old_info = mse_data.data[1];
			delta_x = mse_data.new[2];
			delta_y = mse_data.new[3];
			extra = mse_data.new[4];
			old_extra = mse_data.data[4];
		}
		else
		#endif
		{					   /* boot report */
			info = mse_data.new[0];
			old_info = mse_data.data[0];
			delta_x = mse_data.new[1];
			delta_y = mse_data.new[2];
			if (actlen >= 3)
			{
				extra = mse_data.new[3];
				old_extra = mse_data.data[3];
			}
		}

		// Build ikbd mouse packet
		mouse_packet[0] =
			((info & 1) << 1) |
			((info & 2) >> 1) | 0xF8;
		mouse_packet[1] = delta_x;
		mouse_packet[2] = delta_y;

		if ((info ^ old_info) & 4)
		{					   /* 3rd button */
			mousexvec ((info & 4)?0x37:0xb7);
		}

#if 0
	// TODO: some mice flip these bits on mouse wheel movement if they don't have a 4th and a 5th button
		if ((extra ^ old_extra) & 0x10)
		{					   /* 4th button */
			mousexvec ((extra & 0x10)?0x5e:0xde);
		}
		if ((extra ^ old_extra) & 0x20)
		{					   /* 5th button */
			mousexvec ((extra & 0x20)?0x5f:0xdf);
		}
#endif
		// Mouse wheel is stored in the low nybble of the extra byte
		switch (extra & 0xf)
		{
			case 0x1:		/* Wheel up */
			mousexvec (0x59);
			break;
			case 0x2:		/* Wheel right */
			mousexvec (0x5d);
			break;
			case 0xe:		/* Wheel left */
			mousexvec (0x5c);
			break;
			case 0xf:		/* Wheel down */
			mousexvec (0x5a);
			break;
			case 0:
			default:
			break; // Default is no movement
		}

		call_mousevec(mouse_packet);
		mse_data.data[0] = mse_data.new[0];
		mse_data.data[1] = mse_data.new[1];
		mse_data.data[2] = mse_data.new[2];
		mse_data.data[3] = mse_data.new[3];
		mse_data.data[4] = mse_data.new[4];
		mse_data.data[5] = mse_data.new[5];
	}
}

/*******************************************************************************
 *
 *
 */
static long
mouse_probe (struct usb_device *dev, unsigned int ifnum)
{
	struct usb_interface *iface;
	struct usb_endpoint_descriptor *ep_desc;

	/*
	 * Only one mouse at time
	 */
	if (mse_data.pusb_dev)
	{
		return -1;
	}

	if (dev == NULL)
	{
		return -1;
	}

	usb_disable_asynch (1);     /* asynch transfer not allowed */

	/*
	 * let's examine the device now
	 */
	iface = &dev->config.if_desc[ifnum];
	if (!iface)
	{
		return -1;
	}

	if (iface->desc.bInterfaceClass != USB_CLASS_HID)
	{
		return -1;
	}

	if (iface->desc.bInterfaceSubClass != USB_SUB_HID_BOOT)
	{
		return -1;
	}

	if (iface->desc.bInterfaceProtocol != 2)
	{
		return -1;
	}

	if (iface->desc.bNumEndpoints != 1)
	{
		return -1;
	}

	ep_desc = &iface->ep_desc[0];
	if (!ep_desc)
	{
		return -1;
	}

	if ((ep_desc->bmAttributes &
		 USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT)
	{
		mse_data.ep_int =
			ep_desc->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
		mse_data.irqinterval = ep_desc->bInterval;
	}
	else
	{
		return -1;
	}

	mse_data.pusb_dev = dev;

	mse_data.irqinterval =
		(mse_data.irqinterval > 0) ? mse_data.irqinterval : 255;
	mse_data.irqpipe =
		usb_rcvintpipe (mse_data.pusb_dev, (long) mse_data.ep_int);
	mse_data.irqmaxp = usb_maxpacket (dev, mse_data.irqpipe);
	dev->irq_handle = usb_mouse_irq;
	memset (mse_data.data, 0, 8);
	memset (mse_data.new, 0, 8);

	// if(mse_data.irqmaxp < 6)
	usb_set_protocol(dev, iface->desc.bInterfaceNumber, 0); /* boot */
	// else
	//usb_set_protocol (dev, iface->desc.bInterfaceNumber, 1);    /* report */

	usb_set_idle (dev, iface->desc.bInterfaceNumber, 0, 0);     /* report
                                                                 * infinite
                                                                 */

KINFO (("%s: exit mouse_probe success\n", __FILE__));

	return 0;
}

int usb_mouse_init (void);
int usb_mouse_init (void)
{
	long ret;

	KINFO (("%s: enter init\n", __FILE__));

	ret = udd_register (&mouse_uif);

	if (ret)
	{
		KINFO (("%s: udd register failed! %ld\n", __FILE__, ret));
		return 1;
	}

	KINFO(("%s: udd register ok\n", __FILE__));
	return 0;
}
