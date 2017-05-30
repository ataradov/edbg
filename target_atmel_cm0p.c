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
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "target.h"
#include "edbg.h"
#include "dap.h"

/*- Definitions -------------------------------------------------------------*/
#define DHCSR                  0xe000edf0
#define DEMCR                  0xe000edfc
#define AIRCR                  0xe000ed0c

#define DSU_CTRL_STATUS        0x41002100
#define DSU_DID                0x41002118

/*- Types -------------------------------------------------------------------*/
typedef struct
{
  uint32_t  dsu_did;
  char      *name;
  uint32_t  flash_start;
  uint32_t  flash_size;
  uint32_t  page_size;
  uint32_t  n_pages;
  uint32_t  row_size;
} device_t;

/*- Variables ---------------------------------------------------------------*/
static device_t devices[] =
{
  { 0x10040107, "SAM D09C13A",         0,   8*1024, 64,  128, 256 },
  { 0x10020100, "SAM D10D14AM",        0,  16*1024, 64,  256, 256 },
  { 0x10030100, "SAM D11D14A",         0,  16*1024, 64,  256, 256 },
  { 0x10030000, "SAM D11D14AM",        0,  16*1024, 64,  256, 256 },
  { 0x10030003, "SAM D11D14AS",        0,  16*1024, 64,  256, 256 },
  { 0x10030006, "SAM D11C14A",         0,  16*1024, 64,  256, 256 },
  { 0x10030106, "SAM D11C14A (Rev B)", 0,  16*1024, 64,  256, 256 },
  { 0x1000140a, "SAM D20E18A",         0, 256*1024, 64, 4096, 256 },
  { 0x10001100, "SAM D20J18A",         0, 256*1024, 64, 4096, 256 },
  { 0x10001200, "SAM D20J18A (Rev C)", 0, 256*1024, 64, 4096, 256 },
  { 0x10010100, "SAM D21J18A",         0, 256*1024, 64, 4096, 256 },
  { 0x10010200, "SAM D21J18A (Rev C)", 0, 256*1024, 64, 4096, 256 },
  { 0x10010300, "SAM D21J18A (Rev D)", 0, 256*1024, 64, 4096, 256 },
  { 0x1001020d, "SAM D21E15A (Rev C)", 0,  32*1024, 64,  512, 256 },
  { 0x1001030a, "SAM D21E18A",         0, 256*1024, 64, 4096, 256 },
  { 0x10010205, "SAM D21G18A",         0, 256*1024, 64, 4096, 256 },
  { 0x10010305, "SAM D21G18A (Rev D)", 0, 256*1024, 64, 4096, 256 },
  { 0x10010019, "SAM R21G18 ES",       0, 256*1024, 64, 4096, 256 },
  { 0x10010119, "SAM R21G18",          0, 256*1024, 64, 4096, 256 },
  { 0x10010319, "SAM R21G18A",         0, 256*1024, 64, 4096, 256 },
  { 0x11010100, "SAM C21J18A ES",      0, 256*1024, 64, 4096, 256 },
  { 0x10810219, "SAM L21E18B",         0, 256*1024, 64, 4096, 256 },
  { 0x1081020f, "SAM L21J18B",         0, 256*1024, 64, 4096, 256 },
  { 0 },
};

static device_t target_device;
static target_options_t target_options;

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
static void target_select(target_options_t *options)
{
  uint32_t dsu_did;

  // Stop the core
  dap_write_word(DHCSR, 0xa05f0003);
  dap_write_word(DEMCR, 0x00000001);
  dap_write_word(AIRCR, 0x05fa0004);

  dsu_did = dap_read_word(DSU_DID);

  for (device_t *device = devices; device->dsu_did > 0; device++)
  {
    if (device->dsu_did == dsu_did)
    {
      verbose("Target: %s\n", device->name);

      target_device = *device;
      target_options = *options;

      target_check_options(&target_options, device->flash_size, device->row_size);

      return;
    }
  }

  error_exit("unknown target device (DSU_DID = 0x%08x)", dsu_did);
}

