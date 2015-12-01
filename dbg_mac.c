/*
 * Copyright (c) 2015, Lourens Rozema <me@LourensRozema.nl>
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
#include <wchar.h>
#include "hidapi.h"
#include "edbg.h"
#include "dbg.h"

/*- Definitions -------------------------------------------------------------*/
#define HID_BUFFER_SIZE   513 // Atmel EDBG expects 512 bytes + 1 byte for report ID

/*- Types -------------------------------------------------------------------*/

/*- Variables ---------------------------------------------------------------*/
static hid_device *handle = NULL;
static uint8_t hid_buffer[HID_BUFFER_SIZE];

/*- Implementations ---------------------------------------------------------*/

char * wcstombsdup(const wchar_t * const src)
{
  const int len = wcslen(src);
  char * const dst = malloc(len+1);
  if(dst)
  {
    wcstombs(dst, src, len);
    dst[len] = '\0';
  }
  return(dst);
}

//-----------------------------------------------------------------------------
int dbg_enumerate(debugger_t *debuggers, int size)
{
  int rsize = 0;

  struct hid_device_info *devs, *cur_dev;

  if (hid_init())
    return -1;

  devs = hid_enumerate(0x0, 0x0);
  cur_dev = devs;
  for(cur_dev = devs; cur_dev && rsize < size; cur_dev = cur_dev->next)
  {
    if(dbg_validate_dap(cur_dev->vendor_id, cur_dev->product_id) != -1)
    {
      debuggers[rsize].path = strdup(cur_dev->path);
      debuggers[rsize].serial = cur_dev->serial_number ? wcstombsdup(cur_dev->serial_number) : "<unknown>";
      debuggers[rsize].wserial = cur_dev->serial_number ? wcsdup(cur_dev->serial_number) : NULL;
      debuggers[rsize].manufacturer = cur_dev->manufacturer_string ? wcstombsdup(cur_dev->manufacturer_string) : "<unknown>";
      debuggers[rsize].product = cur_dev->product_string ? wcstombsdup(cur_dev->product_string) : "<unknown>";
      debuggers[rsize].VID = cur_dev->vendor_id;
      debuggers[rsize].PID = cur_dev->product_id;

      rsize++;
    }
  }
  hid_free_enumeration(devs);

  return rsize;
}

//-----------------------------------------------------------------------------
void dbg_open(debugger_t *debugger)
{
  handle = hid_open(debugger->VID, debugger->PID, debugger->wserial);
  if (!handle)
    perror_exit("unable to open device");
}

//-----------------------------------------------------------------------------
void dbg_close(void)
{
  if (handle)
    hid_close(handle);
}

//-----------------------------------------------------------------------------
int dbg_dap_cmd(uint8_t *data, int size, int rsize)
{
  char cmd = data[0];
  int res;

  memset(hid_buffer, 0xff, HID_BUFFER_SIZE);

  hid_buffer[0] = 0x00; // Report ID
  memcpy(&hid_buffer[1], data, rsize);

  res = hid_write(handle, hid_buffer, HID_BUFFER_SIZE/*rsize+1*/); // Atmel EDBG expects 512 bytes
  if (res < 0)
  {
    printf("Error: %ls\n", hid_error(handle));
    perror_exit("debugger write()");
  }

  res = hid_read(handle, hid_buffer, sizeof(hid_buffer));
  if (res < 0)
    perror_exit("debugger read()");

  check(res, "empty response received");

  check(hid_buffer[0] == cmd, "invalid response received");

  res--;
  memcpy(data, &hid_buffer[1], (size < res) ? size : res);

  return res;
}

