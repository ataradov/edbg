/*
 * Copyright (c) 2013-2015, Alex Taradov <alex@taradov.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*- Includes ----------------------------------------------------------------*/
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/hidraw.h>
#include <libudev.h>
#include "edbg.h"
#include "dbg.h"

/*- Definitions -------------------------------------------------------------*/
#define HID_BUFFER_SIZE   513 // Atmel EDBG expects 512 bytes + 1 byte for report ID

/*- Types -------------------------------------------------------------------*/

/*- Variables ---------------------------------------------------------------*/
static int debugger_fd = -1;
static uint8_t hid_buffer[HID_BUFFER_SIZE];

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
int dbg_enumerate(debugger_t *debuggers, int size)
{
  struct udev *udev;
  struct udev_enumerate *enumerate;
  struct udev_list_entry *devices, *dev_list_entry;
  struct udev_device *dev, *parent;
  int rsize = 0;

  udev = udev_new();
  check(udev, "unable to create udev object");

  enumerate = udev_enumerate_new(udev);
  udev_enumerate_add_match_subsystem(enumerate, "hidraw");
  udev_enumerate_scan_devices(enumerate);
  devices = udev_enumerate_get_list_entry(enumerate);

  udev_list_entry_foreach(dev_list_entry, devices)
  {
    const char *path;

    path = udev_list_entry_get_name(dev_list_entry);
    dev = udev_device_new_from_syspath(udev, path);

    parent = udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_device");
    check(parent, "unable to find parent usb device");

    if (DBG_VID == strtol(udev_device_get_sysattr_value(parent, "idVendor"), NULL, 16) &&
        DBG_PID == strtol(udev_device_get_sysattr_value(parent, "idProduct"), NULL, 16) &&
        rsize < size)
    {
      const char *serial = udev_device_get_sysattr_value(parent, "serial");
      const char *manufacturer = udev_device_get_sysattr_value(parent, "manufacturer");
      const char *product = udev_device_get_sysattr_value(parent, "product");

      debuggers[rsize].path = strdup(udev_device_get_devnode(dev));
      debuggers[rsize].serial = serial ? strdup(serial) : "<unknown>";
      debuggers[rsize].manufacturer = manufacturer ? strdup(manufacturer) : "<unknown>";
      debuggers[rsize].product = product ? strdup(product) : "<unknown>";

      rsize++;
    }

    udev_device_unref(parent);
  }

  udev_enumerate_unref(enumerate);
  udev_unref(udev);

  return rsize;
}

//-----------------------------------------------------------------------------
void dbg_open(debugger_t *debugger)
{
  debugger_fd = open(debugger->path, O_RDWR);

  if (debugger_fd < 0)
    perror_exit("unable to open device");
}

//-----------------------------------------------------------------------------
void dbg_close(void)
{
  if (debugger_fd)
    close(debugger_fd);
}

//-----------------------------------------------------------------------------
int dbg_dap_cmd(uint8_t *data, int size, int rsize)
{
  char cmd = data[0];
  int res;

  memset(hid_buffer, 0xff, HID_BUFFER_SIZE);

  hid_buffer[0] = 0x00; // Report ID
  memcpy(&hid_buffer[1], data, rsize);

  res = write(debugger_fd, hid_buffer, HID_BUFFER_SIZE/*rsize+1*/); // Atmel EDBG expects 512 bytes
  if (res < 0)
    perror_exit("debugger write()");

  res = read(debugger_fd, hid_buffer, sizeof(hid_buffer));
  if (res < 0)
    perror_exit("debugger read()");

  check(res, "empty response received");

  check(hid_buffer[0] == cmd, "invalid response received");

  res--;
  memcpy(data, &hid_buffer[1], (size < res) ? size : res);

  return res;
}

