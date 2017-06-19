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
#include <stdio.h>
#include "target.h"
#include "edbg.h"
#include "dap.h"

/*- Definitions -------------------------------------------------------------*/
#define DHCSR                  0xe000edf0
#define DEMCR                  0xe000edfc
#define AIRCR                  0xe000ed0c

#define CHIPID_CIDR            0x400e0940
#define CHIPID_EXID            0x400e0944

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
#define CMD_CGPB               0x5a00000c
#define CMD_GGPB               0x5a00000d

#define PAGES_IN_ERASE_BLOCK   16

#define GPNVM_SECURITY_BIT     0
#define GPNVM_BMS_BIT          1
#define GPNVM_USER_BIT_0       2
#define GPNVM_USER_BIT_1       3
#define GPNVM_USER_BIT_2       4
#define GPNVM_USER_BIT_3       5
#define GPNVM_RESERVED_BIT     6
#define GPNVM_ITCM_BIT         7
#define GPNVM_DTCM_BIT         8

/*- Types -------------------------------------------------------------------*/
typedef struct
{
  uint32_t  chip_id;
  uint32_t  chip_exid;
  char      *name;
  uint32_t  flash_start;
  uint32_t  flash_size;
  uint32_t  page_size;
} device_t;


/*- Variables ---------------------------------------------------------------*/
static device_t devices[] =
{
  { 0xa1020e00, 0x00000002, "SAM E70Q21", 0x00400000, 2*1024*1024, 512 },
  { 0xa1020c00, 0x00000002, "SAM E70Q20", 0x00400000,   1024*1024, 512 },
  { 0xa10d0a00, 0x00000002, "SAM E70Q19", 0x00400000,    512*1024, 512 },
  { 0xa1020e00, 0x00000001, "SAM E70N21", 0x00400000, 2*1024*1024, 512 },
  { 0xa1020c00, 0x00000001, "SAM E70N20", 0x00400000,   1024*1024, 512 },
  { 0xa10d0a00, 0x00000001, "SAM E70N19", 0x00400000,    512*1024, 512 },
  { 0xa1020e00, 0x00000000, "SAM E70J21", 0x00400000, 2*1024*1024, 512 },
  { 0xa1020c00, 0x00000000, "SAM E70J20", 0x00400000,   1024*1024, 512 },
  { 0xa10d0a00, 0x00000000, "SAM E70J19", 0x00400000,    512*1024, 512 },
  { 0xa1120e00, 0x00000002, "SAM S70Q21", 0x00400000, 2*1024*1024, 512 },
  { 0xa1120c00, 0x00000002, "SAM S70Q20", 0x00400000,   1024*1024, 512 },
  { 0xa11d0a00, 0x00000002, "SAM S70Q19", 0x00400000,    512*1024, 512 },
  { 0xa1120e00, 0x00000001, "SAM S70N21", 0x00400000, 2*1024*1024, 512 },
  { 0xa1120c00, 0x00000001, "SAM S70N20", 0x00400000,   1024*1024, 512 },
  { 0xa11d0a00, 0x00000001, "SAM S70N19", 0x00400000,    512*1024, 512 },
  { 0xa1120e00, 0x00000000, "SAM S70J21", 0x00400000, 2*1024*1024, 512 },
  { 0xa1120c00, 0x00000000, "SAM S70J20", 0x00400000,   1024*1024, 512 },
  { 0xa11d0a00, 0x00000000, "SAM S70J19", 0x00400000,    512*1024, 512 },
  { 0xa1220e00, 0x00000002, "SAM V71Q21", 0x00400000, 2*1024*1024, 512 },
  { 0xa1220e01, 0x00000002, "SAM V71Q21 (Rev B)", 0x00400000, 2*1024*1024, 512 },
  { 0xa1220c00, 0x00000002, "SAM V71Q20", 0x00400000,   1024*1024, 512 },
  { 0xa12d0a00, 0x00000002, "SAM V71Q19", 0x00400000,    512*1024, 512 },
  { 0xa1220e00, 0x00000001, "SAM V71N21", 0x00400000, 2*1024*1024, 512 },
  { 0xa1220c00, 0x00000001, "SAM V71N20", 0x00400000,   1024*1024, 512 },
  { 0xa12d0a00, 0x00000001, "SAM V71N19", 0x00400000,    512*1024, 512 },
  { 0xa1220e00, 0x00000000, "SAM V71J21", 0x00400000, 2*1024*1024, 512 },
  { 0xa1220c00, 0x00000000, "SAM V71J20", 0x00400000,   1024*1024, 512 },
  { 0xa12d0a00, 0x00000000, "SAM V71J19", 0x00400000,    512*1024, 512 },
  { 0xa1320c00, 0x00000002, "SAM V70Q20", 0x00400000,   1024*1024, 512 },
  { 0xa13d0a00, 0x00000002, "SAM V70Q19", 0x00400000,    512*1024, 512 },
  { 0xa1320c00, 0x00000001, "SAM V70N20", 0x00400000,   1024*1024, 512 },
  { 0xa13d0a00, 0x00000001, "SAM V70N19", 0x00400000,    512*1024, 512 },
  { 0xa1320c00, 0x00000000, "SAM V70J20", 0x00400000,   1024*1024, 512 },
  { 0xa13d0a00, 0x00000000, "SAM V70J19", 0x00400000,    512*1024, 512 },
  { 0, 0, "", 0, 0, 0 },
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

      target_device = *device;
      target_options = *options;

      target_check_options(&target_options, device->flash_size,
          device->page_size * PAGES_IN_ERASE_BLOCK);

      return;
    }
  }

  error_exit("unknown target device (CHIP_ID = 0x%08x)", chip_id);
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
  dap_write_word(EEFC_FCR, CMD_EA);
  while (0 == (dap_read_word(EEFC_FSR) & FSR_FRDY));
}

