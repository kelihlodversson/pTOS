/*
 * Modified for the FreeMiNT USB subsystem by David Galvez. 2010 - 2011
 * Modified for Atari by Didier Mequignon 2009
 *
 * Most of this source has been derived from the Linux USB
 * project:
 * (C) Copyright Linus Torvalds 1999
 * (C) Copyright Johannes Erdfelt 1999-2001
 * (C) Copyright Andreas Gal 1999
 * (C) Copyright Gregory P. Smith 1999
 * (C) Copyright Deti Fliegl 1999 (new USB architecture)
 * (C) Copyright Randy Dunlap 2000
 * (C) Copyright David Brownell 2000 (kernel hotplug, usb_device_id)
 * (C) Copyright Yggdrasil Computing, Inc. 2000
 *     (usb_device_id matching changes by Adam J. Richter)
 *
 * Adapted for U-Boot:
 * (C) Copyright 2001 Denis Peter, MPL AG Switzerland
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
 */

/*#define ENABLE_KDEBUG*/

#include "usb_global.h"
#include "usb.h"
#include "usb_hub.h"
#include "endian.h"


/****************************************************************************
 * HUB "Driver"
 * Probes device for being a hub and configurate it
 */

static struct usb_hub_device hub_dev[USB_MAX_HUB];
extern struct usb_device usb_dev[USB_MAX_DEVICE];

long usb_get_hub_descriptor(struct usb_device *dev, void *data, long size)
{
    return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
            USB_REQ_GET_DESCRIPTOR, USB_DIR_IN | USB_RT_HUB,
            USB_DT_HUB << 8, 0, data, size, USB_CNTL_TIMEOUT);
}

long usb_clear_port_feature(struct usb_device *dev, long port, long feature)
{
    return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
                USB_REQ_CLEAR_FEATURE, USB_RT_PORT, feature,
                port, NULL, 0, USB_CNTL_TIMEOUT);
}

long usb_set_port_feature(struct usb_device *dev, long port, long feature)
{
    return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
                USB_REQ_SET_FEATURE, USB_RT_PORT, feature,
                port, NULL, 0, USB_CNTL_TIMEOUT);
}

long usb_get_hub_status(struct usb_device *dev, void *data)
{
    return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
            USB_REQ_GET_STATUS, USB_DIR_IN | USB_RT_HUB, 0, 0,
            data, sizeof(struct usb_hub_status), USB_CNTL_TIMEOUT);
}

long usb_clear_hub_feature(struct usb_device *dev, long feature)
{
    return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
                USB_REQ_CLEAR_FEATURE, USB_RT_HUB, feature,
                0, NULL, 0, USB_CNTL_TIMEOUT);
}

long usb_get_port_status(struct usb_device *dev, long port, void *data)
{
    return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
            USB_REQ_GET_STATUS, USB_DIR_IN | USB_RT_PORT, 0, port,
            data, sizeof(struct usb_hub_status), USB_CNTL_TIMEOUT);
}


static void usb_hub_power_on(struct usb_device *dev, unsigned pgood_delay)
{
    long i;
    struct usb_port_status portsts;
    unsigned short portstatus;
    long ret;
pgood_delay = 500;
    /*
     * Enable power to the ports:
     * Here we Power-cycle the ports: aka,
     * turning them off and turning on again.
     */
    KDEBUG(("enabling power on all ports\n"));
    for (i = 0; i < dev->maxchild; i++) {
        usb_clear_port_feature(dev, i + 1, USB_PORT_FEAT_POWER);
        KDEBUG(("port %d returns %lX\n", i + 1, dev->status));
    }

    /* Wait at least 2*bPwrOn2PwrGood for PP to change */
    KDEBUG(("pgood_delay: %lu\n", pgood_delay));
    mdelay(pgood_delay);

    for (i = 0; i < dev->maxchild; i++) {
        ret = usb_get_port_status(dev, i + 1, &portsts);
        if (ret < 0) {
            KDEBUG(("port %d: get_port_status failed\n", i + 1));
            return;
        }

        /*
         * Check to confirm the state of Port Power:
         * xHCI says "After modifying PP, s/w shall read
         * PP and confirm that it has reached the desired state
         * before modifying it again, undefined behavior may occur
         * if this procedure is not followed".
         * EHCI doesn't say anything like this, but no harm in keeping
         * this.
         */
        portstatus = le2cpu16(portsts.wPortStatus);
        if (portstatus & (USB_PORT_STAT_POWER << 1)) {
            KDEBUG(("port %d: Port power change failed\n", i + 1));
            return;
        }
    }

    for (i = 0; i < dev->maxchild; i++) {
        usb_set_port_feature(dev, i + 1, USB_PORT_FEAT_POWER);
        KDEBUG(("port %ld returns %lx\n", i + 1, dev->status));
        mdelay(pgood_delay);
    }

    /* Wait at least 100 msec for power to become stable */
    mdelay((pgood_delay>100)?pgood_delay:(unsigned)100);
}

