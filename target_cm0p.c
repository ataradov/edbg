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
  { 0x10001100, "SAM D20J18A",		0,	256*1024,	64,	4096,	256 }, 
  { 0x10010100, "SAM D21J18A",		0,	256*1024,	64,	4096,	256 },
  { 0x10010200, "SAM D21J18A Rev C",	0,	256*1024,	64,	4096,	256 },
  { 0x10010205, "SAM D21G18A",		0,	256*1024,	64,	4096,	256 },
  { 0x10010019, "SAM R21G18 ES",	0,	256*1024,	64,	4096,	256 },
  { 0x10010119, "SAM R21G18",		0,	256*1024,	64,	4096,	256 },
  { 0x10010219, "SAM R21G18 A",		0,	256*1024,	64,	4096,	256 },
  { 0x11010100, "SAM C21J18A ES",	0,	256*1024,	64,	4096,	256 },
  { 0, "", 0, 0, 0, 0, 0 },
};

static device_t *device;

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
static void target_cm0p_select(void)
{
  uint32_t dsu_did;

  // Stop the core
  dap_write_word(DHCSR, 0xa05f0003);
  dap_write_word(DEMCR, 0x00000001);
  dap_write_word(AIRCR, 0x05fa0004);

  dsu_did = dap_read_word(DSU_DID);

  for (device = devices; device->dsu_did > 0; device++)
  {
    if (device->dsu_did == dsu_did)
    {
      verbose("Target: %s\n", device->name);
      return;
    }
  }

  error_exit("unknown target device (DSU_DID = 0x%08x)", dsu_did);
}

//-----------------------------------------------------------------------------
static void target_cm0p_deselect(void)
{
  dap_write_word(DHCSR, 0xa05f0000);
  dap_write_word(DEMCR, 0x00000000);
  dap_write_word(AIRCR, 0x05fa0004);
}

//-----------------------------------------------------------------------------
static void target_cm0p_erase(void)
{
  verbose("Erasing... ");

  dap_write_word(DSU_CTRL_STATUS, 0x00001f00); // Clear flags
  dap_write_word(DSU_CTRL_STATUS, 0x00000010); // Chip erase
  usleep(100000);
  while (0 == (dap_read_word(0x41002100) & 0x00000100));

  verbose("done.\n");
}

//-----------------------------------------------------------------------------
static void target_cm0p_lock(void)
{
  verbose("Locking... ");

  dap_write_word(0x41004000, 0x0000a545); // Set Security Bit

  verbose("done.\n");
}

//-----------------------------------------------------------------------------
static void target_cm0p_program(char *name)
{
  uint32_t addr = device->flash_start;
  uint32_t size;
  uint32_t offs = 0;
  uint32_t number_of_rows;
  uint8_t *buf;

  if (dap_read_word(DSU_CTRL_STATUS) & 0x00010000)
    error_exit("devices is locked, perform a chip erase before programming");

  buf = buf_alloc(device->flash_size);

  size = load_file(name, buf, device->flash_size);

  memset(&buf[size], 0xff, device->flash_size - size);

  verbose("Programming...");

  number_of_rows = (size + device->row_size - 1) / device->row_size;

  dap_write_word(0x41004004, 0); // Enable automatic write

  for (uint32_t row = 0; row < number_of_rows; row++)
  {
    dap_write_word(0x4100401c, addr >> 1);

    dap_write_word(0x41004000, 0x0000a541); // Unlock Region
    while (0 == (dap_read_word(0x41004014) & 1));

    dap_write_word(0x41004000, 0x0000a502); // Erase Row
    while (0 == (dap_read_word(0x41004014) & 1));

    dap_write_block(addr, &buf[offs], device->row_size);

    addr += device->row_size;
    offs += device->row_size;

    verbose(".");
  }

  buf_free(buf);

  verbose(" done.\n");
}

//-----------------------------------------------------------------------------
static void target_cm0p_verify(char *name)
{
  uint32_t addr = device->flash_start;
  uint32_t size, block_size;
  uint32_t offs = 0;
  uint8_t *bufa, *bufb;

  if (dap_read_word(DSU_CTRL_STATUS) & 0x00010000)
    error_exit("devices is locked, unable to verify");

  bufa = buf_alloc(device->flash_size);
  bufb = buf_alloc(device->row_size);

  size = load_file(name, bufa, device->flash_size);

  verbose("Verification...");

  while (size)
  {
    dap_read_block(addr, bufb, device->row_size);

    block_size = (size > device->row_size) ? device->row_size : size;

    for (int i = 0; i < (int)block_size; i++)
    {
      if (bufa[offs + i] != bufb[i])
      {
        verbose("\nat address 0x%x expected 0x%02x, read 0x%02x\n",
            addr + i, bufa[offs + i], bufb[i]);
        free(bufa);
        free(bufb);
        error_exit("verification failed");
      }
    }

    addr += device->row_size;
    offs += device->row_size;
    size -= block_size;

    verbose(".");
  }

  free(bufa);
  free(bufb);

  verbose(" done.\n");
}

//-----------------------------------------------------------------------------
static void target_cm0p_read(char *name)
{
  uint32_t size = device->flash_size;
  uint32_t addr = device->flash_start;
  uint32_t offs = 0;
  uint8_t *buf;

  if (dap_read_word(DSU_CTRL_STATUS) & 0x00010000)
    error_exit("devices is locked, unable to read");

  buf = buf_alloc(device->flash_size);

  verbose("Reading...");

  while (size)
  {
    dap_read_block(addr, &buf[offs], device->row_size);

    addr += device->row_size;
    offs += device->row_size;
    size -= device->row_size;

    verbose(".");
  }

  save_file(name, buf, device->flash_size);

  buf_free(buf);

  verbose(" done.\n");
}

//-----------------------------------------------------------------------------
target_ops_t target_cm0p_ops = 
{
  .select   = target_cm0p_select,
  .deselect = target_cm0p_deselect,
  .erase    = target_cm0p_erase,
  .lock     = target_cm0p_lock,
  .program  = target_cm0p_program,
  .verify   = target_cm0p_verify,
  .read     = target_cm0p_read,
};

