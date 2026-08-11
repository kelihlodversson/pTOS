#include "usb_global.h"

#include "usb.h"
#include "usb_api.h"
#include "ikbd.h"               /* for kbd_int() */

/*
 * USB HID boot-keyboard class driver.
 *
 * Modeled on udd_mouse.c: probes for a HID boot-protocol keyboard
 * interface, then polls its interrupt endpoint from the raspi timer-C
 * tick (see usb_keyboard_timerc() below, hooked up next to
 * usb_mouse_timerc() in bios/arch/arm/vectors.c).
 */

static long keyboard_ioctl (struct uddif *, short, long);
static long keyboard_disconnect (struct usb_device *dev);
static long keyboard_probe (struct usb_device *dev, unsigned int ifnum);

static char lname[] = "USB keyboard class driver\0";

static struct uddif keyboard_uif = {
    0,                          /* *next */
    USB_API_VERSION,            /* API */
    USB_DEVICE,                 /* class */
    lname,                      /* lname */
    "keyboard",                 /* name */
    0,                          /* unit */
    0,                          /* flags */
    keyboard_probe,             /* probe */
    keyboard_disconnect,        /* disconnect */
    0,                          /* resrvd1 */
    keyboard_ioctl,             /* ioctl */
    0,                          /* resrvd2 */
};

struct kbd_data
{
    struct usb_device *pusb_dev;        /* this usb_device */
    unsigned char ep_int;       /* interrupt endpoint */
    unsigned long irqpipe;      /* pipe for polling */
    unsigned char irqmaxp;      /* max packet for irq pipe */
    unsigned char irqinterval;  /* interval for irq pipe */
    UBYTE modifier;              /* last modifier byte (boot report byte 0) */
    UBYTE keys[6];                /* last set of up to 6 pressed key usages */
    UBYTE new_modifier;
    UBYTE new_keys[6];
};

static struct kbd_data kbd_data;

/*
 * USB HID keyboard usage ID (Usage Page 0x07) -> Atari IKBD scancode.
 * Index 0 means "no scancode for this usage" (also covers the "error
 * rollover" usages 0x01-0x03 the boot report uses to signal more keys
 * are down than it can report). Atari's IKBD scancodes are numbered the
 * same as the IBM PC XT Scan Code Set 1 (confirmed against
 * bios/keyb_us.h and reused by bios/virtio_input_keytbl.c), but HID
 * usage IDs use their own, alphabetic-for-letters numbering, so unlike
 * the evdev table this needs an explicit entry per key rather than an
 * identity range.
 */
#define USB_KEYTBL_SIZE 0x66

static const UBYTE usb_keytbl[USB_KEYTBL_SIZE] =
{
    /* 0x00-0x03: no key / rollover error */
    0, 0, 0, 0,
    /* 0x04-0x1d: a-z */
    30, 48, 46, 32, 18, 33, 34, 35, 23, 36,
    37, 38, 50, 49, 24, 25, 16, 19, 31, 20,
    22, 47, 17, 45, 21, 44,
    /* 0x1e-0x27: 1-9, 0 */
    2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
    /* 0x28-0x2c: Enter, Escape, Backspace, Tab, Space */
    28, 1, 14, 15, 57,
    /* 0x2d-0x38: - = [ ] \ (Non-US #) ; ' ` , . / */
    12, 13, 26, 27, 43, 0, 39, 40, 41, 51,
    52, 53,
    /* 0x39: CapsLock */
    58,
    /* 0x3a-0x43: F1-F10 */
    59, 60, 61, 62, 63, 64, 65, 66, 67, 68,
    /* 0x44-0x45: F11, F12 -- no Atari scancode, unmapped */
    0, 0,
    /* 0x46-0x48: PrintScreen, ScrollLock, Pause -- unmapped */
    0, 0, 0,
    /* 0x49-0x52: Insert, Home, PageUp, Delete, End, PageDown,
     *            Right, Left, Down, Up */
    82, 71, 73, 83, 79, 81, 77, 75, 80, 72,
    /* 0x53-0x65: NumLock, keypad and Application key -- unmapped
     * (the ST numeric keypad has its own separate scancodes not
     * reachable from a PC keyboard's keypad block) */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0
};

/*
 * Modifier byte bits (boot report byte 0) -> Atari scancode. The ST
 * has one physical Ctrl and one Alt key, so left/right variants both
 * map to the same scancode (matches bios/virtio_input_keytbl.c's
 * handling of the same asymmetry). Left/Right GUI (Windows/Command)
 * have no ST equivalent and are left unmapped.
 */
static const UBYTE usb_modtbl[8] =
{
    29,   /* bit 0: LCtrl  -> Ctrl   */
    42,   /* bit 1: LShift -> LShift */
    56,   /* bit 2: LAlt   -> Alt    */
    0,    /* bit 3: LGui   -> (none) */
    29,   /* bit 4: RCtrl  -> Ctrl   */
    54,   /* bit 5: RShift -> RShift */
    56,   /* bit 6: RAlt   -> Alt    */
    0,    /* bit 7: RGui   -> (none) */
};

#define KEY_RELEASED 0x80

static long keyboard_ioctl (struct uddif *u, short cmd, long arg)
{
    return E_OK;
}