void usb_hub_reset(void)
{
    memset(hub_dev, 0, sizeof(hub_dev));
}

struct usb_hub_device *usb_get_hub_index(long index)
{
    struct usb_device *dev;

    if (index >= USB_MAX_HUB)
        return NULL;

    dev = hub_dev[index].pusb_dev;
    if (dev && dev->devnum != -1 && dev->devnum != 0)
        return &hub_dev[index];
    else
        return NULL;
}

struct usb_hub_device *usb_hub_allocate(void)
{
    long i;

    for (i = 0; i < USB_MAX_HUB; i++) {
        if (!hub_dev[i].pusb_dev)
            return &hub_dev[i];
    }

    ALERT(("ERROR: USB_MAX_HUB (%d) reached", USB_MAX_HUB));

    return NULL;
}

void usb_hub_disconnect(struct usb_device *dev)
{
    long i;

    for (i = 0; i < USB_MAX_HUB; i++) {
        if (dev == hub_dev[i].pusb_dev) {
            memset(&hub_dev[i], 0, sizeof(struct usb_hub_device));
            break;
        }
    }
}

#define MAX_TRIES 5

static inline char *portspeed(long portstatus)
{
    char *speed_str;

    switch (portstatus & USB_PORT_STAT_SPEED_MASK) {
    case USB_PORT_STAT_SUPER_SPEED:
        speed_str = "5 Gb/s";
        break;
    case USB_PORT_STAT_HIGH_SPEED:
        speed_str = "480 Mb/s";
        break;
    case USB_PORT_STAT_LOW_SPEED:
        speed_str = "1.5 Mb/s";
        break;
    default:
        speed_str = "12 Mb/s";
        break;
    }

    return speed_str;
}

long hub_port_reset(struct usb_device *dev, long port)
{
    long tries;
    struct usb_port_status portsts;
    unsigned short portstatus, portchange;

    KDEBUG(("hub_port_reset: resetting port %ld...\n", port));
    for (tries = 0; tries < MAX_TRIES; tries++) {
        usb_set_port_feature(dev, port + 1, USB_PORT_FEAT_RESET);
        mdelay(150);

        if (usb_get_port_status(dev, port + 1, &portsts) < 0) {
            KDEBUG(("get_port_status failed status %lx\n",
                    dev->status));
            return -1;
        }
        portstatus = le2cpu16(portsts.wPortStatus);
        portchange = le2cpu16(portsts.wPortChange);

        KDEBUG(("portstatus %x, change %x, %s\n",
                portstatus, portchange,
                portspeed(portstatus)));

        KDEBUG(("STAT_C_CONNECTION = %d STAT_CONNECTION = %d" \
                   "  USB_PORT_STAT_ENABLE %d\n",
            (portchange & USB_PORT_STAT_C_CONNECTION) ? 1 : 0,
            (portstatus & USB_PORT_STAT_CONNECTION) ? 1 : 0,
            (portstatus & USB_PORT_STAT_ENABLE) ? 1 : 0));

        if ((portchange & USB_PORT_STAT_C_CONNECTION) ||
            !(portstatus & USB_PORT_STAT_CONNECTION))
            return -1;

        if (portstatus & USB_PORT_STAT_ENABLE)
            break;
    }

    if (tries == MAX_TRIES) {
        KDEBUG(("Cannot enable port %li after %i retries, " \
                "disabling port.\n", port + 1, MAX_TRIES));
        KDEBUG(("Maybe the USB cable is bad?\n"));
        return -1;
    }

    usb_clear_port_feature(dev, port + 1, USB_PORT_FEAT_C_RESET);

    return 0;
}


