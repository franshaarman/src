/*	$OpenBSD: uow.c,v 1.7 2006/09/29 19:41:22 grange Exp $	*/

/*
 * Copyright (c) 2006 Alexander Yurchenko <grange@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Maxim/Dallas DS2490 USB 1-Wire adapter driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/proc.h>

#include <dev/onewire/onewirevar.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>

#include <dev/usb/uowreg.h>

#ifdef UOW_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

#define UOW_TIMEOUT	1000	/* ms */

struct uow_softc {
	USBBASEDEVICE		sc_dev;

	struct onewire_bus	sc_ow_bus;
	struct device *		sc_ow_dev;

	usbd_device_handle	sc_udev;
	usbd_interface_handle	sc_iface;
	usbd_pipe_handle	sc_ph_ibulk;
	usbd_pipe_handle	sc_ph_obulk;
	usbd_pipe_handle	sc_ph_intr;
	u_int8_t		sc_regs[DS2490_NREGS];
	usbd_xfer_handle	sc_xfer;
	u_int8_t		sc_fifo[DS2490_DATAFIFOSIZE];
};

USB_DECLARE_DRIVER(uow);

/* List of supported devices */
static const struct usb_devno uow_devs[] = {
	{ USB_VENDOR_DALLAS,		USB_PRODUCT_DALLAS_USB_FOB_IBUTTON }
};

Static int	uow_ow_reset(void *);
Static int	uow_ow_bit(void *, int);
Static int	uow_ow_read_byte(void *);
Static void	uow_ow_write_byte(void *, int);
Static void	uow_ow_read_block(void *, void *, int);
Static void	uow_ow_write_block(void *, const void *, int);

Static int	uow_cmd(struct uow_softc *, int, int);
Static void	uow_intr(usbd_xfer_handle, usbd_private_handle, usbd_status);
Static int	uow_read(struct uow_softc *, void *, int);
Static int	uow_write(struct uow_softc *, const void *, int);

