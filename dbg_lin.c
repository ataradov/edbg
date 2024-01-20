// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2013-2024, Alex Taradov <alex@taradov.com>. All rights reserved.

/*- Includes ----------------------------------------------------------------*/
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/usb/ch9.h>
#include <linux/usbdevice_fs.h>
#include <libudev.h>
#include "edbg.h"
#include "dbg.h"

/*- Definitions -------------------------------------------------------------*/
#define CONTROL_TIMEOUT    150 // ms
#define LANGID_US_ENGLISH  0x0409

/*- Variables ---------------------------------------------------------------*/
static debugger_t *g_debugger;
static int g_debugger_fd = -1;

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
static char *get_string(int fd, int index)
{
  struct usbdevfs_ctrltransfer ctrl;
  uint16_t buf[128] = { 0 };

  ctrl.bRequestType = USB_RECIP_DEVICE | USB_TYPE_STANDARD | USB_DIR_IN;
  ctrl.bRequest     = USB_REQ_GET_DESCRIPTOR;
  ctrl.wValue       = (USB_DT_STRING << 8) | index;
  ctrl.wIndex       = LANGID_US_ENGLISH;
  ctrl.wLength      = sizeof(buf);
  ctrl.data         = buf;
  ctrl.timeout      = CONTROL_TIMEOUT;

  int res = ioctl(fd, USBDEVFS_CONTROL, &ctrl);

  if (res < 0)
    return "";

  check(res >= 2 && 0 == (res % 2), "invalid string descriptor response");

  int len = res/2 - 1;
  char *str = "<unknown>";

  if (len > 0)
  {
    str = buf_alloc(len + 1);

    for (int i = 0; i < len; i++)
      str[i] = isprint(buf[i+1]) ? buf[i+1] : '?';
  }

  return str;
}

//-----------------------------------------------------------------------------
static void *find_descriptor(uint8_t **desc, int *size, int desc_type, int desc_size)
{
  while (*size >= desc_size)
  {
    struct usb_descriptor_header *hdr = (struct usb_descriptor_header *)*desc;

    *desc += hdr->bLength;
    *size -= hdr->bLength;

    if (hdr->bDescriptorType == desc_type)
      return hdr;
  }

  return NULL;
}

//-----------------------------------------------------------------------------
static bool is_dap_str(char *str)
{
  return (NULL != strstr(str, "CMSIS-DAP"));
}

