/*
 * Copyright (c) 2018, Alex Taradov <alex@taradov.com>
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
#define FLASH_ADDR             0
#define FLASH_ROW_SIZE         8192
#define FLASH_PAGE_SIZE        512
#define PAGES_IN_ERASE_BLOCK   (FLASH_ROW_SIZE / FLASH_PAGE_SIZE)

#define USER_ROW_ADDR          0x00804000
#define USER_ROW_SIZE          512
#define USER_ROW_PAGE_SIZE     16

#define DHCSR                  0xe000edf0
#define DHCSR_DEBUGEN          (1 << 0)
#define DHCSR_HALT             (1 << 1)
#define DHCSR_DBGKEY           (0xa05f << 16)

#define DEMCR                  0xe000edfc
#define DEMCR_VC_CORERESET     (1 << 0)

#define AIRCR                  0xe000ed0c
#define AIRCR_VECTKEY          (0x05fa << 16)
#define AIRCR_SYSRESETREQ      (1 << 2)

#define DSU_CTRL               0x41002100
#define DSU_STATUSA            0x41002101
#define DSU_STATUSB            0x41002102
#define DSU_DID                0x41002118

#define DSU_CTRL_CE            (1 << 4)
#define DSU_STATUSA_CRSTEXT    (1 << 1)

#define DSU_STATUSA_DONE       (1 << 0)
#define DSU_STATUSB_PROT       (1 << 0)

#define NVMCTRL_CTRLA          0x41004000
#define NVMCTRL_CTRLB          0x41004004
#define NVMCTRL_PARAM          0x41004008
#define NVMCTRL_INTFLAG        0x41004010
#define NVMCTRL_STATUS         0x41004012
#define NVMCTRL_ADDR           0x41004014

#define NVMCTRL_STATUS_READY   (1 << 0)

#define NVMCTRL_CTRLA_AUTOWS     (1 << 2)
#define NVMCTRL_CTRLA_WMODE_MAN  (0 << 4)
#define NVMCTRL_CTRLA_PRM_MANUAL (3 << 6)
#define NVMCTRL_CTRLA_CACHEDIS0  (1 << 14)
#define NVMCTRL_CTRLA_CACHEDIS1  (1 << 15)

#define NVMCTRL_CMD_EP         0xa500
#define NVMCTRL_CMD_EB         0xa501
#define NVMCTRL_CMD_WP         0xa503
#define NVMCTRL_CMD_WQW        0xa504
#define NVMCTRL_CMD_UR         0xa512
#define NVMCTRL_CMD_PBC        0xa515
#define NVMCTRL_CMD_SSB        0xa516

#define DEVICE_ID_MASK         0xfffff0ff
#define DEVICE_REV_SHIFT       8
#define DEVICE_REV_MASK        0xf

/*- Types -------------------------------------------------------------------*/
typedef struct
{
  uint32_t  dsu_did;
  char      *name;
  uint32_t  flash_size;
} device_t;

/*- Variables ---------------------------------------------------------------*/
static device_t devices[] =
{
  { 0x61810003, "SAM E51J18A",        256*1024 },
  { 0x61810001, "SAM E51N19A",        512*1024 },
  { 0x61810002, "SAM E51J19A",        512*1024 },
  { 0x61810000, "SAM E51N20A",     1*1024*1024 },
  { 0x61810004, "SAM E51J20A",     1*1024*1024 },
  { 0x60060006, "SAM D51J18A",        256*1024 },
  { 0x60060008, "SAM D51G18A",        256*1024 },
  { 0x60060001, "SAM D51P19A",        512*1024 },
  { 0x60060003, "SAM D51N19A",        512*1024 },
  { 0x60060005, "SAM D51J19A",        512*1024 },
  { 0x60060007, "SAM D51G19A",        512*1024 },
  { 0x60060000, "SAM D51P20A",     1*1024*1024 },
  { 0x60060002, "SAM D51N20A",     1*1024*1024 },
  { 0x60060004, "SAM D51J20A",     1*1024*1024 },
  { 0x61830006, "SAM E53J18A",        256*1024 },
  { 0x61830003, "SAM E53N19A",        512*1024 },
  { 0x61830005, "SAM E53J19A",        512*1024 },
  { 0x61830002, "SAM E53N20A",     1*1024*1024 },
  { 0x61830004, "SAM E53J20A",     1*1024*1024 },
  { 0x61840001, "SAM E54P19A",        512*1024 },
  { 0x61840003, "SAM E54N19A",        512*1024 },
  { 0x61840000, "SAM E54P20A",     1*1024*1024 },
  { 0x61840002, "SAM E54N20A",     1*1024*1024 },
  { 0 },
};

static device_t target_device;
static target_options_t target_options;

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
static void reset_with_extension(void)
{
  dap_reset_target_hw(0);
  sleep_ms(10);
  reconnect_debugger();
}