//-----------------------------------------------------------------------------
static void target_deselect(void)
{
  dap_write_word(DHCSR, 0xa05f0000);
  dap_write_word(DEMCR, 0x00000000);
  dap_write_word(AIRCR, 0x05fa0004);

  target_free_options(&target_options);
}

//-----------------------------------------------------------------------------
static void target_erase(void)
{
  dap_write_word(DSU_CTRL_STATUS, 0x00001f00); // Clear flags
  dap_write_word(DSU_CTRL_STATUS, 0x00000010); // Chip erase
  sleep_ms(100);
  while (0 == (dap_read_word(DSU_CTRL_STATUS) & 0x00000100));
}

//-----------------------------------------------------------------------------
static void target_lock(void)
{
  dap_write_word(0x41004000, 0x0000a545); // Set Security Bit
}

//-----------------------------------------------------------------------------
static void target_program(void)
{
  uint32_t addr = target_device.flash_start + target_options.offset;
  uint32_t offs = 0;
  uint32_t number_of_rows;
  uint8_t *buf = target_options.file_data;
  uint32_t size = target_options.file_size;

  if (dap_read_word(DSU_CTRL_STATUS) & 0x00010000)
    error_exit("device is locked, perform a chip erase before programming");

  number_of_rows = (size + target_device.row_size - 1) / target_device.row_size;

  dap_write_word(0x41004004, 0); // Enable automatic write

  for (uint32_t row = 0; row < number_of_rows; row++)
  {
    dap_write_word(0x4100401c, addr >> 1);

    dap_write_word(0x41004000, 0x0000a541); // Unlock Region
    while (0 == (dap_read_word(0x41004014) & 1));

    dap_write_word(0x41004000, 0x0000a502); // Erase Row
    while (0 == (dap_read_word(0x41004014) & 1));

    dap_write_block(addr, &buf[offs], target_device.row_size);

    addr += target_device.row_size;
    offs += target_device.row_size;

    verbose(".");
  }
}

//-----------------------------------------------------------------------------
static void target_verify(void)
{
  uint32_t addr = target_device.flash_start + target_options.offset;
  uint32_t block_size;
  uint32_t offs = 0;
  uint8_t *bufb;
  uint8_t *bufa = target_options.file_data;
  uint32_t size = target_options.file_size;

  if (dap_read_word(DSU_CTRL_STATUS) & 0x00010000)
    error_exit("device is locked, unable to verify");

  bufb = buf_alloc(target_device.row_size);

  while (size)
  {
    dap_read_block(addr, bufb, target_device.row_size);

    block_size = (size > target_device.row_size) ? target_device.row_size : size;

    for (int i = 0; i < (int)block_size; i++)
    {
      if (bufa[offs + i] != bufb[i])
      {
        verbose("\nat address 0x%x expected 0x%02x, read 0x%02x\n",
            addr + i, bufa[offs + i], bufb[i]);
        buf_free(bufb);
        error_exit("verification failed");
      }
    }

    addr += target_device.row_size;
    offs += target_device.row_size;
    size -= block_size;

    verbose(".");
  }

  buf_free(bufb);
}

//-----------------------------------------------------------------------------
static void target_read(void)
{
  uint32_t addr = target_device.flash_start + target_options.offset;
  uint32_t offs = 0;
  uint8_t *buf = target_options.file_data;
  uint32_t size = target_options.size;

  if (dap_read_word(DSU_CTRL_STATUS) & 0x00010000)
    error_exit("device is locked, unable to read");

  while (size)
  {
    dap_read_block(addr, &buf[offs], target_device.row_size);

    addr += target_device.row_size;
    offs += target_device.row_size;
    size -= target_device.row_size;

    verbose(".");
  }

  save_file(target_options.name, buf, target_options.size);
}

//-----------------------------------------------------------------------------
target_ops_t target_atmel_cm0p_ops =
{
  .select   = target_select,
  .deselect = target_deselect,
  .erase    = target_erase,
  .lock     = target_lock,
  .program  = target_program,
  .verify   = target_verify,
  .read     = target_read,
};