USB_MATCH(uow)
{
	USB_MATCH_START(uow, uaa);

	if (uaa->iface != NULL)
		return (UMATCH_NONE);

	return ((usb_lookup(uow_devs, uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

USB_ATTACH(uow)
{
	USB_ATTACH_START(uow, sc, uaa);
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	char *devinfop;
	int ep_ibulk = -1, ep_obulk = -1, ep_intr = -1;
	struct onewirebus_attach_args oba;
	usbd_status error;
	int i;

	sc->sc_udev = uaa->device;

	/* Display device info string */
	USB_ATTACH_SETUP;
	if ((devinfop = usbd_devinfo_alloc(uaa->device, 0)) != NULL) {
		printf("%s: %s\n", USBDEVNAME(sc->sc_dev), devinfop);
		usbd_devinfo_free(devinfop);
	}

	/* Set USB configuration */
	if ((error = usbd_set_config_no(sc->sc_udev,
	    DS2490_USB_CONFIG, 0)) != 0) {
		printf("%s: failed to set config %d: %s\n",
		    USBDEVNAME(sc->sc_dev), DS2490_USB_CONFIG,
		    usbd_errstr(error));
		USB_ATTACH_ERROR_RETURN;
	}

	/* Get interface handle */
	if ((error = usbd_device2interface_handle(sc->sc_udev,
	    DS2490_USB_IFACE, &sc->sc_iface)) != 0) {
		printf("%s: failed to get iface %d: %s\n",
		    USBDEVNAME(sc->sc_dev), DS2490_USB_IFACE,
		    usbd_errstr(error));
		USB_ATTACH_ERROR_RETURN;
	}

	/* Find endpoints */
	id = usbd_get_interface_descriptor(sc->sc_iface);
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (ed == NULL) {
			printf("%s: failed to get endpoint %d descriptor\n",
			    USBDEVNAME(sc->sc_dev), i);
			USB_ATTACH_ERROR_RETURN;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			ep_ibulk = ed->bEndpointAddress;
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			ep_obulk = ed->bEndpointAddress;
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT)
			ep_intr = ed->bEndpointAddress;
	}
	if (ep_ibulk == -1 || ep_obulk == -1 || ep_intr == -1) {
		printf("%s: missing endpoint: ibulk %d, obulk %d, intr %d\n",
		   USBDEVNAME(sc->sc_dev), ep_ibulk, ep_obulk, ep_intr);
		USB_ATTACH_ERROR_RETURN;
	}

	/* Open pipes */
	if ((error = usbd_open_pipe(sc->sc_iface, ep_ibulk, USBD_EXCLUSIVE_USE,
	    &sc->sc_ph_ibulk)) != 0) {
		printf("%s: failed to open bulk-in pipe: %s\n",
		    USBDEVNAME(sc->sc_dev), usbd_errstr(error));
		USB_ATTACH_ERROR_RETURN;
	}
	if ((error = usbd_open_pipe(sc->sc_iface, ep_obulk, USBD_EXCLUSIVE_USE,
	    &sc->sc_ph_obulk)) != 0) {
		printf("%s: failed to open bulk-out pipe: %s\n",
		    USBDEVNAME(sc->sc_dev), usbd_errstr(error));
		goto fail;
	}
	if ((error = usbd_open_pipe_intr(sc->sc_iface, ep_intr,
	    USBD_SHORT_XFER_OK, &sc->sc_ph_intr, sc,
	    sc->sc_regs, sizeof(sc->sc_regs), uow_intr,
	    USBD_DEFAULT_INTERVAL)) != 0) {
		printf("%s: failed to open intr pipe: %s\n",
		    USBDEVNAME(sc->sc_dev), usbd_errstr(error));
		goto fail;
	}

	/* Allocate xfer for bulk transfers */
	if ((sc->sc_xfer = usbd_alloc_xfer(sc->sc_udev)) == NULL) {
		printf("%s: failed to alloc bulk xfer\n",
		    USBDEVNAME(sc->sc_dev));
		goto fail;
	}

	memset(sc->sc_fifo, 0xff, sizeof(sc->sc_fifo));

	/* Attach 1-Wire bus */
	sc->sc_ow_bus.bus_cookie = sc;
	sc->sc_ow_bus.bus_reset = uow_ow_reset;
	sc->sc_ow_bus.bus_bit = uow_ow_bit;
	sc->sc_ow_bus.bus_read_byte = uow_ow_read_byte;
	sc->sc_ow_bus.bus_write_byte = uow_ow_write_byte;
	sc->sc_ow_bus.bus_read_block = uow_ow_read_block;
	sc->sc_ow_bus.bus_write_block = uow_ow_write_block;

	bzero(&oba, sizeof(oba));
	oba.oba_bus = &sc->sc_ow_bus;
	sc->sc_ow_dev = config_found(self, &oba, onewirebus_print);

	USB_ATTACH_SUCCESS_RETURN;

fail:
	if (sc->sc_ph_ibulk != NULL)
		usbd_close_pipe(sc->sc_ph_ibulk);
	if (sc->sc_ph_obulk != NULL)
		usbd_close_pipe(sc->sc_ph_obulk);
	if (sc->sc_ph_intr != NULL)
		usbd_close_pipe(sc->sc_ph_intr);
	if (sc->sc_xfer != NULL)
		usbd_free_xfer(sc->sc_xfer);
	USB_ATTACH_ERROR_RETURN;
}

USB_DETACH(uow)
{
	USB_DETACH_START(uow, sc);
	int rv = 0, s;

	s = splusb();

	if (sc->sc_ph_ibulk != NULL) {
		usbd_abort_pipe(sc->sc_ph_ibulk);
		usbd_close_pipe(sc->sc_ph_ibulk);
	}
	if (sc->sc_ph_obulk != NULL) {
		usbd_abort_pipe(sc->sc_ph_obulk);
		usbd_close_pipe(sc->sc_ph_obulk);
	}
	if (sc->sc_ph_intr != NULL) {
		usbd_abort_pipe(sc->sc_ph_intr);
		usbd_close_pipe(sc->sc_ph_intr);
	}

	if (sc->sc_xfer != NULL)
		usbd_free_xfer(sc->sc_xfer);

	if (sc->sc_ow_dev != NULL)
		rv = config_detach(sc->sc_ow_dev, flags);

	splx(s);

	usbd_add_drv_event(USB_EVENT_DRIVER_DETACH, sc->sc_udev,
	    USBDEV(sc->sc_dev));

	return (rv);
}

int
uow_activate(device_ptr_t self, enum devact act)
{
	struct uow_softc *sc = (struct uow_softc *)self;
	int rv = 0;

	switch (act) {
	case DVACT_ACTIVATE:
		break;
	case DVACT_DEACTIVATE:
		if (sc->sc_ow_dev != NULL)
			rv = config_deactivate(sc->sc_ow_dev);
		break;
	}

	return (rv);
}

Static int
uow_ow_reset(void *arg)
{
	struct uow_softc *sc = arg;

	if (uow_cmd(sc, DS2490_COMM_1WIRE_RESET | DS2490_BIT_IM, 0) != 0)
		return (1);

	/* XXX: check presence pulse */
	return (0);
}

Static int
uow_ow_bit(void *arg, int value)
{
	struct uow_softc *sc = arg;
	u_int8_t data;

	if (uow_cmd(sc, DS2490_COMM_BIT_IO | DS2490_BIT_IM |
	    (value ? DS2490_BIT_D : 0), 0) != 0)
		return (1);
	if (uow_read(sc, &data, sizeof(data)) != 0)
		return (1);

	return (data);
}

Static int
uow_ow_read_byte(void *arg)
{
	struct uow_softc *sc = arg;
	u_int8_t data;

	if (uow_cmd(sc, DS2490_COMM_BYTE_IO | DS2490_BIT_IM, 0xff) != 0)
		return (-1);
	if (uow_read(sc, &data, sizeof(data)) != 0)
		return (-1);

	return (data);
}

Static void
uow_ow_write_byte(void *arg, int value)
{
	struct uow_softc *sc = arg;
	u_int8_t data;

	if (uow_cmd(sc, DS2490_COMM_BYTE_IO | DS2490_BIT_IM, value) != 0)
		return;
	uow_read(sc, &data, sizeof(data));
}

Static void
uow_ow_read_block(void *arg, void *buf, int len)
{
	struct uow_softc *sc = arg;

	if (uow_write(sc, sc->sc_fifo, len) != 0)
		return;
	if (uow_cmd(sc, DS2490_COMM_BLOCK_IO | DS2490_BIT_IM, len) != 0)
		return;
	uow_read(sc, buf, len);
}

Static void
uow_ow_write_block(void *arg, const void *buf, int len)
{
	struct uow_softc *sc = arg;

	if (uow_write(sc, buf, len) != 0)
		return;
	if (uow_cmd(sc, DS2490_COMM_BLOCK_IO | DS2490_BIT_IM, len) != 0)
		return;
}

Static int
uow_cmd(struct uow_softc *sc, int cmd, int param)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = DS2490_COMM_CMD;
	USETW(req.wValue, cmd);
	USETW(req.wIndex, param);
	USETW(req.wLength, 0);
	if ((error = usbd_do_request(sc->sc_udev, &req, NULL)) != 0) {
		printf("%s: failed to do request: %s\n",
		    USBDEVNAME(sc->sc_dev), usbd_errstr(error));
		return (1);
	}

	bzero(sc->sc_regs, sizeof(sc->sc_regs));
	if (tsleep(sc->sc_regs, PRIBIO, "uowcmd",
	    (UOW_TIMEOUT * hz) / 1000) != 0) {
		printf("%s: request timeout\n", USBDEVNAME(sc->sc_dev));
		return (1);
	}

	return (0);
}

Static void
uow_intr(usbd_xfer_handle xfer, usbd_private_handle priv, usbd_status status)
{
	struct uow_softc *sc = priv;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_ph_intr);
		return;
	}

	wakeup(sc->sc_regs);
}

