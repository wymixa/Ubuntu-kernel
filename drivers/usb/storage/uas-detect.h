#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include "usb.h"

static int uas_is_interface(struct usb_host_interface *intf)
{
	return (intf->desc.bInterfaceClass == USB_CLASS_MASS_STORAGE &&
		intf->desc.bInterfaceSubClass == USB_SC_SCSI &&
		intf->desc.bInterfaceProtocol == USB_PR_UAS);
}

static int uas_find_uas_alt_setting(struct usb_interface *intf)
{
	int i;

	for (i = 0; i < intf->num_altsetting; i++) {
		struct usb_host_interface *alt = &intf->altsetting[i];

		if (uas_is_interface(alt))
			return alt->desc.bAlternateSetting;
	}

	return -ENODEV;
}

static int uas_find_endpoints(struct usb_host_interface *alt,
			      struct usb_host_endpoint *eps[])
{
	struct usb_host_endpoint *endpoint = alt->endpoint;
	unsigned i, n_endpoints = alt->desc.bNumEndpoints;

	for (i = 0; i < n_endpoints; i++) {
		unsigned char *extra = endpoint[i].extra;
		int len = endpoint[i].extralen;
		while (len >= 3) {
			if (extra[1] == USB_DT_PIPE_USAGE) {
				unsigned pipe_id = extra[2];
				if (pipe_id > 0 && pipe_id < 5)
					eps[pipe_id - 1] = &endpoint[i];
				break;
			}
			len -= extra[0];
			extra += extra[0];
		}
	}

	if (!eps[0] || !eps[1] || !eps[2] || !eps[3])
		return -ENODEV;

	return 0;
}

static int uas_use_uas_driver(struct usb_interface *intf,
			      const struct usb_device_id *id)
{
	struct usb_host_endpoint *eps[4] = { };
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_hcd *hcd = bus_to_hcd(udev->bus);
	unsigned long flags = id->driver_info;
	int r, alt;

	usb_stor_adjust_quirks(udev, &flags);

	if (flags & US_FL_IGNORE_UAS)
		return 0;

	alt = uas_find_uas_alt_setting(intf);
	if (alt < 0)
		return 0;

	r = uas_find_endpoints(&intf->altsetting[alt], eps);
	if (r < 0)
		return 0;

	if (udev->bus->sg_tablesize == 0) {
		dev_warn(&udev->dev,
			"The driver for the USB controller %s does not support scatter-gather which is\n",
			hcd->driver->description);
		dev_warn(&udev->dev,
			"required by the UAS driver. Please try an other USB controller if you wish to use UAS.\n");
		return 0;
	}

	if (udev->speed >= USB_SPEED_SUPER && !hcd->can_do_streams) {
		dev_warn(&udev->dev,
			"USB controller %s does not support streams, which are required by the UAS driver.\n",
			hcd_to_bus(hcd)->bus_name);
		dev_warn(&udev->dev,
			"Please try an other USB controller if you wish to use UAS.\n");
		return 0;
	}

	return 1;
}
