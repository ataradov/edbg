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
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "target.h"
#include "edbg.h"
#include "dap.h"

/*- Definitions -------------------------------------------------------------*/
#define SECTOR_SIZE            256

#define DHCSR                  0xe000edf0
#define DEMCR                  0xe000edfc
#define AIRCR                  0xe000ed0c

#define CHIPID_CIDR            0x400e0940

#define EEFC_FMR               0x400e0c00
#define EEFC_FCR               0x400e0c04
#define EEFC_FSR               0x400e0c08
#define EEFC_FRR               0x400e0c0c
#define FSR_FRDY               1

#define CMD_GETD               0x5a000000
#define CMD_WP                 0x5a000001
#define CMD_EPA                0x5a000007
#define CMD_EA                 0x5a000005
#define CMD_SGPB               0x5a00000b

/*- Types -------------------------------------------------------------------*/
typedef struct
{
  uint32_t  chip_id;
  char      *name;
  uint32_t  flash_start;
  uint32_t  flash_size;
  uint32_t  page_size;
} device_t;


/*- Variables ---------------------------------------------------------------*/
static device_t devices[] =
{
  { 0xa1220e00, "SAM V71J21 ",	0x00400000, 	2*1024*1024,	512 },
  { 0, "", 0, 0, 0 },
};

static device_t *device;

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
static void target_cm7_select(void)
{
  uint32_t chip_id;

  // Stop the core
  dap_write_word(DHCSR, 0xa05f0003);
  dap_write_word(DEMCR, 0x00000001);
  dap_write_word(AIRCR, 0xfa050004);

  chip_id = dap_read_word(CHIPID_CIDR);

  for (device = devices; device->chip_id > 0; device++)
  {
    if (device->chip_id == chip_id)
    {
      uint32_t fl_id, fl_size, fl_page_size, fl_nb_palne, fl_nb_lock;

      verbose("Target: %s\n", device->name);

      dap_write_word(EEFC_FCR, CMD_GETD);
      while (0 == (dap_read_word(EEFC_FSR) & FSR_FRDY));

      fl_id = dap_read_word(EEFC_FRR);
      check(fl_id, "Cannot read flash descriptor, check Erase pin state");

      fl_size = dap_read_word(EEFC_FRR);
      check(fl_size == device->flash_size, "Invalid reported Flash size (%d)", fl_size);

      fl_page_size = dap_read_word(EEFC_FRR);
      check(fl_page_size == device->page_size, "Invalid reported page size (%d)", fl_page_size);

      fl_nb_palne = dap_read_word(EEFC_FRR);
      for (uint32_t i = 0; i < fl_nb_palne; i++)
        dap_read_word(EEFC_FRR);

      fl_nb_lock =  dap_read_word(EEFC_FRR);
      for (uint32_t i = 0; i < fl_nb_lock; i++)
        dap_read_word(EEFC_FRR);

      return;
    }
  }

  error_exit("unknown target device (CHIP_ID = 0x%08x)", chip_id);
}

//-----------------------------------------------------------------------------
static void target_cm7_deselect(void)
{
  dap_write_word(DEMCR, 0x00000000);
  dap_write_word(DHCSR, 0xa05f0000);
}

//-----------------------------------------------------------------------------
static void target_cm7_erase(void)
{
  verbose("Erasing... ");

  dap_write_word(EEFC_FCR, CMD_EA);
  while (0 == (dap_read_word(EEFC_FSR) & FSR_FRDY));

  verbose("done.\n");
}

//-----------------------------------------------------------------------------
static void target_cm7_lock(void)
{
  verbose("Locking... ");

  dap_write_word(EEFC_FCR, CMD_SGPB | (0 << 8));

  verbose("done.\n");
}

//-----------------------------------------------------------------------------
static void target_cm7_program(char *name)
{
  uint32_t addr = device->flash_start;
  uint32_t size, number_of_pages;
  uint32_t offs = 0;
  uint8_t *buf;

  buf = buf_alloc(device->flash_size);

  size = load_file(name, buf, device->flash_size);

  memset(&buf[size], 0xff, device->flash_size - size);

  verbose("Programming...");

  number_of_pages = (size + device->page_size - 1) / device->page_size;

  for (uint32_t page = 0; page < number_of_pages; page += 8)
  {
    dap_write_word(EEFC_FCR, CMD_EPA | ((page | 1) << 8));
    while (0 == (dap_read_word(EEFC_FSR) & FSR_FRDY));

    verbose(".");
  }

  verbose(",");

  for (uint32_t page = 0; page < number_of_pages; page++)
  {
    for (uint32_t sector = 0; sector < device->page_size / SECTOR_SIZE; sector++)
    {
      dap_write_block(addr, &buf[offs], SECTOR_SIZE);
      addr += SECTOR_SIZE;
      offs += SECTOR_SIZE;
    }

    dap_write_word(EEFC_FCR, CMD_WP | (page << 8));
    while (0 == (dap_read_word(EEFC_FSR) & FSR_FRDY));

    verbose(".");
  }

  buf_free(buf);

  verbose(" done.\n");
}

//-----------------------------------------------------------------------------
static void target_cm7_verify(char *name)
{
  uint32_t addr = device->flash_start;
  uint32_t size, block_size;
  uint32_t offs = 0;
  uint8_t *bufa, *bufb;

  bufa = buf_alloc(device->flash_size);
  bufb = buf_alloc(SECTOR_SIZE);

  size = load_file(name, bufa, device->flash_size);

  verbose("Verification...");

  while (size)
  {
    dap_read_block(addr, bufb, SECTOR_SIZE);

    block_size = (size > SECTOR_SIZE) ? SECTOR_SIZE : size;

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

    addr += SECTOR_SIZE;
    offs += SECTOR_SIZE;
    size -= block_size;

    verbose(".");
  }

  free(bufa);
  free(bufb);

  verbose(" done.\n");
}

//-----------------------------------------------------------------------------
static void target_cm7_read(char *name)
{
  uint32_t size = device->flash_size;
  uint32_t addr = device->flash_start;
  uint32_t offs = 0;
  uint8_t *buf;

  buf = buf_alloc(device->flash_size);

  verbose("Reading...");

  while (size)
  {
    for (uint32_t sector = 0; sector < device->page_size / SECTOR_SIZE; sector++)
    {
      dap_read_block(addr, &buf[offs], SECTOR_SIZE);
      addr += SECTOR_SIZE;
      offs += SECTOR_SIZE;
      size -= SECTOR_SIZE;
    }

    verbose(".");
  }

  save_file(name, buf, device->flash_size);

  buf_free(buf);

  verbose(" done.\n");
}

//-----------------------------------------------------------------------------
target_ops_t target_cm7_ops = 
{
  .select   = target_cm7_select,
  .deselect = target_cm7_deselect,
  .erase    = target_cm7_erase,
  .lock     = target_cm7_lock,
  .program  = target_cm7_program,
  .verify   = target_cm7_verify,
  .read     = target_cm7_read,
};

