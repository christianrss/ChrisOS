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
 * TODO xHCI: map the capability/operational/runtime registers, allocate a
 * device context and one transfer ring, enumerate the port, then hand the
 * bulk endpoints to the existing MSC BOT code. Do not pretend the UHCI
 * driver speaks xHCI.
 */

enum {
    USB_HOST_NONE = 0,
    USB_HOST_UHCI = 1,
    USB_HOST_EHCI = 2,
    USB_HOST_XHCI = 3
};

#endif
