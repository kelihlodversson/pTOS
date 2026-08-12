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
	unsigned char ep_int;       /* interrupt . */
	unsigned long irqpipe;      /* pipe for release_irq */
	unsigned char irqmaxp;      /* max packed for irq Pipe */
	unsigned char irqinterval;  /* Intervall for IRQ Pipe */
	char data[8];
	UBYTE report[8];
};

static struct mse_data mse_data;
static struct usb_async_int_msg mouse_request;

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
		usb_cancel_async_int_msg(&mouse_request);
		mse_data.pusb_dev = NULL;
	}

	return 0;
}

static void mouse_report_complete(struct usb_async_int_msg *msg,
                                  LONG status, LONG actual_length)
{
	UBYTE info;
	UBYTE old_info;
	BYTE delta_x;
	BYTE delta_y;
	UBYTE extra;
	BOOL changed;

	if (status || actual_length < 3 || actual_length > 8)
	{
		KDEBUG(("usb mouse interrupt transfer failed (%ld, %ld)\n",
			status, actual_length));
		return;
	}

	info = mse_data.report[0];
	old_info = mse_data.data[0];
	delta_x = mse_data.report[1];
	delta_y = mse_data.report[2];
	extra = (actual_length >= 4) ? mse_data.report[3] : 0;
	changed = (info != old_info) || delta_x || delta_y
		|| ((actual_length >= 4) && (extra != mse_data.data[3]));
	if (!changed)
		return;

	mouse_packet[0] = ((info & 1) << 1) | ((info & 2) >> 1) | 0xf8;
	mouse_packet[1] = delta_x;
	mouse_packet[2] = delta_y;

	if ((info ^ old_info) & 4)
		mousexvec((info & 4) ? 0x37 : 0xb7);

	switch (extra & 0x0f)
	{
	case 0x1:
		mousexvec(0x59);
		break;
	case 0x2:
		mousexvec(0x5d);
		break;
	case 0xe:
		mousexvec(0x5c);
		break;
	case 0xf:
		mousexvec(0x5a);
		break;
	default:
		break;
	}

	call_mousevec(mouse_packet);
	mse_data.data[0] = info;
	mse_data.data[1] = delta_x;
	mse_data.data[2] = delta_y;
	if (actual_length >= 4)
		mse_data.data[3] = extra;
	else
		mse_data.data[3] = 0;
	mse_data.data[4] = 0;
	mse_data.data[5] = 0;
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

	mse_data.irqinterval =
		(mse_data.irqinterval > 0) ? mse_data.irqinterval : 255;
	mse_data.irqpipe =
		usb_rcvintpipe (dev, (long) mse_data.ep_int);
	mse_data.irqmaxp = usb_maxpacket (dev, mse_data.irqpipe);
	memset (mse_data.data, 0, 8);
	memset (mse_data.report, 0, 8);

	// if(mse_data.irqmaxp < 6)
	usb_set_protocol(dev, iface->desc.bInterfaceNumber, 0); /* boot */
	// else
	//usb_set_protocol (dev, iface->desc.bInterfaceNumber, 1);    /* report */

	usb_set_idle (dev, iface->desc.bInterfaceNumber, 0, 0);     /* report
	                                                              * infinite
	                                                              */
	mouse_request.dev = dev;
	mouse_request.pipe = mse_data.irqpipe;
	mouse_request.buffer = mse_data.report;
	mouse_request.transfer_len = mse_data.irqmaxp > 8 ? 8 : mse_data.irqmaxp;
	mouse_request.interval = mse_data.irqinterval;
	mouse_request.callback = mouse_report_complete;
	mouse_request.context = &mse_data;
	mse_data.pusb_dev = dev;

	if (usb_submit_async_int_msg(&mouse_request))
	{
		mse_data.pusb_dev = NULL;
		return -1;
	}

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
