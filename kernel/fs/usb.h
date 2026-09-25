#ifndef CHRIS_USB_H
#define CHRIS_USB_H

/*
 * USB core is not a generic stack yet.
 * The only backend is UHCI, and the only class driver is mass storage:
 *
 *   USB core (this note + shared buffers in usb_msc.c)
 *     └── UHCI
 *           └── USB MSC (BOT)
 *
 * EHCI can be added as another host controller beside UHCI.
 * xHCI is a separate poll-only HID path in xhci.c (boot keyboard and
 * mouse). It does not speak MSC and the UHCI driver does not speak xHCI.
 */

enum {
    USB_HOST_NONE = 0,
    USB_HOST_UHCI = 1,
    USB_HOST_EHCI = 2,
    USB_HOST_XHCI = 3
};

#endif