long usb_hub_port_connect_change(struct usb_device *dev, long port, unsigned short portstatus)
{
    struct usb_device *usb;

    /* Clear the connection change status */
    usb_clear_port_feature(dev, port + 1, USB_PORT_FEAT_C_CONNECTION);

    /* Disconnect any existing devices under this port */
    if (((!(portstatus & USB_PORT_STAT_CONNECTION)) &&
         (!(portstatus & USB_PORT_STAT_ENABLE))) || (dev->children[port])) {
        usb_disconnect(dev->children[port]);
        dev->children[port] = NULL;
        /* Return now if nothing is connected */
        if (!(portstatus & USB_PORT_STAT_CONNECTION)) {
            return 0;
        }
    }

    /* Reset the port */
    if (hub_port_reset(dev, port) < 0) {
        KDEBUG(("cannot reset port %li!?\n", port + 1));
        return -1;
    }

    /* Allocate a new device struct for it */
    usb = usb_alloc_new_device(dev->controller);
    if (!usb)
    {
        return -1;
    }

    switch (portstatus & USB_PORT_STAT_SPEED_MASK) {
    case USB_PORT_STAT_SUPER_SPEED:
        usb->speed = USB_SPEED_SUPER;
        break;
    case USB_PORT_STAT_HIGH_SPEED:
        usb->speed = USB_SPEED_HIGH;
        break;
    case USB_PORT_STAT_LOW_SPEED:
        usb->speed = USB_SPEED_LOW;
        break;
    default:
        usb->speed = USB_SPEED_FULL;
        break;
    }

    usb->portnr = port + 1;
    dev->children[port] = usb;
    usb->parent = dev;
    /* Run it through the hoops (find a driver, etc) */
    if (usb_new_device(usb)) {
        /* Ensure device is cleared. */
        usb_free_device(usb->devnum);
        /* Woops, disable the port */
        dev->children[port] = NULL;
        KDEBUG(("hub: disabling port %ld\n", port + 1));
        usb_clear_port_feature(dev, port + 1, USB_PORT_FEAT_ENABLE);
    }

    return 1;
}


struct usb_hub_device *
usb_hub_configure(struct usb_device *dev)
{
    struct usb_hub_descriptor *descriptor;
    struct usb_hub_status *hubsts;
    long i;
    struct usb_hub_device *hub;
    unsigned char buffer[USB_BUFSIZ];
    int ret;

    /* "allocate" Hub device */
    hub = usb_hub_allocate();
    if (hub == NULL)
        return NULL;

    if (usb_get_hub_descriptor(dev, buffer, 4) < 0) {
        KDEBUG(("usb_hub_configure: failed to get hub " \
                   "descriptor, giving up %lx\n", dev->status));
        hub = NULL;
        goto errout;
    }
    descriptor = (struct usb_hub_descriptor *)buffer;

    /* silence compiler warning if USB_BUFSIZ is > 256 [= sizeof(char)] */
    i = descriptor->bLength;
    if (i > USB_BUFSIZ) {
        KDEBUG(("usb_hub_configure: failed to get hub " \
                "descriptor - too long: %d\n",
                descriptor->bLength));
        hub = NULL;
        goto errout;
    }

    if (usb_get_hub_descriptor(dev, buffer, descriptor->bLength) < 0) {
        KDEBUG(("usb_hub_configure: failed to get hub " \
                "descriptor 2nd giving up %lx\n", dev->status));
        hub = NULL;
        goto errout;
    }
    memcpy((unsigned char *)&hub->desc, buffer, descriptor->bLength);
    /* adjust 16bit values */
    hub->desc.wHubCharacteristics =
                le2cpu16(descriptor->wHubCharacteristics);

    /* devices not removable by default */
    for (i = 0; i < ((hub->desc.bNbrPorts + 1 + 7)/8); i++)
        hub->desc.DeviceRemovable[i] = descriptor->DeviceRemovable[i];
    for (; i < ((USB_MAXCHILDREN + 1 + 7)/8); i++)
        hub->desc.DeviceRemovable[i] = 0xff;

    for (i = 0; i < ((hub->desc.bNbrPorts + 1 + 7)/8); i++)
        hub->desc.PortPowerCtrlMask[i] = descriptor->PortPowerCtrlMask[i];
    for (; i < ((USB_MAXCHILDREN + 1 + 7)/8); i++)
        hub->desc.PortPowerCtrlMask[i] = 0xff;

    dev->maxchild = descriptor->bNbrPorts;
    KDEBUG(("%ld ports detected\n", dev->maxchild));

    switch (hub->desc.wHubCharacteristics & HUB_CHAR_LPSM) {
    case 0x00:
        KDEBUG(("ganged power switching\n"));
        break;
    case 0x01:
        KDEBUG(("individual port power switching\n"));
        break;
    case 0x02:
    case 0x03:
        KDEBUG(("unknown reserved power switching mode\n"));
        break;
    }

    if (hub->desc.wHubCharacteristics & HUB_CHAR_COMPOUND) {
        KDEBUG(("part of a compound device\n"));
    } else {
        KDEBUG(("standalone hub\n"));
    }

    switch (hub->desc.wHubCharacteristics & HUB_CHAR_OCPM) {
    case 0x00:
        KDEBUG(("global over-current protection\n"));
        break;
    case 0x08:
        KDEBUG(("individual port over-current protection\n"));
        break;
    case 0x10:
    case 0x18:
        KDEBUG(("no over-current protection\n"));
        break;
    }

    KDEBUG(("power on to power good time: %dms\n",
            descriptor->bPwrOn2PwrGood * 2));
    KDEBUG(("hub controller current requirement: %dmA\n",
            descriptor->bHubContrCurrent));

    for (i = 0; i < dev->maxchild; i++)
        KDEBUG(("port %ld is%s removable\n", i + 1,
            hub->desc.DeviceRemovable[(i + 1) / 8] & \
                       (1 << ((i + 1) % 8)) ? " not" : ""));

    if (sizeof(struct usb_hub_status) > USB_BUFSIZ) {
        KDEBUG(("usb_hub_configure: failed to get Status - " \
                "too long: %d\n", descriptor->bLength));
        hub = NULL;
        goto errout;
    }

    if (usb_get_hub_status(dev, buffer) < 0) {
        KDEBUG(("usb_hub_configure: failed to get Status %lx\n",
                dev->status));
        hub = NULL;
        goto errout;
    }

    hubsts = (struct usb_hub_status *)buffer;
    UNUSED(hubsts);
    KDEBUG(("get_hub_status returned status %x, change %x\n",
            le2cpu16(hubsts->wHubStatus),
            le2cpu16(hubsts->wHubChange)));
    KDEBUG(("local power source is %s\n",
        (le2cpu16(hubsts->wHubStatus) & HUB_STATUS_LOCAL_POWER) ? \
        "lost (inactive)" : "good"));
    KDEBUG(("%sover-current condition exists\n",
        (le2cpu16(hubsts->wHubStatus) & HUB_STATUS_OVERCURRENT) ? \
        "" : "no "));

    usb_hub_power_on(dev, hub->desc.bPwrOn2PwrGood * 2);

    hub->pusb_dev = dev;

errout:
    return hub;
}

