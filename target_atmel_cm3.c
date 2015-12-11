/*
 * Copyright (c) 2013-2015, Alex Taradov <alex@taradov.com>
 * Copyright (c) 2015, Thibaut VIARD for derivative work from original target_atmel_cm4.c and SAM3X/A port
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
#define ARM_DAP_DHCSR          0xe000edf0
#define ARM_DAP_DEMCR          0xe000edfc
#define ARM_SCB_AIRCR          0xe000ed0c

#define CHIPID_CIDR            0x400E0940
#define CHIPID_EXID            0x400E0944

#define EEFC_BASE              (0x400E0A00) // be aware this is not the case for SAM3U!!!
#define EEFC_FMR(n)            (EEFC_BASE + 0x00 + (n) * 0x200) // EEFC Flash Mode Register      Read-write
#define EEFC_FCR(n)            (EEFC_BASE + 0x04 + (n) * 0x200) // EEFC Flash Command Register   Write-only
#define EEFC_FSR(n)            (EEFC_BASE + 0x08 + (n) * 0x200) // EEFC Flash Status Register    Read-only
#define EEFC_FRR(n)            (EEFC_BASE + 0x0c + (n) * 0x200) // EEFC Flash Result Register    Read-only
#define FSR_FRDY               (1ul)

#define CMD_GETD               0x5a000000 // Get Flash Descriptor
#define CMD_WP                 0x5a000001 // Write page
#define CMD_EWP                0x5a000003 // Erase page and write page
#define CMD_EA                 0x5a000005 // Erase all
#define CMD_SGPB               0x5a00000B // Set GPNVM Bit

/*- Types -------------------------------------------------------------------*/
typedef struct
{
  uint32_t  chip_id;
  uint32_t  chip_exid;
  char      *name;
  uint32_t  flash_start;
  uint32_t  n_planes;
  uint32_t  flash_size;
  uint32_t  page_size;
} device_t;

/*- Variables ---------------------------------------------------------------*/
static device_t devices[] =
{
  { 0x286E0A60, 0x00000000, "ATSAM3X8H",         0x00080000, 2,  256*1024, 256 },
  { 0x285E0A60, 0x00000000, "ATSAM3X8E",         0x00080000, 2,  256*1024, 256 },
  { 0x285B0960, 0x00000000, "ATSAM3X4E",         0x00080000, 2,  128*1024, 256 },
  { 0x284E0A60, 0x00000000, "ATSAM3X8C",         0x00080000, 2,  256*1024, 256 },
  { 0x284B0960, 0x00000000, "ATSAM3X4C",         0x00080000, 2,  128*1024, 256 },
  { 0x283E0A60, 0x00000000, "ATSAM3A8C",         0x00080000, 2,  256*1024, 256 },
  { 0x283B0960, 0x00000000, "ATSAM3A4C",         0x00080000, 2,  128*1024, 256 },
  { 0, 0, "", 0, 0, 0, 0 },
};

static device_t *device;

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
static void target_select(void)
{
  uint32_t chip_id, chip_exid;

  // Set boot mode GPNVM bit as a workaraound
  dap_write_word(EEFC_FCR(0), CMD_SGPB | (1 << 8));

  // Stop the core
  dap_write_word(ARM_DAP_DHCSR, 0xa05f0003);
  dap_write_word(ARM_DAP_DEMCR, 0x00000001);
  dap_write_word(ARM_SCB_AIRCR, 0x05fa0004);

  chip_id = dap_read_word(CHIPID_CIDR);
  chip_exid = dap_read_word(CHIPID_EXID);

  for (device = devices; device->chip_id > 0; device++)
  {
    if (device->chip_id == chip_id && device->chip_exid == chip_exid)
    {
      uint32_t fl_id, fl_size, fl_page_size, fl_nb_palne, fl_nb_lock;

      verbose("Target: %s\n", device->name);

      for (uint32_t plane = 0; plane < device->n_planes; plane++)
      {
        dap_write_word(EEFC_FCR(plane), CMD_GETD);
        while (0 == (dap_read_word(EEFC_FSR(plane)) & FSR_FRDY));

        fl_id = dap_read_word(EEFC_FRR(plane));
        check(fl_id, "Cannot read flash descriptor, check Erase pin state");

        fl_size = dap_read_word(EEFC_FRR(plane));
        check(fl_size == device->flash_size, "Invalid reported Flash size (%d)", fl_size);

        fl_page_size = dap_read_word(EEFC_FRR(plane));
        check(fl_page_size == device->page_size, "Invalid reported page size (%d)", fl_page_size);

        fl_nb_palne = dap_read_word(EEFC_FRR(plane));
        for (uint32_t i = 0; i < fl_nb_palne; i++)
          dap_read_word(EEFC_FRR(plane));

        fl_nb_lock =  dap_read_word(EEFC_FRR(plane));
        for (uint32_t i = 0; i < fl_nb_lock; i++)
          dap_read_word(EEFC_FRR(plane));
      }

      return;
    }
  }

  error_exit("unknown target device (CHIP_ID = 0x%08x)", chip_id);
}

