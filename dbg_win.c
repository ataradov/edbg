// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2013-2024, Alex Taradov <alex@taradov.com>. All rights reserved.

/*- Includes ----------------------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <winusb.h>
#include <hidsdi.h>
#include <hidpi.h>
#include "dbg.h"
#include "edbg.h"

#undef interface

/*- Definitions -------------------------------------------------------------*/
DEFINE_GUID(GUID_CMSIS_DAP_V2, 0xcdb3b5adL, 0x293b, 0x4663, 0xaa, 0x36, 0x1a, 0xae, 0x46, 0x46, 0x37, 0x76);

#define MAX_STRING_SIZE    256
#define LANGID_US_ENGLISH  0x0409

/*- Variables ---------------------------------------------------------------*/
static debugger_t *g_debugger;
static HANDLE g_handle = INVALID_HANDLE_VALUE;
static WINUSB_INTERFACE_HANDLE g_winusb_handle = INVALID_HANDLE_VALUE;

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
static void get_device_descriptor(WINUSB_INTERFACE_HANDLE handle, USB_DEVICE_DESCRIPTOR *desc)
{
  WINUSB_SETUP_PACKET packet = {0};
  ULONG size = 0;

  packet.RequestType = 0x80; // IN, DEVICE, STANDARD
  packet.Request     = USB_REQUEST_GET_DESCRIPTOR;
  packet.Value       = USB_DESCRIPTOR_MAKE_TYPE_AND_INDEX(USB_DEVICE_DESCRIPTOR_TYPE, 0);
  packet.Index       = 0;
  packet.Length      = sizeof(USB_DEVICE_DESCRIPTOR);

  WINBOOL res = WinUsb_ControlTransfer(handle, packet, (uint8_t *)desc, packet.Length, &size, NULL);
  check(res, "[bulk] WinUsb_ControlTransfer() failed");
  check(size == sizeof(USB_DEVICE_DESCRIPTOR), "incomplete control transfer");
}

