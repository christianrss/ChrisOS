#ifndef CHRIS_USB_MSC_H
#define CHRIS_USB_MSC_H

int usb_msc_probe(void);
int usb_tablet_ready(void);
void usb_tablet_poll(void);

#endif