//-----------------------------------------------------------------------------
static void parse_descriptors(debugger_t *debugger, int fd, uint8_t *desc, int size)
{
  memset(debugger, 0, sizeof(debugger_t));

  struct usb_device_descriptor *device = find_descriptor(&desc, &size, USB_DT_DEVICE, USB_DT_DEVICE_SIZE);
  if (!device)
    return;

  debugger->serial       = get_string(fd, device->iSerialNumber);
  debugger->manufacturer = get_string(fd, device->iManufacturer);
  debugger->product      = get_string(fd, device->iProduct);
  debugger->vid          = device->idVendor;
  debugger->pid          = device->idProduct;

  while (1)
  {
    struct usb_interface_descriptor *interface = find_descriptor(&desc, &size, USB_DT_INTERFACE, USB_DT_INTERFACE_SIZE);

    if (!interface)
      break;

    if (interface->bNumEndpoints != 2 || interface->bInterfaceSubClass != 0 || interface->bInterfaceProtocol != 0)
      continue;

    if (USB_CLASS_HID != interface->bInterfaceClass && USB_CLASS_VENDOR_SPEC != interface->bInterfaceClass)
      continue;

    if (!is_dap_str(debugger->product) && !is_dap_str(get_string(fd, interface->iInterface)))
      continue;

    struct usb_endpoint_descriptor *ep0 = find_descriptor(&desc, &size, USB_DT_ENDPOINT, USB_DT_ENDPOINT_SIZE);
    struct usb_endpoint_descriptor *ep1 = find_descriptor(&desc, &size, USB_DT_ENDPOINT, USB_DT_ENDPOINT_SIZE);

    if (!ep0 || !ep1)
      break;

    if (ep0->wMaxPacketSize != ep1->wMaxPacketSize)
      continue;

    if (64 != ep0->wMaxPacketSize && 512 != ep0->wMaxPacketSize && 1024 != ep0->wMaxPacketSize)
      continue;

    if ((ep0->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == (ep1->bEndpointAddress & USB_ENDPOINT_DIR_MASK))
      continue;

    if (USB_CLASS_HID == interface->bInterfaceClass)
    {
      if (!usb_endpoint_xfer_int(ep0) || !usb_endpoint_xfer_int(ep1))
        continue;

      if (usb_endpoint_dir_in(ep0))
      {
        debugger->v1_tx_ep = ep1->bEndpointAddress;
        debugger->v1_rx_ep = ep0->bEndpointAddress;
      }
      else
      {
        debugger->v1_tx_ep = ep0->bEndpointAddress;
        debugger->v1_rx_ep = ep1->bEndpointAddress;
      }

      debugger->v1_ep_size   = ep0->wMaxPacketSize;
      debugger->v1_interface = interface->bInterfaceNumber;

      debugger->versions |= DBG_CMSIS_DAP_V1;
    }

    if (USB_CLASS_VENDOR_SPEC == interface->bInterfaceClass)
    {
      if (!usb_endpoint_xfer_bulk(ep0) || !usb_endpoint_xfer_bulk(ep1))
        continue;

      if (usb_endpoint_dir_in(ep0))
        continue;

      debugger->v2_tx_ep     = ep0->bEndpointAddress;
      debugger->v2_rx_ep     = ep1->bEndpointAddress;
      debugger->v2_ep_size   = ep0->wMaxPacketSize;
      debugger->v2_interface = interface->bInterfaceNumber;

      debugger->versions |= DBG_CMSIS_DAP_V2;
    }
  }
}

//-----------------------------------------------------------------------------
int dbg_enumerate(debugger_t *debuggers, int size)
{
  struct udev *udev;
  struct udev_enumerate *enumerate;
  struct udev_list_entry *devices, *dev_list_entry;
  int rsize = 0;

  udev = udev_new();
  check(udev, "unable to create udev object");

  enumerate = udev_enumerate_new(udev);
  udev_enumerate_add_match_subsystem(enumerate, "usb");
  udev_enumerate_add_match_property(enumerate, "DEVTYPE", "usb_device");

  udev_enumerate_scan_devices(enumerate);
  devices = udev_enumerate_get_list_entry(enumerate);

  udev_list_entry_foreach(dev_list_entry, devices)
  {
    const char *syspath = udev_list_entry_get_name(dev_list_entry);
    struct udev_device *dev = udev_device_new_from_syspath(udev, syspath);
    const char *path = udev_device_get_devnode(dev);
    uint8_t descriptors[4096];

    int fd = open(path, O_RDWR);

    if (fd < 0)
    {
      udev_device_unref(dev);
      continue;
    }

    int res = read(fd, descriptors, sizeof(descriptors));
    check(res > 0, "descriptors read from %s: %d", path, res);

    parse_descriptors(&debuggers[rsize], fd, descriptors, res);

    if (debuggers[rsize].versions)
    {
      debuggers[rsize].path = strdup(path);
      rsize++;
    }

    close(fd);
    udev_device_unref(dev);

    if (rsize == size)
      break;
  }

  udev_enumerate_unref(enumerate);
  udev_unref(udev);

  return rsize;
}

//-----------------------------------------------------------------------------
void dbg_open(debugger_t *debugger, int version)
{
  g_debugger = debugger;

  g_debugger_fd = open(g_debugger->path, O_RDWR);

  if (g_debugger_fd < 0)
    error_exit("unable to open device %s: %s",  g_debugger->path, strerror(errno));

  g_debugger->use_v2 = (DBG_CMSIS_DAP_V2 == version);

  int interface = g_debugger->use_v2 ? g_debugger->v2_interface : g_debugger->v1_interface;

  { // Disconnect kernel driver
    struct usbdevfs_disconnect_claim dc;

    dc.interface = interface;
    dc.flags = USBDEVFS_DISCONNECT_CLAIM_EXCEPT_DRIVER;
    strcpy(dc.driver, "usbfs");

    int res = ioctl(g_debugger_fd, USBDEVFS_DISCONNECT_CLAIM, &dc);
    check(res >= 0, "ioctl(DISCONNECT_CLAIM): %d", res);
  }

  { // Claim interface
    int res = ioctl(g_debugger_fd, USBDEVFS_CLAIMINTERFACE, &interface);
    check(res >= 0, "ioctl(CLAIMINTERFACE): %d", res);
  }
}

//-----------------------------------------------------------------------------
void dbg_close(void)
{
  int interface = g_debugger->use_v2 ? g_debugger->v2_interface : g_debugger->v1_interface;

  { // Release interface
    int res = ioctl(g_debugger_fd, USBDEVFS_RELEASEINTERFACE, &interface);
    check(res >= 0, "ioctl(RELEASEINTERFACE): %d", res);
  }

  close(g_debugger_fd);
}

//-----------------------------------------------------------------------------
int dbg_get_packet_size(void)
{
  return g_debugger->use_v2 ? g_debugger->v2_ep_size : g_debugger->v1_ep_size;
}

//-----------------------------------------------------------------------------
int dbg_dap_cmd(uint8_t *data, int resp_size, int req_size)
{
  uint8_t cmd = data[0];
  struct usbdevfs_urb urb = { 0 };
  struct usbdevfs_urb *purb = NULL;
  uint8_t buf[DBG_MAX_EP_SIZE];
  int res;

  memset(buf, 0xff, sizeof(buf));
  memcpy(buf, data, req_size);

  // TX
  urb.endpoint      = g_debugger->use_v2 ? g_debugger->v2_tx_ep : g_debugger->v1_tx_ep;
  urb.type          = g_debugger->use_v2 ? USBDEVFS_URB_TYPE_BULK : USBDEVFS_URB_TYPE_INTERRUPT;
  urb.buffer        = buf;
  urb.buffer_length = g_debugger->use_v2 ? req_size : g_debugger->v1_ep_size;

  res = ioctl(g_debugger_fd, USBDEVFS_SUBMITURB, &urb);
  check(res >= 0, "ioctl(SUBMITURB) for TX: %d", res);

  res = ioctl(g_debugger_fd, USBDEVFS_REAPURB, &purb);
  check(res >= 0, "ioctl(REAPURB) for TX: %d", res);
  check(urb.actual_length == urb.buffer_length, "incomplete buffer TX: request = %d, actual = %d",
      urb.buffer_length, urb.actual_length);

  // RX
  urb.endpoint      = g_debugger->use_v2 ? g_debugger->v2_rx_ep : g_debugger->v1_rx_ep;
  urb.type          = g_debugger->use_v2 ? USBDEVFS_URB_TYPE_BULK : USBDEVFS_URB_TYPE_INTERRUPT;
  urb.buffer        = buf;
  urb.buffer_length = g_debugger->use_v2 ? g_debugger->v2_ep_size : g_debugger->v1_ep_size;

  res = ioctl(g_debugger_fd, USBDEVFS_SUBMITURB, &urb);
  check(res >= 0, "ioctl(SUBMITURB) for RX: %d", res);

  res = ioctl(g_debugger_fd, USBDEVFS_REAPURB, &purb);
  check(res >= 0, "ioctl(REAPURB) for RX: %d", res);

  // Result
  check(urb.actual_length, "empty response received");

  if (buf[0] != cmd)
    error_exit("invalid response received: request = 0x%02x, response = 0x%02x", cmd, buf[0]);

  memcpy(data, &buf[1], (resp_size < (urb.actual_length - 1)) ? resp_size : (urb.actual_length - 1));

  return urb.actual_length - 1;
}