Static int
uow_read(struct uow_softc *sc, void *buf, int len)
{
	usbd_status error;

	/* XXX: implement FIFO status monitoring */
	if (len > DS2490_DATAFIFOSIZE) {
		printf("%s: read %d bytes, xfer too big\n",
		    USBDEVNAME(sc->sc_dev), len);
		return (1);
	}

	usbd_setup_xfer(sc->sc_xfer, sc->sc_ph_ibulk, sc, buf, len, 0,
	    UOW_TIMEOUT, NULL);
	if ((error = usbd_sync_transfer(sc->sc_xfer)) != 0) {
		printf("%s: failed to read: %s\n",
		    USBDEVNAME(sc->sc_dev), usbd_errstr(error));
		return (1);
	}

	return (0);
}

Static int
uow_write(struct uow_softc *sc, const void *buf, int len)
{
	usbd_status error;

	/* XXX: implement FIFO status monitoring */
	if (len > DS2490_DATAFIFOSIZE) {
		printf("%s: write %d bytes, xfer too big\n",
		    USBDEVNAME(sc->sc_dev), len);
		return (1);
	}

	usbd_setup_xfer(sc->sc_xfer, sc->sc_ph_obulk, sc, (void *)buf, len, 0,
	    UOW_TIMEOUT, NULL);
	if ((error = usbd_sync_transfer(sc->sc_xfer)) != 0) {
		printf("%s: failed to write: %s\n",
		    USBDEVNAME(sc->sc_dev), usbd_errstr(error));
		return (1);
	}

	return (0);
}