//-----------------------------------------------------------------------------
static void finish_reset(void)
{
  // Stop the core
  dap_write_word(DHCSR, DHCSR_DBGKEY | DHCSR_DEBUGEN | DHCSR_HALT);
  dap_write_word(DEMCR, DEMCR_VC_CORERESET);
  dap_write_word(AIRCR, AIRCR_VECTKEY | AIRCR_SYSRESETREQ);

  // Release the reset
  dap_write_byte(DSU_STATUSA, DSU_STATUSA_CRSTEXT);
}

//-----------------------------------------------------------------------------
static void target_select(target_options_t *options)
{
  uint32_t dsu_did, id, rev;
  bool locked;

  reset_with_extension();

  dsu_did = dap_read_word(DSU_DID);
  id = dsu_did & DEVICE_ID_MASK;
  rev = (dsu_did >> DEVICE_REV_SHIFT) & DEVICE_REV_MASK;

  for (device_t *device = devices; device->dsu_did > 0; device++)
  {
    if (device->dsu_did == id)
    {
      verbose("Target: %s (Rev %c)\n", device->name, 'A' + rev);

      target_device = *device;
      target_options = *options;

      target_check_options(&target_options, device->flash_size,
          FLASH_ROW_SIZE, USER_ROW_SIZE);

      locked = dap_read_byte(DSU_STATUSB) & DSU_STATUSB_PROT;

      if (locked && !options->erase)
        error_exit("target is locked, erase is necessary");

      if (!locked)
        finish_reset();

      return;
    }
  }

  error_exit("unknown target device (DSU_DID = 0x%08x)", dsu_did);
}

//-----------------------------------------------------------------------------
static void target_deselect(void)
{
  dap_write_word(DEMCR, 0);
  dap_write_word(AIRCR, AIRCR_VECTKEY | AIRCR_SYSRESETREQ);
  target_free_options(&target_options);
}

//-----------------------------------------------------------------------------
static void target_erase(void)
{
  dap_write_byte(DSU_CTRL, DSU_CTRL_CE); // Chip erase
  sleep_ms(100);
  while (0 == (dap_read_byte(DSU_STATUSA) & DSU_STATUSA_DONE));

  reset_with_extension();
  finish_reset();
}

//-----------------------------------------------------------------------------
static void target_lock(void)
{
  dap_write_half(NVMCTRL_CTRLB, NVMCTRL_CMD_SSB); // Set Security Bit
}

//-----------------------------------------------------------------------------
static void target_program(void)
{
  uint32_t addr = FLASH_ADDR + target_options.offset;
  uint32_t offs = 0;
  uint32_t number_of_rows;
  uint8_t *buf = target_options.file_data;
  uint32_t size = target_options.file_size;

  dap_write_half(NVMCTRL_CTRLA, NVMCTRL_CTRLA_AUTOWS | NVMCTRL_CTRLA_WMODE_MAN |
      NVMCTRL_CTRLA_PRM_MANUAL | NVMCTRL_CTRLA_CACHEDIS0 | NVMCTRL_CTRLA_CACHEDIS1);

  number_of_rows = (size + FLASH_ROW_SIZE - 1) / FLASH_ROW_SIZE;

  for (uint32_t row = 0; row < number_of_rows; row++)
  {
    dap_write_word(NVMCTRL_ADDR, addr);

    dap_write_half(NVMCTRL_CTRLB, NVMCTRL_CMD_UR); // Unlock Region
    while (0 == (dap_read_half(NVMCTRL_STATUS) & NVMCTRL_STATUS_READY));

    dap_write_half(NVMCTRL_CTRLB, NVMCTRL_CMD_EB);
    while (0 == (dap_read_half(NVMCTRL_STATUS) & NVMCTRL_STATUS_READY));

    for (int page = 0; page < PAGES_IN_ERASE_BLOCK; page++)
    {
      dap_write_word(NVMCTRL_ADDR, addr);

      dap_write_half(NVMCTRL_CTRLB, NVMCTRL_CMD_PBC);
      while (0 == (dap_read_half(NVMCTRL_STATUS) & NVMCTRL_STATUS_READY));

      dap_write_block(addr, &buf[offs], FLASH_PAGE_SIZE);

      dap_write_half(NVMCTRL_CTRLB, NVMCTRL_CMD_WP); // Write page
      while (0 == (dap_read_half(NVMCTRL_STATUS) & NVMCTRL_STATUS_READY));

      addr += FLASH_PAGE_SIZE;
      offs += FLASH_PAGE_SIZE;
    }

    verbose(".");
  }
}

