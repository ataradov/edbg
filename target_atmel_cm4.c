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
#define DHCSR                  0xe000edf0
#define DEMCR                  0xe000edfc
#define AIRCR                  0xe000ed0c

#define CHIPID_CIDR            0x400e0740
#define CHIPID_EXID            0x400e0744

#define EEFC_FMR(n)            (0x400e0a00 + (n) * 0x200)
#define EEFC_FCR(n)            (0x400e0a04 + (n) * 0x200)
#define EEFC_FSR(n)            (0x400e0a08 + (n) * 0x200)
#define EEFC_FRR(n)            (0x400e0a0c + (n) * 0x200)
#define FSR_FRDY               1

#define CMD_GETD               0x5a000000
#define CMD_WP                 0x5a000001
#define CMD_EPA                0x5a000007
#define CMD_EA                 0x5a000005
#define CMD_SGPB               0x5a00000b

#define PAGES_IN_ERASE_BLOCK   16

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
  { 0x243b09e0, 0x00000000, "SAM G51G18",         0x00400000, 1,  256*1024, 512 },
  { 0x243b09e8, 0x00000000, "SAM G51N18",         0x00400000, 1,  256*1024, 512 },
  { 0x247e0ae0, 0x00000000, "SAM G53G19 (Rev A)", 0x00400000, 1,  512*1024, 512 },
  { 0x247e0ae1, 0x00000000, "SAM G53G19 (Rev B)", 0x00400000, 1,  512*1024, 512 },
  { 0x247e0ae8, 0x00000000, "SAM G53N19 (Rev A)", 0x00400000, 1,  512*1024, 512 },
  { 0x247e0ae9, 0x00000000, "SAM G53N19 (Rev B)", 0x00400000, 1,  512*1024, 512 },
  { 0x247e0ae2, 0x00000000, "SAM G54G19 (Rev A)", 0x00400000, 1,  512*1024, 512 },
  { 0x247e0ae3, 0x00000000, "SAM G54G19 (Rev B)", 0x00400000, 1,  512*1024, 512 },
  { 0x247e0ae6, 0x00000000, "SAM G54J19 (Rev A)", 0x00400000, 1,  512*1024, 512 },
  { 0x247e0aea, 0x00000000, "SAM G54N19 (Rev A)", 0x00400000, 1,  512*1024, 512 },
  { 0x247e0aeb, 0x00000000, "SAM G54N19 (Rev B)", 0x00400000, 1,  512*1024, 512 },
  { 0x24470ae0, 0x00000000, "SAM G55G19",         0x00400000, 1,  512*1024, 512 },
  { 0x24570ae0, 0x00000000, "SAM G55J19",         0x00400000, 1,  512*1024, 512 },
  { 0x29970ee0, 0x00000000, "SAM4SD32B (Rev A)",  0x00400000, 2, 1024*1024, 512 },
  { 0x29970ee1, 0x00000000, "SAM4SD32B (Rev B)",  0x00400000, 2, 1024*1024, 512 },
  { 0x29a70ee0, 0x00000000, "SAM4SD32C (Rev A)",  0x00400000, 2, 1024*1024, 512 },
  { 0x29a70ee1, 0x00000000, "SAM4SD32C (Rev B)",  0x00400000, 2, 1024*1024, 512 },
  { 0x29970ce0, 0x00000000, "SAM4SD16B (Rev A)",  0x00400000, 2,  512*1024, 512 },
  { 0x29970ce0, 0x00000000, "SAM4SD16B (Rev B)",  0x00400000, 2,  512*1024, 512 },
  { 0x29a70ce0, 0x00000000, "SAM4SD16C (Rev A)",  0x00400000, 2,  512*1024, 512 },
  { 0x29a70ce1, 0x00000000, "SAM4SD16C (Rev B)",  0x00400000, 2,  512*1024, 512 },
  { 0x28970ce0, 0x00000000, "SAM4SA16B (Rev A)",  0x00400000, 1, 1024*1024, 512 },
  { 0x28970ce1, 0x00000000, "SAM4SA16B (Rev B)",  0x00400000, 1, 1024*1024, 512 },
  { 0x28a70ce0, 0x00000000, "SAM4SA16C (Rev A)",  0x00400000, 1, 1024*1024, 512 },
  { 0x28a70ce1, 0x00000000, "SAM4SA16C (Rev B)",  0x00400000, 1, 1024*1024, 512 },
  { 0x289c0ce0, 0x00000000, "SAM4S16B (Rev A)",   0x00400000, 1, 1024*1024, 512 },
  { 0x289c0ce1, 0x00000000, "SAM4S16B (Rev B)",   0x00400000, 1, 1024*1024, 512 },
  { 0x28ac0ce0, 0x00000000, "SAM4S16C (Rev A)",   0x00400000, 1, 1024*1024, 512 },
  { 0x28ac0ce1, 0x00000000, "SAM4S16C (Rev B)",   0x00400000, 1, 1024*1024, 512 },
  { 0x289c0ae0, 0x00000000, "SAM4S8B (Rev A)",    0x00400000, 1,  512*1024, 512 },
  { 0x289c0ae1, 0x00000000, "SAM4S8B (Rev B)",    0x00400000, 1,  512*1024, 512 },
  { 0x28ac0ae0, 0x00000000, "SAM4S8C (Rev A)",    0x00400000, 1,  512*1024, 512 },
  { 0x28ac0ae1, 0x00000000, "SAM4S8C (Rev B)",    0x00400000, 1,  512*1024, 512 },
  { 0x288b09e0, 0x00000000, "SAM4S4A (Rev A)",    0x00400000, 1,  256*1024, 512 },
  { 0x288b09e1, 0x00000000, "SAM4S4A (Rev B)",    0x00400000, 1,  256*1024, 512 },
  { 0x289b09e0, 0x00000000, "SAM4S4B (Rev A)",    0x00400000, 1,  256*1024, 512 },
  { 0x289b09e1, 0x00000000, "SAM4S4B (Rev B)",    0x00400000, 1,  256*1024, 512 },
  { 0x28ab09e0, 0x00000000, "SAM4S4C (Rev A)",    0x00400000, 1,  256*1024, 512 },
  { 0x28ab09e1, 0x00000000, "SAM4S4C (Rev B)",    0x00400000, 1,  256*1024, 512 },
  { 0x288b07e0, 0x00000000, "SAM4S2A (Rev A)",    0x00400000, 1,  128*1024, 512 },
  { 0x288b07e1, 0x00000000, "SAM4S2A (Rev B)",    0x00400000, 1,  128*1024, 512 },
  { 0x289b07e0, 0x00000000, "SAM4S2B (Rev A)",    0x00400000, 1,  128*1024, 512 },
  { 0x289b07e1, 0x00000000, "SAM4S2B (Rev B)",    0x00400000, 1,  128*1024, 512 },
  { 0x28ab07e0, 0x00000000, "SAM4S2C (Rev A)",    0x00400000, 1,  128*1024, 512 },
  { 0x28ab07e1, 0x00000000, "SAM4S2C (Rev B)",    0x00400000, 1,  128*1024, 512 },
  { 0xa3cc0ce0, 0x00120200, "SAM4E16E",           0x00400000, 1, 1024*1024, 512 },
  { 0xa3cc0ce0, 0x00120208, "SAM4E8E",            0x00400000, 1,  512*1024, 512 },
  { 0xa3cc0ce0, 0x00120201, "SAM4E16C",           0x00400000, 1, 1024*1024, 512 },
  { 0xa3cc0ce0, 0x00120209, "SAM4E8C",            0x00400000, 1,  512*1024, 512 },
  { 0x29460ce0, 0x00000000, "SAM4N16B (Rev A)",   0x00400000, 1, 1024*1024, 512 },
  { 0x29560ce0, 0x00000000, "SAM4N16C (Rev A)",   0x00400000, 1, 1024*1024, 512 },
  { 0x293b0ae0, 0x00000000, "SAM4N8A (Rev A)",    0x00400000, 1,  512*1024, 512 },
  { 0x294b0ae0, 0x00000000, "SAM4N8B (Rev A)",    0x00400000, 1,  512*1024, 512 },
  { 0x295b0ae0, 0x00000000, "SAM4N8C (Rev A)",    0x00400000, 1,  512*1024, 512 },
  { 0 },
};