//-----------------------------------------------------------------------------
static void target_lock(void)
{
  dap_write_word(EEFC_FCR, CMD_SGPB | (0 << 8));
}

//-----------------------------------------------------------------------------
static uint32_t target_fuse_read(void)
{
  uint32_t gpnvm = 0;

  dap_write_word(EEFC_FCR, CMD_GGPB);
  while (0 == (dap_read_word(EEFC_FSR) & FSR_FRDY));
  gpnvm = dap_read_word(EEFC_FRR);

  return gpnvm;
}

//-----------------------------------------------------------------------------
static void target_fuse_write(uint32_t mask)
{
  if (mask & (1<<GPNVM_SECURITY_BIT)) {
    while (0 == (dap_read_word(EEFC_FSR) & FSR_FRDY));
    dap_write_word(EEFC_FCR, CMD_SGPB | (GPNVM_SECURITY_BIT << 8));
  } else {
    // we cannot clear the security bit so do nothing.
  }

  if (mask & (1<<GPNVM_BMS_BIT)) {
    while (0 == (dap_read_word(EEFC_FSR) & FSR_FRDY));
    dap_write_word(EEFC_FCR, CMD_SGPB | (GPNVM_BMS_BIT << 8));
  } else {
    dap_write_word(EEFC_FCR, CMD_CGPB | (GPNVM_BMS_BIT << 8));
  }

  if (mask & (1<<GPNVM_USER_BIT_0)) {
    while (0 == (dap_read_word(EEFC_FSR) & FSR_FRDY));
    dap_write_word(EEFC_FCR, CMD_SGPB | (GPNVM_USER_BIT_0 << 8));
  } else {
    dap_write_word(EEFC_FCR, CMD_CGPB | (GPNVM_USER_BIT_0 << 8));
  }

  if (mask & (1<<GPNVM_USER_BIT_1)) {
    while (0 == (dap_read_word(EEFC_FSR) & FSR_FRDY));
    dap_write_word(EEFC_FCR, CMD_SGPB | (GPNVM_USER_BIT_1 << 8));
  } else {
    dap_write_word(EEFC_FCR, CMD_CGPB | (GPNVM_USER_BIT_1 << 8));
  }

  if (mask & (1<<GPNVM_USER_BIT_2)) {
    while (0 == (dap_read_word(EEFC_FSR) & FSR_FRDY));
    dap_write_word(EEFC_FCR, CMD_SGPB | (GPNVM_USER_BIT_2 << 8));
  } else {
    dap_write_word(EEFC_FCR, CMD_CGPB | (GPNVM_USER_BIT_2 << 8));
  }

  if (mask & (1<<GPNVM_USER_BIT_3)) {
    while (0 == (dap_read_word(EEFC_FSR) & FSR_FRDY));
    dap_write_word(EEFC_FCR, CMD_SGPB | (GPNVM_USER_BIT_3 << 8));
  } else {
    dap_write_word(EEFC_FCR, CMD_CGPB | (GPNVM_USER_BIT_3 << 8));
  }

  if (mask & (1<<GPNVM_ITCM_BIT)) {
    while (0 == (dap_read_word(EEFC_FSR) & FSR_FRDY));
    dap_write_word(EEFC_FCR, CMD_SGPB | (GPNVM_ITCM_BIT << 8));
  } else {
    dap_write_word(EEFC_FCR, CMD_CGPB | (GPNVM_ITCM_BIT << 8));
  }

  if (mask & (1<<GPNVM_DTCM_BIT)) {
    while (0 == (dap_read_word(EEFC_FSR) & FSR_FRDY));
    dap_write_word(EEFC_FCR, CMD_SGPB | (GPNVM_DTCM_BIT << 8));
  } else {
    dap_write_word(EEFC_FCR, CMD_CGPB | (GPNVM_DTCM_BIT << 8));
  }
}