//-----------------------------------------------------------------------------
static void target_verify(void)
{
  uint32_t addr = FLASH_ADDR + target_options.offset;
  uint32_t block_size;
  uint32_t offs = 0;
  uint8_t *bufb;
  uint8_t *bufa = target_options.file_data;
  uint32_t size = target_options.file_size;

  bufb = buf_alloc(FLASH_PAGE_SIZE);

  while (size)
  {
    dap_read_block(addr, bufb, FLASH_PAGE_SIZE);

    block_size = (size > FLASH_PAGE_SIZE) ? FLASH_PAGE_SIZE : size;

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

    addr += FLASH_PAGE_SIZE;
    offs += FLASH_PAGE_SIZE;
    size -= block_size;

    verbose(".");
  }

  buf_free(bufb);
}

//-----------------------------------------------------------------------------
static void target_read(void)
{
  uint32_t addr = FLASH_ADDR + target_options.offset;
  uint32_t offs = 0;
  uint8_t *buf = target_options.file_data;
  uint32_t size = target_options.size;

  while (size)
  {
    dap_read_block(addr, &buf[offs], FLASH_PAGE_SIZE);

    addr += FLASH_PAGE_SIZE;
    offs += FLASH_PAGE_SIZE;
    size -= FLASH_PAGE_SIZE;

    verbose(".");
  }

  save_file(target_options.name, buf, target_options.size);
}

//-----------------------------------------------------------------------------
static void target_fuse(void)
{
  uint8_t buf[USER_ROW_SIZE];
  bool read_all = (-1 == target_options.fuse_start);
  int size = (target_options.fuse_size < USER_ROW_SIZE) ?
      target_options.fuse_size : USER_ROW_SIZE;
  uint32_t addr, offs;

  check(0 == target_options.fuse_section, "unsupported fuse section %d",
      target_options.fuse_section);

  dap_read_block(USER_ROW_ADDR, buf, USER_ROW_SIZE);

  if (target_options.fuse_read)
  {
    if (target_options.fuse_name)
    {
      save_file(target_options.fuse_name, buf, USER_ROW_SIZE);
    }
    else if (read_all)
    {
      message("Fuses (user row): ");

      for (int i = 0; i < USER_ROW_SIZE; i++)
        message("%02x ", buf[i]);

      message("\n");
    }
    else
    {
      uint32_t value = extract_value(buf, target_options.fuse_start,
          target_options.fuse_end);

      message("Fuses: 0x%x (%d)\n", value, value);
    }
  }

  if (target_options.fuse_write)
  {
    if (target_options.fuse_name)
    {
      for (int i = 0; i < size; i++)
        buf[i] = target_options.fuse_data[i];
    }
    else
    {
      apply_value(buf, target_options.fuse_value, target_options.fuse_start,
          target_options.fuse_end);
    }

    dap_write_word(NVMCTRL_ADDR, USER_ROW_ADDR);

    dap_write_half(NVMCTRL_CTRLB, NVMCTRL_CMD_EP);
    while (0 == (dap_read_half(NVMCTRL_STATUS) & NVMCTRL_STATUS_READY));

    dap_write_half(NVMCTRL_CTRLB, NVMCTRL_CMD_PBC);
    while (0 == (dap_read_half(NVMCTRL_STATUS) & NVMCTRL_STATUS_READY));

    addr = USER_ROW_ADDR;
    offs = 0;

    for (int i = 0; i < (USER_ROW_SIZE / USER_ROW_PAGE_SIZE); i++)
    {
      dap_write_word(NVMCTRL_ADDR, USER_ROW_ADDR);

      dap_write_block(addr, &buf[offs], USER_ROW_PAGE_SIZE);

      dap_write_half(NVMCTRL_CTRLB, NVMCTRL_CMD_WQW);
      while (0 == (dap_read_half(NVMCTRL_STATUS) & NVMCTRL_STATUS_READY));

      addr += USER_ROW_PAGE_SIZE;
      offs += USER_ROW_PAGE_SIZE;
    }
  }

  if (target_options.fuse_verify)
  {
    dap_read_block(USER_ROW_ADDR, buf, USER_ROW_SIZE);

    if (target_options.fuse_name)
    {
      for (int i = 0; i < size; i++)
      {
        if (target_options.fuse_data[i] != buf[i])
        {
          message("fuse byte %d expected 0x%02x, got 0x%02x", i,
              target_options.fuse_data[i], buf[i]);
          error_exit("fuse verification failed");
        }
      }
    }
    else if (read_all)
    {
      error_exit("please specify fuse bit range for verification");
    }
    else
    {
      uint32_t value = extract_value(buf, target_options.fuse_start,
          target_options.fuse_end);

      if (target_options.fuse_value != value)
      {
        error_exit("fuse verification failed: expected 0x%x (%u), got 0x%x (%u)",
            target_options.fuse_value, target_options.fuse_value, value, value);
      }
    }
  }
}

//-----------------------------------------------------------------------------
target_ops_t target_atmel_cm4v2_ops = 
{
  .select   = target_select,
  .deselect = target_deselect,
  .erase    = target_erase,
  .lock     = target_lock,
  .program  = target_program,
  .verify   = target_verify,
  .read     = target_read,
  .fuse     = target_fuse,
};