long
usb_hub_events(struct usb_hub_device *hub)
{
    long i;
    struct usb_device *dev = hub->pusb_dev;
    struct usb_hub_status hubsts;
    long changed = 0;

    for (i = 0; i < dev->maxchild; i++)
    {
        struct usb_port_status portsts;
        unsigned short portstatus, portchange;
        (void) portstatus;

        if (usb_get_port_status(dev, i + 1, &portsts) < 0)
        {
            KDEBUG(("get_port_status failed\n"));
            continue;
        }

        portstatus = le2cpu16(portsts.wPortStatus);
        portchange = le2cpu16(portsts.wPortChange);
        KDEBUG(("Port %ld Status %x Change %x\n",
                i + 1, portstatus, portchange));

        if (portchange & USB_PORT_STAT_C_CONNECTION)
        {
            changed |= USB_PORT_STAT_C_CONNECTION;
            KDEBUG(("port %ld connection change\n", i + 1));

            /* If something disconnected, return now, as it
             * could have been our hub.
             */
            if (usb_hub_port_connect_change(dev, i, portstatus) == 0)
            {
                return changed;
            }
        }

        if (portchange & USB_PORT_STAT_C_ENABLE)
        {
            changed |= USB_PORT_STAT_C_ENABLE;
            KDEBUG(("port %ld enable change, status %x\n",
                    i + 1, portstatus));
            usb_clear_port_feature(dev, i + 1,
                        USB_PORT_FEAT_C_ENABLE);

            /* EM interference sometimes causes bad shielded USB
             * devices to be shutdown by the hub, this hack enables
             * them again. Works at least with mouse driver */
            if (!(portstatus & USB_PORT_STAT_ENABLE) &&
                 (portstatus & USB_PORT_STAT_CONNECTION) &&
                 ((dev->children[i])))
            {
                KDEBUG(("already running port %i \n"  \
                        "disabled by hub (EMI?), " \
                        "re-enabling...", i + 1));

                /* If something disconnected, return now, as it
                 * could have been our hub.
                 */
                if (usb_hub_port_connect_change(dev, i, portstatus) == 0)
                {
                    return changed;
                }
            }
        }
        if (portstatus & USB_PORT_STAT_SUSPEND)
        {
            changed |= USB_PORT_STAT_SUSPEND;
            KDEBUG(("port %ld suspend change\n", i + 1);
            usb_clear_port_feature(dev, i + 1,
                        USB_PORT_FEAT_SUSPEND));
        }

        if (portchange & USB_PORT_STAT_C_OVERCURRENT)
        {
            changed |= USB_PORT_STAT_C_OVERCURRENT;
            KDEBUG(("port %ld over-current change\n", i + 1));
            usb_clear_port_feature(dev, i + 1,
                        USB_PORT_FEAT_C_OVER_CURRENT);
            usb_hub_power_on(hub->pusb_dev, hub->desc.bPwrOn2PwrGood * 2);
        }

        if (portchange & USB_PORT_STAT_C_RESET)
        {
            changed |= USB_PORT_STAT_C_RESET;
            KDEBUG(("port %ld reset change\n", i + 1));
            usb_clear_port_feature(dev, i + 1,
                        USB_PORT_FEAT_C_RESET);
        }
    } /* end for i all ports */

    /* now do hub status */
    if (usb_get_hub_status(dev, &hubsts) < 0) {
        KDEBUG(("usb_hub_events: failed to get Status %lx\n",
                dev->status));
    } else {
        unsigned short hubstatus, hubchange;

        hubstatus = le2cpu16(hubsts.wHubStatus);
        hubchange = le2cpu16(hubsts.wHubChange);

        if (hubchange & HUB_CHANGE_LOCAL_POWER) {
            usb_clear_hub_feature(dev, C_HUB_LOCAL_POWER);
        }
        if (hubchange & HUB_CHANGE_OVERCURRENT) {
            usb_clear_hub_feature(dev, C_HUB_OVER_CURRENT);
            mdelay(500);
            usb_hub_power_on(hub->pusb_dev, hub->desc.bPwrOn2PwrGood * 2);
            usb_get_hub_status(dev, &hubsts);

            hubstatus = le2cpu16(hubsts.wHubStatus);
            hubchange = le2cpu16(hubsts.wHubChange);

            if (hubstatus & HUB_STATUS_OVERCURRENT) {
                ALERT(("usb_hub_events: Overcurrent on hub!"));
            }
        }
    }

    return changed;
}

