// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022, Alex Taradov <alex@taradov.com>. All rights reserved.

/*- Includes ----------------------------------------------------------------*/
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <wchar.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDLib.h>
#include <IOKit/hid/IOHIDDevice.h>
#include "edbg.h"
#include "dbg.h"

/*- Definitions -------------------------------------------------------------*/
#define MAX_DEVICES    256 // Not possible with USB, just a big safe number

/*- Variables ---------------------------------------------------------------*/
static IOHIDDeviceRef debugger_handle = NULL;
static uint8_t tx_buffer[DBG_MAX_EP_SIZE];
static uint8_t rx_buffer[DBG_MAX_EP_SIZE];
static int rx_size = -1;
static int report_size;

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
static int32_t get_int_property(IOHIDDeviceRef device, CFStringRef prop)
{
  CFTypeRef ref;
  int32_t value;

  ref = IOHIDDeviceGetProperty(device, prop);

  if (!ref)
    return 0;

  if (CFGetTypeID(ref) != CFNumberGetTypeID())
     error_exit("failed to get integer property value");

  CFNumberGetValue((CFNumberRef) ref, kCFNumberSInt32Type, &value);

  return value;
}

//-----------------------------------------------------------------------------
static char * get_string_property(IOHIDDeviceRef device, CFStringRef prop)
{
  CFStringRef str;
  CFIndex len, max_size;
  char *res;

  str = (CFStringRef)IOHIDDeviceGetProperty(device, prop);

  if (!str)
    return "<unknown>";

  len = CFStringGetLength(str);
  max_size = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;

  res = (char *)malloc(max_size);

  if (!res)
    error_exit("out of memory");

  if (!CFStringGetCString(str, res, max_size, kCFStringEncodingUTF8))
    error_exit("failed to get string property value");

  return res;
}

//-----------------------------------------------------------------------------
int dbg_enumerate(debugger_t *debuggers, int size)
{
  IOHIDManagerRef hid_manager;
  CFSetRef device_set;
  IOHIDDeviceRef device_list[MAX_DEVICES];
  int device_count;
  int rsize = 0;

  hid_manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);

  if (NULL == hid_manager || CFGetTypeID(hid_manager) != IOHIDManagerGetTypeID())
    error_exit("unable to access HID manager");

  IOHIDManagerSetDeviceMatching(hid_manager, NULL);

  device_set = IOHIDManagerCopyDevices(hid_manager);

  if (NULL == device_set)
    return 0;

  device_count = CFSetGetCount(device_set);

  if (device_count > MAX_DEVICES)
    error_exit("too many devices");

  CFSetGetValues(device_set, (const void **)&device_list);

  for (int i = 0; i < device_count; i++)
  {
    IOHIDDeviceRef dev = device_list[i];

    if (!dev)
      continue;

    io_object_t iokit_dev = IOHIDDeviceGetService(dev);
    kern_return_t res;

    if (iokit_dev == MACH_PORT_NULL)
      error_exit("iokit_dev == MACH_PORT_NULL");

    res = IORegistryEntryGetRegistryEntryID(iokit_dev, &debuggers[rsize].entry_id);

    if (res != KERN_SUCCESS)
      error_exit("failed to get entry_id");

    debuggers[rsize].path         = NULL;
    debuggers[rsize].serial       = get_string_property(dev, CFSTR(kIOHIDSerialNumberKey));
    debuggers[rsize].manufacturer = get_string_property(dev, CFSTR(kIOHIDManufacturerKey));
    debuggers[rsize].product      = get_string_property(dev, CFSTR(kIOHIDProductKey));
    debuggers[rsize].vid          = get_int_property(dev, CFSTR(kIOHIDVendorIDKey));
    debuggers[rsize].pid          = get_int_property(dev, CFSTR(kIOHIDProductIDKey));
    debuggers[rsize].versions     = DBG_CMSIS_DAP_V1;

    if (strstr(debuggers[rsize].product, "CMSIS-DAP"))
      rsize++;

    if (rsize == size)
      break;
  }

  CFRelease(device_set);

  return rsize;
}

//-----------------------------------------------------------------------------
static void rx_callback(void *user, IOReturn result, void *sender, IOHIDReportType type,
    uint32_t report_id, uint8_t *report, CFIndex report_length)
{
  rx_size = report_length;

  (void)user;
  (void)result;
  (void)sender;
  (void)type;
  (void)report_id;
  (void)report;
}

//-----------------------------------------------------------------------------
void dbg_open(debugger_t *debugger, int version)
{
  io_registry_entry_t entry = MACH_PORT_NULL;
  IOReturn ret = kIOReturnInvalid;

  entry = IOServiceGetMatchingService((mach_port_t)0, IORegistryEntryIDMatching(debugger->entry_id));

  if (MACH_PORT_NULL == entry)
    error_exit("IOServiceGetMatchingService() failed");

  debugger_handle = IOHIDDeviceCreate(kCFAllocatorDefault, entry);

  if (NULL == debugger_handle)
    error_exit("IOHIDDeviceCreate() failed");

  ret = IOHIDDeviceOpen(debugger_handle, kIOHIDOptionsTypeNone);

  if (kIOReturnSuccess != ret)
    error_exit("IOHIDDeviceOpen() failed");

  report_size = get_int_property(debugger_handle, CFSTR(kIOHIDMaxInputReportSizeKey));

  IOHIDDeviceRegisterInputReportCallback(debugger_handle, rx_buffer, report_size, &rx_callback, NULL);

  IOHIDDeviceScheduleWithRunLoop(debugger_handle, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

  IOObjectRelease(entry);

  (void)version;
}

//-----------------------------------------------------------------------------
void dbg_close(void)
{
  if (debugger_handle)
  {
    IOHIDDeviceRegisterRemovalCallback(debugger_handle, NULL, NULL);
    IOHIDDeviceClose(debugger_handle, kIOHIDOptionsTypeNone);
    debugger_handle = NULL;
  }
}

//-----------------------------------------------------------------------------
int dbg_get_packet_size(void)
{
  return report_size;
}

//-----------------------------------------------------------------------------
int dbg_dap_cmd(uint8_t *data, int resp_size, int req_size)
{
  uint8_t cmd = data[0];
  IOReturn ret;

  if (NULL == debugger_handle)
    return 0;

  rx_size = -1;

  memset(tx_buffer, 0xff, report_size);
  memcpy(tx_buffer, data, req_size);

  ret = IOHIDDeviceSetReport(debugger_handle, kIOHIDReportTypeOutput, 0, tx_buffer, report_size);

  if (ret != kIOReturnSuccess)
    error_exit("HID write failed");

  while (1)
  {
    int res = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.001, true);

    if (kCFRunLoopRunFinished == res)
      error_exit("debugger disconnected");

    if (kCFRunLoopRunTimedOut != res && kCFRunLoopRunHandledSource != res)
      error_exit("debugger rx error");

    if (rx_size >= 0)
      break;
  }

  check(rx_size, "empty response received");

  if (rx_buffer[0] != cmd)
    error_exit("invalid response received: request = 0x%02x, response = 0x%02x", cmd, rx_buffer[0]);

  rx_size--;

  memcpy(data, &rx_buffer[1], (resp_size < rx_size) ? resp_size : rx_size);

  return rx_size;
}