//-----------------------------------------------------------------------------
static void target_program(void)
{
  uint32_t addr = target_device.flash_start + target_options.offset;
  uint32_t number_of_pages, page_offset;
  uint32_t offs = 0;
  uint8_t *buf = target_options.file_data;
  uint32_t size = target_options.file_size;
  uint32_t fuse_mask = target_options.offset;
  uint32_t gpnvm = 0;

  if (target_options.fuse == true) {
      gpnvm = target_fuse_read();
      printf("Old GPNVM row:  0x%X\r\n", gpnvm);
      target_fuse_write(fuse_mask);
      gpnvm = target_fuse_read();
      printf("New GPNVM row:  0x%X\r\n", gpnvm);
      return;
  }

  number_of_pages = (size + target_device.page_size - 1) / target_device.page_size;
  page_offset = target_options.offset / target_device.page_size;

  for (uint32_t page = 0; page < number_of_pages; page += PAGES_IN_ERASE_BLOCK)
  {
    dap_write_word(EEFC_FCR, CMD_EPA | (((page_offset + page) | 2) << 8));
    while (0 == (dap_read_word(EEFC_FSR) & FSR_FRDY));

    verbose(".");
  }

  verbose(",");

  for (uint32_t page = 0; page < number_of_pages; page++)
  {
    dap_write_block(addr, &buf[offs], target_device.page_size);
    addr += target_device.page_size;
    offs += target_device.page_size;

    dap_write_word(EEFC_FCR, CMD_WP | ((page + page_offset) << 8));
    while (0 == (dap_read_word(EEFC_FSR) & FSR_FRDY));

    verbose(".");
  }

  // Set boot mode GPNVM bit
  dap_write_word(EEFC_FCR, CMD_SGPB | (1 << 8));
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
  uint32_t gpnvm = 0;

  if (target_options.fuse == true) {
    gpnvm = target_fuse_read();
    printf("GPNVM row:  0x%X\r\n", gpnvm);
    return;
  }

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
target_ops_t target_atmel_cm7_ops = 
{
  .select   = target_select,
  .deselect = target_deselect,
  .erase    = target_erase,
  .lock     = target_lock,
  .program  = target_program,
  .verify   = target_verify,
  .read     = target_read,
};

