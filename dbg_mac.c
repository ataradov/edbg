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

/*- Variables ---------------------------------------------------------------*/
static hid_device *handle = NULL;
static uint8_t hid_buffer[1024 + 1];

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
char *wcstombsdup(const wchar_t * const src)
{
  const int len = wcslen(src);
  char * const dst = malloc(len + 1);

  if (dst)
  {
    wcstombs(dst, src, len);
    dst[len] = '\0';
  }

  return dst;
}

//-----------------------------------------------------------------------------
int dbg_enumerate(debugger_t *debuggers, int size) 
{
  struct hid_device_info *devs, *cur_dev;
  int rsize = 0;

  if (hid_init())
    return 0;

  devs = hid_enumerate(0, 0);
  cur_dev = devs;	

  for (cur_dev = devs; cur_dev && rsize < size; cur_dev = cur_dev->next)
  {
    debuggers[rsize].path = strdup(cur_dev->path);
    debuggers[rsize].serial = cur_dev->serial_number ? wcstombsdup(cur_dev->serial_number) : "<unknown>";
    debuggers[rsize].wserial = cur_dev->serial_number ? wcsdup(cur_dev->serial_number) : NULL;
    debuggers[rsize].manufacturer = cur_dev->manufacturer_string ? wcstombsdup(cur_dev->manufacturer_string) : "<unknown>";
    debuggers[rsize].product = cur_dev->product_string ? wcstombsdup(cur_dev->product_string) : "<unknown>";
    debuggers[rsize].vid = cur_dev->vendor_id;
    debuggers[rsize].pid = cur_dev->product_id;

    if (strstr(debuggers[rsize].product, "CMSIS-DAP"))
      rsize++;
  }

  hid_free_enumeration(devs);

  return rsize;
}

//-----------------------------------------------------------------------------
void dbg_open(debugger_t *debugger)
{
  handle = hid_open(debugger->vid, debugger->pid, debugger->wserial);

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
int dbg_get_report_size(void)
{
  // TODO: implement
  return 512;
}

//-----------------------------------------------------------------------------
int dbg_dap_cmd(uint8_t *data, int size, int rsize)
{
  char cmd = data[0];
  int res;

  memset(hid_buffer, 0xff, report_size + 1);

  hid_buffer[0] = 0x00; // Report ID
  memcpy(&hid_buffer[1], data, rsize);

  res = hid_write(handle, hid_buffer, report_size + 1);
  if (res < 0)
  {
    printf("Error: %ls\n", hid_error(handle));
    perror_exit("debugger write()");
  }

  res = hid_read(handle, hid_buffer, report_size + 1);
  if (res < 0)
    perror_exit("debugger read()");

  check(res, "empty response received");

  check(hid_buffer[0] == cmd, "invalid response received");

  res--;
  memcpy(data, &hid_buffer[1], (size < res) ? size : res);

  return res;
}