//-----------------------------------------------------------------------------
static char *get_string(WINUSB_INTERFACE_HANDLE handle, int index)
{
  uint16_t buf[128] = {0};
  ULONG size = 0;

  WINBOOL res = WinUsb_GetDescriptor(handle, USB_STRING_DESCRIPTOR_TYPE, index,
      LANGID_US_ENGLISH, (uint8_t *)buf, sizeof(buf), &size);
  check(res, "[bulk] WinUsb_GetDescriptor(STRING) failed");

  check(size >= 2 && 0 == (size % 2), "invalid string descriptor response");

  int len = size/2 - 1;
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
static void check_ep_size(char *name, int in_size, int out_size)
{
  if (in_size != out_size)
  {
    error_exit("[%s] input and output endpoint sizes do not match (%d and %d)",
        name, in_size, out_size);
  }

  if (64 != in_size && 512 != in_size && 1024 != in_size)
    error_exit("[%s] endpoint size %s is not 64, 512 or 1024", name, in_size);
}

//-----------------------------------------------------------------------------
static int dbg_enumerate_bulk(debugger_t *debuggers, int size, int rsize)
{
  HDEVINFO                         dev_info;
  SP_DEVICE_INTERFACE_DATA         dev_int_data;
  PSP_DEVICE_INTERFACE_DETAIL_DATA dev_int_detail;
  DWORD detail_size;
  WINBOOL res;

  dev_info = SetupDiGetClassDevs(&GUID_CMSIS_DAP_V2, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  check(INVALID_HANDLE_VALUE != dev_info, "[bulk] SetupDiGetClassDevs() failed");

  dev_int_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

  for (int index = 0; rsize < size; index++)
  {
    if (FALSE == SetupDiEnumDeviceInterfaces(dev_info, 0, &GUID_CMSIS_DAP_V2, index, &dev_int_data))
      break;

    SetupDiGetDeviceInterfaceDetail(dev_info, &dev_int_data, NULL, 0, &detail_size, NULL);

    dev_int_detail = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, detail_size);
    dev_int_detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

    res = SetupDiGetDeviceInterfaceDetail(dev_info, &dev_int_data, dev_int_detail,
        detail_size, NULL, NULL);
    check(res, "[bulk] SetupDiGetDeviceInterfaceDetail() failed");

    HANDLE device_handle = CreateFile(dev_int_detail->DevicePath,
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    check(INVALID_HANDLE_VALUE != device_handle, "[bulk] CreateFile() failed");

    WINUSB_INTERFACE_HANDLE handle;

    res = WinUsb_Initialize(device_handle, &handle);
    check(res, "[bulk] WinUsb_Initialize() failed");

    USB_DEVICE_DESCRIPTOR device;
    get_device_descriptor(handle, &device);

    struct
    {
      USB_CONFIGURATION_DESCRIPTOR configuration;
      USB_INTERFACE_DESCRIPTOR     interface;
      USB_ENDPOINT_DESCRIPTOR      out;
      USB_ENDPOINT_DESCRIPTOR      in;
    } config;

    ULONG size = 0;

    res = WinUsb_GetDescriptor(handle, USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0,
        (PUCHAR)&config, sizeof(config), &size);
    check(res, "[bulk] WinUsb_GetDescriptor(CONFIGURATION) failed");
    check(size == sizeof(config), "incomplete configuration descriptor");

    if (USB_CONFIGURATION_DESCRIPTOR_TYPE != config.configuration.bDescriptorType ||
        USB_INTERFACE_DESCRIPTOR_TYPE != config.interface.bDescriptorType ||
        USB_ENDPOINT_DESCRIPTOR_TYPE != config.out.bDescriptorType ||
        USB_ENDPOINT_TYPE_BULK != config.out.bmAttributes ||
        !USB_ENDPOINT_DIRECTION_OUT(config.out.bEndpointAddress) ||
        USB_ENDPOINT_DESCRIPTOR_TYPE != config.in.bDescriptorType ||
        USB_ENDPOINT_TYPE_BULK != config.in.bmAttributes ||
        !USB_ENDPOINT_DIRECTION_IN(config.in.bEndpointAddress))
      error_exit("invalid configuration descriptor");

    check_ep_size("bulk", config.in.wMaxPacketSize, config.out.wMaxPacketSize);

    char *serial       = get_string(handle, device.iSerialNumber);
    char *manufacturer = get_string(handle, device.iManufacturer);
    char *product      = get_string(handle, device.iProduct);
    int idx = -1;

    for (int i = 0; i < rsize; i++)
    {
      if (debuggers[i].vid == device.idVendor &&
          debuggers[i].pid == device.idProduct &&
          0 == strcmp(debuggers[i].serial, serial) &&
          0 == strcmp(debuggers[i].manufacturer, manufacturer))
      {
        idx = i;
        break;
      }
    }

    if (-1 == idx)
    {
      idx = rsize;
      rsize++;
    }

    debuggers[idx].v2_path      = strdup(dev_int_detail->DevicePath);
    debuggers[idx].serial       = serial;
    debuggers[idx].manufacturer = manufacturer;
    debuggers[idx].product      = product;
    debuggers[idx].vid          = device.idVendor;
    debuggers[idx].pid          = device.idProduct;
    debuggers[idx].v2_ep_size   = config.in.wMaxPacketSize;
    debuggers[idx].v2_tx_ep     = config.out.bEndpointAddress;
    debuggers[idx].v2_rx_ep     = config.in.bEndpointAddress;

    debuggers[idx].versions |= DBG_CMSIS_DAP_V2;

    WinUsb_Free(handle);
    CloseHandle(device_handle);
    HeapFree(GetProcessHeap(), 0, dev_int_detail);
  }

  SetupDiDestroyDeviceInfoList(dev_info);

  return rsize;
}

//-----------------------------------------------------------------------------
static int dbg_enumerate_hid(debugger_t *debuggers, int size)
{
  HDEVINFO                         dev_info;
  SP_DEVICE_INTERFACE_DATA         dev_int_data;
  PSP_DEVICE_INTERFACE_DETAIL_DATA dev_int_detail;
  HIDD_ATTRIBUTES hid_attr;
  HANDLE handle;
  GUID hid_guid;
  DWORD detail_size;
  int rsize = 0;
  WINBOOL res;

  HidD_GetHidGuid(&hid_guid);

  dev_info = SetupDiGetClassDevs(&hid_guid, NULL, NULL, DIGCF_PRESENT | DIGCF_INTERFACEDEVICE);
  check(INVALID_HANDLE_VALUE != dev_info, "[hid] SetupDiGetClassDevs() failed");

  dev_int_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

  for (int index = 0; rsize < size; index++)
  {
    if (FALSE == SetupDiEnumDeviceInterfaces(dev_info, 0, &hid_guid, index, &dev_int_data))
      break;

    SetupDiGetDeviceInterfaceDetail(dev_info, &dev_int_data, NULL, 0, &detail_size, NULL);

    dev_int_detail = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, detail_size);
    dev_int_detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

    res = SetupDiGetDeviceInterfaceDetail(dev_info, &dev_int_data, dev_int_detail,
        detail_size, NULL, NULL);
    check(res, "[hid] SetupDiGetDeviceInterfaceDetail() failed");

    handle = CreateFile(dev_int_detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    if (INVALID_HANDLE_VALUE == handle)
    {
      HeapFree(GetProcessHeap(), 0, dev_int_detail);
      continue;
    }

    wchar_t wstr[MAX_STRING_SIZE] = {0};
    char str[MAX_STRING_SIZE] = {0};

    HidD_GetProductString(handle, (PVOID)wstr, MAX_STRING_SIZE);
    wcstombs(str, wstr, MAX_STRING_SIZE);

    if (NULL == strstr(str, "CMSIS-DAP"))
    {
      CloseHandle(handle);
      HeapFree(GetProcessHeap(), 0, dev_int_detail);
      continue;
    }

    debuggers[rsize].v1_path = strdup(dev_int_detail->DevicePath);

    debuggers[rsize].product = strdup(str);

    HidD_GetManufacturerString(handle, (PVOID)wstr, MAX_STRING_SIZE);
    wcstombs(str, wstr, MAX_STRING_SIZE);
    debuggers[rsize].manufacturer = strdup(str);

    HidD_GetSerialNumberString(handle, (PVOID)wstr, MAX_STRING_SIZE);
    wcstombs(str, wstr, MAX_STRING_SIZE);
    debuggers[rsize].serial = strdup(str);

    hid_attr.Size = sizeof(HIDD_ATTRIBUTES);
    HidD_GetAttributes(handle, &hid_attr);

    debuggers[rsize].vid = hid_attr.VendorID;
    debuggers[rsize].pid = hid_attr.ProductID;

    PHIDP_PREPARSED_DATA prep;
    HidD_GetPreparsedData(handle, &prep);

    HIDP_CAPS caps;
    HidP_GetCaps(prep, &caps);

    check_ep_size("hid", caps.InputReportByteLength-1, caps.OutputReportByteLength-1);

    debuggers[rsize].v1_ep_size = caps.InputReportByteLength-1;

    debuggers[rsize].versions = DBG_CMSIS_DAP_V1;

    if (strstr(debuggers[rsize].product, "CMSIS-DAP"))
      rsize++;

    CloseHandle(handle);
    HeapFree(GetProcessHeap(), 0, dev_int_detail);
  }

  SetupDiDestroyDeviceInfoList(dev_info);

  return rsize;
}

//-----------------------------------------------------------------------------
int dbg_enumerate(debugger_t *debuggers, int size)
{
  int rsize = dbg_enumerate_hid(debuggers, size);
  return dbg_enumerate_bulk(debuggers, size, rsize);
}

//-----------------------------------------------------------------------------
void dbg_open(debugger_t *debugger, int version)
{
  g_debugger = debugger;

  g_debugger->use_v2 = (DBG_CMSIS_DAP_V2 == version);

  if (g_debugger->use_v2)
  {
    g_handle = CreateFile(g_debugger->v2_path, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, NULL);
    check(INVALID_HANDLE_VALUE != g_handle, "[bulk] CreateFile() failed");

    WINBOOL res = WinUsb_Initialize(g_handle, &g_winusb_handle);
    check(res, "[bulk] WinUsb_Initialize() failed");
  }
  else
  {
    g_handle = CreateFile(g_debugger->v1_path, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    check(INVALID_HANDLE_VALUE != g_handle, "[hid] CreateFile() failed");
  }
}

//-----------------------------------------------------------------------------
void dbg_close(void)
{
  if (g_debugger->use_v2)
    WinUsb_Free(g_winusb_handle);

  CloseHandle(g_handle);
}

//-----------------------------------------------------------------------------
int dbg_get_packet_size(void)
{
  return g_debugger->use_v2 ? g_debugger->v2_ep_size : g_debugger->v1_ep_size;
}

//-----------------------------------------------------------------------------
int dbg_dap_cmd(uint8_t *data, int resp_size, int req_size)
{
  uint8_t buf[DBG_MAX_EP_SIZE + 1];
  int cmd = data[0];
  int resp = 0;
  int size;
  WINBOOL res;

  memset(buf, 0xff, sizeof(buf));

  if (g_debugger->use_v2)
  {
    ULONG actual_size = 0;

    res = WinUsb_WritePipe(g_winusb_handle, g_debugger->v2_tx_ep, data, req_size, &actual_size, NULL);
    check(res && (int)actual_size == req_size, "WinUsb_WritePipe() failed");

    res = WinUsb_ReadPipe(g_winusb_handle, g_debugger->v2_rx_ep, buf, g_debugger->v2_ep_size, &actual_size, NULL);
    check(res, "WinUsb_WritePipe() failed");

    resp = buf[0];
    size = actual_size - 1;
    memcpy(data, &buf[1], (resp_size < size) ? resp_size : size);
  }
  else
  {
    DWORD actual_size = 0;

    buf[0] = 0x00; // Report ID
    memcpy(&buf[1], data, req_size);

    res = WriteFile(g_handle, (LPCVOID)buf, g_debugger->v1_ep_size + 1, &actual_size, NULL);
    check(res && (int)actual_size == (g_debugger->v1_ep_size + 1), "WriteFile() failed");

    res = ReadFile(g_handle, (LPVOID)buf, g_debugger->v1_ep_size + 1, &actual_size, NULL);
    check(res, "ReadFile() failed");

    resp = buf[1];
    size = actual_size - 2;
    memcpy(data, &buf[2], (resp_size < size) ? resp_size : size);
  }

  if (resp != cmd)
    error_exit("invalid response received: request = 0x%02x, response = 0x%02x", cmd, resp);

  return size;
}