//-----------------------------------------------------------------------------
static void target_deselect(void)
{
  dap_write_word(ARM_DAP_DHCSR, 0xa05f0000);
  dap_write_word(ARM_DAP_DEMCR, 0x00000000);
  dap_write_word(ARM_SCB_AIRCR, 0x05fa0004);
}

//-----------------------------------------------------------------------------
static void target_erase(void)
{
  verbose("Erasing... ");

  for (uint32_t plane = 0; plane < device->n_planes; plane++)
    dap_write_word(EEFC_FCR(plane), CMD_EA);

  for (uint32_t plane = 0; plane < device->n_planes; plane++)
    while (0 == (dap_read_word(EEFC_FSR(plane)) & FSR_FRDY));

  verbose("done.\n");
}

//-----------------------------------------------------------------------------
static void target_lock(void)
{
  verbose("Locking... ");

  // It is enough to lock just one plane to lock the entire device
  dap_write_word(EEFC_FCR(0), CMD_SGPB | (0 << 8));

  verbose("done.\n");
}

//-----------------------------------------------------------------------------
static void target_program(char *name)
{
  uint32_t addr = device->flash_start;
  uint32_t flash_size = device->flash_size * device->n_planes;
  uint32_t size, number_of_pages, plane;
  uint32_t offs = 0;
  uint8_t *buf;

  buf = buf_alloc(flash_size);

  size = load_file(name, buf, flash_size);

  memset(&buf[size], 0xff, flash_size - size);

  verbose("Programming...");

  number_of_pages = (size + device->page_size - 1) / device->page_size;

  for (uint32_t page = 0; page < number_of_pages; page++)
  {
    dap_write_block(addr, &buf[offs], device->page_size);
    addr += device->page_size;
    offs += device->page_size;

    plane = page / (device->flash_size / device->page_size);

    dap_write_word(EEFC_FCR(plane), CMD_EWP | (page << 8));
    while (0 == (dap_read_word(EEFC_FSR(plane)) & FSR_FRDY));

    verbose(".");
  }

  buf_free(buf);

  verbose(" done.\n");
}

//-----------------------------------------------------------------------------
static void target_verify(char *name)
{
  uint32_t addr = device->flash_start;
  uint32_t flash_size = device->flash_size * device->n_planes;
  uint32_t size, block_size;
  uint32_t offs = 0;
  uint8_t *bufa, *bufb;

  bufa = buf_alloc(flash_size);
  bufb = buf_alloc(device->page_size);

  size = load_file(name, bufa, flash_size);

  verbose("Verification...");

  while (size)
  {
    dap_read_block(addr, bufb, device->page_size);

    block_size = (size > device->page_size) ? device->page_size : size;

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

    addr += device->page_size;
    offs += device->page_size;
    size -= block_size;

    verbose(".");
  }

  free(bufa);
  free(bufb);

  verbose(" done.\n");
}

//-----------------------------------------------------------------------------
static void target_read(char *name)
{
  uint32_t flash_size = device->flash_size * device->n_planes;
  uint32_t size = flash_size;
  uint32_t addr = device->flash_start;
  uint32_t offs = 0;
  uint8_t *buf;

  buf = buf_alloc(flash_size);

  verbose("Reading...");

  while (size)
  {
    dap_read_block(addr, &buf[offs], device->page_size);

    addr += device->page_size;
    offs += device->page_size;
    size -= device->page_size;

    verbose(".");
  }

  save_file(name, buf, flash_size);

  buf_free(buf);

  verbose(" done.\n");
}

//-----------------------------------------------------------------------------
target_ops_t target_atmel_cm3_ops =
{
  .select   = target_select,
  .deselect = target_deselect,
  .erase    = target_erase,
  .lock     = target_lock,
  .program  = target_program,
  .verify   = target_verify,
  .read     = target_read,
};