static long
keyboard_disconnect (struct usb_device *dev)
{
    if (dev == kbd_data.pusb_dev)
    {
        kbd_data.pusb_dev = NULL;
    }

    return 0;
}

static long
usb_keyboard_irq (struct usb_device *dev)
{
    return 0;
}

/* TRUE if usage 'key' (nonzero) is present anywhere in the 6-entry array */
static BOOL
usb_keyboard_key_in(const UBYTE *keys, UBYTE key)
{
    int i;

    if (!key)
        return FALSE;

    for (i = 0; i < 6; i++)
    {
        if (keys[i] == key)
            return TRUE;
    }
    return FALSE;
}

void usb_keyboard_timerc (void);
void usb_keyboard_timerc (void)
{
    long actlen = 0;
    long r;
    UBYTE changed_mod;
    int i;

    if (kbd_data.pusb_dev == NULL)
        return;

    {
        UBYTE report[8];

        r = usb_bulk_msg (kbd_data.pusb_dev,
                          kbd_data.irqpipe,
                          report,
                          kbd_data.irqmaxp > 8 ? 8 : kbd_data.irqmaxp,
                          &actlen, USB_CNTL_TIMEOUT * 5, 1);

        if ((r != 0) || (actlen < 8))
            return;

        kbd_data.new_modifier = report[0];
        for (i = 0; i < 6; i++)
            kbd_data.new_keys[i] = report[2 + i];
    }

    /* Released keys first, so a key that vanishes from one slot and
     * reappears in another in the same report doesn't get treated as
     * "still held" by the release/press ordering below. */
    for (i = 0; i < 6; i++)
    {
        UBYTE key = kbd_data.keys[i];

        if (key && key < USB_KEYTBL_SIZE &&
            !usb_keyboard_key_in(kbd_data.new_keys, key))
        {
            UBYTE scancode = usb_keytbl[key];
            if (scancode)
                kbd_int (scancode | KEY_RELEASED);
        }
    }

    changed_mod = kbd_data.modifier ^ kbd_data.new_modifier;
    for (i = 0; i < 8; i++)
    {
        if ((changed_mod & (1 << i)) && !(kbd_data.new_modifier & (1 << i)))
        {
            UBYTE scancode = usb_modtbl[i];
            if (scancode)
                kbd_int (scancode | KEY_RELEASED);
        }
    }

    for (i = 0; i < 8; i++)
    {
        if ((changed_mod & (1 << i)) && (kbd_data.new_modifier & (1 << i)))
        {
            UBYTE scancode = usb_modtbl[i];
            if (scancode)
                kbd_int (scancode);
        }
    }

    for (i = 0; i < 6; i++)
    {
        UBYTE key = kbd_data.new_keys[i];

        if (key && key < USB_KEYTBL_SIZE &&
            !usb_keyboard_key_in(kbd_data.keys, key))
        {
            UBYTE scancode = usb_keytbl[key];
            if (scancode)
                kbd_int (scancode);
        }
    }

    kbd_data.modifier = kbd_data.new_modifier;
    for (i = 0; i < 6; i++)
        kbd_data.keys[i] = kbd_data.new_keys[i];
}

static long
keyboard_probe (struct usb_device *dev, unsigned int ifnum)
{
    struct usb_interface *iface;
    struct usb_endpoint_descriptor *ep_desc;

    /*
     * Only one keyboard at a time
     */
    if (kbd_data.pusb_dev)
    {
        return -1;
    }

    if (dev == NULL)
    {
        return -1;
    }

    usb_disable_asynch (1);     /* asynch transfer not allowed */

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

    if (iface->desc.bInterfaceProtocol != 1)   /* 1 = keyboard, 2 = mouse */
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
        kbd_data.ep_int =
            ep_desc->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
        kbd_data.irqinterval = ep_desc->bInterval;
    }
    else
    {
        return -1;
    }

    kbd_data.pusb_dev = dev;

    kbd_data.irqinterval =
        (kbd_data.irqinterval > 0) ? kbd_data.irqinterval : 255;
    kbd_data.irqpipe =
        usb_rcvintpipe (kbd_data.pusb_dev, (long) kbd_data.ep_int);
    kbd_data.irqmaxp = usb_maxpacket (dev, kbd_data.irqpipe);
    dev->irq_handle = usb_keyboard_irq;
    kbd_data.modifier = 0;
    memset (kbd_data.keys, 0, sizeof (kbd_data.keys));
    memset (kbd_data.new_keys, 0, sizeof (kbd_data.new_keys));

    usb_set_protocol (dev, iface->desc.bInterfaceNumber, 0); /* boot */
    usb_set_idle (dev, iface->desc.bInterfaceNumber, 0, 0);

    KINFO (("%s: exit keyboard_probe success\n", __FILE__));

    return 0;
}

int usb_keyboard_init (void);
int usb_keyboard_init (void)
{
    long ret;

    KINFO (("%s: enter init\n", __FILE__));

    ret = udd_register (&keyboard_uif);

    if (ret)
    {
        KINFO (("%s: udd register failed! %ld\n", __FILE__, ret));
        return 1;
    }

    KINFO(("%s: udd register ok\n", __FILE__));
    return 0;
}