static device_t target_device;
static target_options_t target_options;

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
static void target_select(target_options_t *options)
{
  uint32_t chip_id, chip_exid;

  // Stop the core
  dap_write_word(DHCSR, 0xa05f0003);
  dap_write_word(DEMCR, 0x00000001);
  dap_write_word(AIRCR, 0x05fa0004);

  chip_id = dap_read_word(CHIPID_CIDR);
  chip_exid = dap_read_word(CHIPID_EXID);

  for (device_t *device = devices; device->chip_id > 0; device++)
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

      target_device = *device;
      target_options = *options;

      target_check_options(&target_options, device->flash_size * target_device.n_planes,
          device->page_size * PAGES_IN_ERASE_BLOCK);

      return;
    }
  }

  error_exit("unknown target device (CHIP_ID = 0x%08x)", chip_id);
}

//-----------------------------------------------------------------------------
static void target_deselect(void)
{
  dap_write_word(DEMCR, 0x00000000);
  dap_write_word(AIRCR, 0x05fa0004);

  target_free_options(&target_options);
}

//-----------------------------------------------------------------------------
static void target_erase(void)
{
  for (uint32_t plane = 0; plane < target_device.n_planes; plane++)
    dap_write_word(EEFC_FCR(plane), CMD_EA);

  for (uint32_t plane = 0; plane < target_device.n_planes; plane++)
    while (0 == (dap_read_word(EEFC_FSR(plane)) & FSR_FRDY));
}

