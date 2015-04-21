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
#include <stdint.h>
#include <stdbool.h>
#include <windows.h>
#include <setupapi.h>          
#include <ddk/hidsdi.h>
#include <ddk/hidpi.h>
#include "dbg.h"
#include "edbg.h"

/*- Definitions -------------------------------------------------------------*/
#define HID_BUFFER_SIZE   513 // Atmel EDBG expects 512 bytes + 1 byte for report ID
#define MAX_STRING_SIZE   256

/*- Types -------------------------------------------------------------------*/

/*- Variables ---------------------------------------------------------------*/
static HANDLE debugger_handle = INVALID_HANDLE_VALUE;
static uint8_t hid_buffer[HID_BUFFER_SIZE];

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
int dbg_enumerate(debugger_t *debuggers, int size)
{
  GUID hid_guid;
  HDEVINFO hid_dev_info;
  HIDD_ATTRIBUTES hid_attr;
  SP_DEVICE_INTERFACE_DATA dev_info_data;
  PSP_DEVICE_INTERFACE_DETAIL_DATA detail_data;
  DWORD detail_size;
  HANDLE handle;
  int rsize = 0;

  HidD_GetHidGuid(&hid_guid);

  hid_dev_info = SetupDiGetClassDevs(&hid_guid, NULL, NULL, DIGCF_PRESENT | DIGCF_INTERFACEDEVICE);

  dev_info_data.cbSize = sizeof(dev_info_data);

  for (int i = 0; i < size; i++)
  {
    if (FALSE == SetupDiEnumDeviceInterfaces(hid_dev_info, 0, &hid_guid, i, &dev_info_data))
      break;

    SetupDiGetDeviceInterfaceDetail(hid_dev_info, &dev_info_data, NULL, 0,
        &detail_size, NULL);

    detail_data = (PSP_DEVICE_INTERFACE_DETAIL_DATA)buf_alloc(detail_size);
    detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

    SetupDiGetDeviceInterfaceDetail(hid_dev_info, &dev_info_data, detail_data,
        detail_size, NULL, NULL);

    handle = CreateFile(detail_data->DevicePath, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    if (INVALID_HANDLE_VALUE != handle)
    {
      hid_attr.Size = sizeof(hid_attr);
      HidD_GetAttributes(handle, &hid_attr);

      if (DBG_VID == hid_attr.VendorID && DBG_PID == hid_attr.ProductID)
      {
        wchar_t wstr[MAX_STRING_SIZE];
        char str[MAX_STRING_SIZE];

        debuggers[rsize].path = strdup(detail_data->DevicePath);

        HidD_GetSerialNumberString(handle, (PVOID)wstr, MAX_STRING_SIZE);
        wcstombs(str, wstr, MAX_STRING_SIZE);
        debuggers[rsize].serial = strdup(str);

        HidD_GetManufacturerString(handle, (PVOID)wstr, MAX_STRING_SIZE);
        wcstombs(str, wstr, MAX_STRING_SIZE);
        debuggers[rsize].manufacturer = strdup(str);

        HidD_GetProductString(handle, (PVOID)wstr, MAX_STRING_SIZE);
        wcstombs(str, wstr, MAX_STRING_SIZE);
        debuggers[rsize].product = strdup(str);

        rsize++;
      }

      CloseHandle(handle);
    }

    buf_free(detail_data);
  }

  SetupDiDestroyDeviceInfoList(hid_dev_info);

  return rsize;
}

//-----------------------------------------------------------------------------
void dbg_open(debugger_t *debugger)
{
  debugger_handle = CreateFile(debugger->path, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

  if (INVALID_HANDLE_VALUE == debugger_handle)
    error_exit("unable to open device");
}

//-----------------------------------------------------------------------------
void dbg_close(void)
{
  if (INVALID_HANDLE_VALUE != debugger_handle)
    CloseHandle(debugger_handle);
}

//-----------------------------------------------------------------------------
int dbg_dap_cmd(uint8_t *data, int size, int rsize)
{
  char cmd = data[0];
  long res;

  memset(hid_buffer, 0xff, HID_BUFFER_SIZE);

  hid_buffer[0] = 0x00; // Report ID
  memcpy(&hid_buffer[1], data, rsize);

  if (FALSE == WriteFile(debugger_handle, (LPCVOID)hid_buffer, HID_BUFFER_SIZE, &res, NULL))
    error_exit("debugger write()");

  if (FALSE == ReadFile(debugger_handle, (LPVOID)hid_buffer, HID_BUFFER_SIZE, &res, NULL))
    error_exit("debugger read()");

  check(hid_buffer[1] == cmd, "invalid response received");

  res -= 2;
  memcpy(data, &hid_buffer[2], (size < res) ? size : res);

  return res;
}