long
usb_hub_probe(struct usb_device *dev, long ifnum)
{
    struct usb_interface *iface;
    struct usb_endpoint_descriptor *ep;

    iface = &dev->config.if_desc[ifnum];
    /* Is it a hub? */
    if (iface->desc.bInterfaceClass != USB_CLASS_HUB)
        return 0;
    /* Some hubs have a subclass of 1, which AFAICT according to the */
    /*  specs is not defined, but it works */
    if ((iface->desc.bInterfaceSubClass != 0) &&
        (iface->desc.bInterfaceSubClass != 1))
        return 0;
    /* Multiple endpoints? What kind of mutant ninja-hub is this? */
    if (iface->desc.bNumEndpoints != 1)
        return 0;
    ep = &iface->ep_desc[0];
    /* Output endpoint? Curiousier and curiousier.. */
    if (!(ep->bEndpointAddress & USB_DIR_IN))
        return 0;
    /* If it's not an interrupt endpoint, we'd better punt! */
    if ((ep->bmAttributes & 3) != 3)
        return 0;
    /* We found a hub */
    KDEBUG(("USB hub found\n"));

    dev->privptr = usb_hub_configure(dev);


    return 1;
}

void
usb_rh_wakeup(void)
{
    /* nothing */
}

void
usb_hub_init(struct usb_device *dev)
{
    long i,j,k = 0;

    cprintf("Scanning USB devices.... Please wait...\n");

again:
    for (i = 0; i < USB_MAX_HUB; i++)
    {
        struct usb_hub_device *hub = usb_get_hub_index (i);

        if (hub)
        {
            if (usb_hub_events (hub) == 1)
            {
                for (j = k; j < USB_MAX_DEVICE; j++)
                {
                    struct usb_device *pdev = usb_get_dev_index (j);
                    if (pdev && pdev->mf && pdev->prod)
                    {
                        cprintf("Found: %s %s\n", pdev->mf, pdev->prod);
                        k = j+1;
                    }
                }
                goto again;
            }
        }
    }
}