//-----------------------------------------------------------------------------
static void target_lock(void)
{
  // It is enough to lock just one plane to lock the entire device
  dap_write_word(EEFC_FCR(0), CMD_SGPB | (0 << 8));
}

//-----------------------------------------------------------------------------
static void target_program(void)
{
  uint32_t addr = target_device.flash_start + target_options.offset;
  uint32_t number_of_pages, plane, page_offset;
  uint32_t offs = 0;
  uint8_t *buf = target_options.file_data;
  uint32_t size = target_options.file_size;

  number_of_pages = (size + target_device.page_size - 1) / target_device.page_size;
  page_offset = target_options.offset / target_device.page_size;

  for (uint32_t page = 0; page < number_of_pages; page += PAGES_IN_ERASE_BLOCK)
  {
    plane = (page + page_offset) / (target_device.flash_size / target_device.page_size);

    dap_write_word(EEFC_FCR(plane), CMD_EPA | (((page_offset + page) | 2) << 8));
    while (0 == (dap_read_word(EEFC_FSR(plane)) & FSR_FRDY));

    verbose(".");
  }

  verbose(",");

  for (uint32_t page = 0; page < number_of_pages; page++)
  {
    dap_write_block(addr, &buf[offs], target_device.page_size);
    addr += target_device.page_size;
    offs += target_device.page_size;

    plane = (page + page_offset) / (target_device.flash_size / target_device.page_size);

    dap_write_word(EEFC_FCR(plane), CMD_WP | ((page + page_offset) << 8));
    while (0 == (dap_read_word(EEFC_FSR(plane)) & FSR_FRDY));

    verbose(".");
  }

  // Set boot mode GPNVM bit
  dap_write_word(EEFC_FCR(0), CMD_SGPB | (1 << 8));
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

  bufb = buf_alloc(target_device.page_size);

  while (size)
  {
    dap_read_block(addr, bufb, target_device.page_size);

    block_size = (size > target_device.page_size) ? target_device.page_size : size;

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

    addr += target_device.page_size;
    offs += target_device.page_size;
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

  while (size)
  {
    dap_read_block(addr, &buf[offs], target_device.page_size);

    addr += target_device.page_size;
    offs += target_device.page_size;
    size -= target_device.page_size;

    verbose(".");
  }

  save_file(target_options.name, buf, target_options.size);
}

//-----------------------------------------------------------------------------
target_ops_t target_atmel_cm4_ops = 
{
  .select   = target_select,
  .deselect = target_deselect,
  .erase    = target_erase,
  .lock     = target_lock,
  .program  = target_program,
  .verify   = target_verify,
  .read     = target_read,
};

