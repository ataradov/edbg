/*
 * Copyright (c) 2018-2019, Alex Taradov <alex@taradov.com>
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
#define FLASH_ADDR             0
#define FLASH_ROW_SIZE         256
#define FLASH_PAGE_SIZE        64

#define USER_ROW_ADDR          0x00804000
#define BOCOR_ROW_ADDR         0x0080c000

#define DSU_CTRL               0x41002100
#define DSU_STATUSA            0x41002101
#define DSU_STATUSB            0x41002102

#define DSU_DID                0x41002118
#define DSU_BCC0               0x41002120
#define DSU_BCC1               0x41002124

#define DSU_STATUSA_CRSTEXT    (1 << 1)
#define DSU_STATUSA_BREXT      (1 << 5)

#define DSU_STATUSB_BCCD0      (1 << 6)
#define DSU_STATUSB_BCCD1      (1 << 7)

#define NVMCTRL_NSEC_CTRLA     0x41004000
#define NVMCTRL_NSEC_CTRLB     0x41004004
#define NVMCTRL_NSEC_CTRLC     0x41004008
#define NVMCTRL_NSEC_STATUS    0x41004018
#define NVMCTRL_NSEC_ADDR      0x4100401c

#define NVMCTRL_SEC_OFFSET     0x1000

#define NVMCTRL_STATUS_READY   (1 << 2)
#define NVMCTRL_STATUS_DAL0    (1 << 3)
#define NVMCTRL_STATUS_DAL1    (1 << 4)

#define NVMCTRL_CMD_ER         0xa502
#define NVMCTRL_CMD_WP         0xa504
#define NVMCTRL_CMD_SDAL0      0xa54b

#define DEVICE_ID_MASK         0xfffff0ff
#define DEVICE_REV_SHIFT       8
#define DEVICE_REV_MASK        0xf

#define CMD_PREFIX             0x44424700
#define SIG_PREFIX             0xec000000

enum
{
  CMD_INIT      = 0x55,
  CMD_EXIT      = 0xaa,
  CMD_RESET     = 0x52,
  CMD_CE0       = 0xe0,
  CMD_CE1       = 0xe1,
  CMD_CE2       = 0xe2,
  CMD_CHIPERASE = 0xe3,
  CMD_CRC       = 0xc0,
  CMD_DCEK      = 0x44,
  CMD_RAUX      = 0x4c,
};

enum
{
  SIG_NO          = 0x00,
  SIG_COMM        = 0x20,
  SIG_CMD_SUCCESS = 0x21,
  SIG_CMD_VALID   = 0x24,
  SIG_BOOTOK      = 0x39,
  SIG_BOOT_ERR    = 0x41,
};

/*- Types -------------------------------------------------------------------*/
typedef struct
{
  uint32_t  CTRLA;
  uint32_t  CTRLB;
  uint32_t  CTRLC;
  uint32_t  STATUS;
  uint32_t  ADDR;
} nvmctrl_t;

typedef struct
{
  uint32_t  dsu_did;
  char      *family;
  char      *name;
  uint32_t  flash_size;
  bool      trust_zone;
} device_t;

/*- Variables ---------------------------------------------------------------*/
static device_t devices[] =
{
  { 0x20840003, "saml10", "SAM L10D16A",  64*1024, false },
  { 0x20840000, "saml10", "SAM L10E16A",  64*1024, false },
  { 0x20830003, "saml11", "SAM L11D16A",  64*1024, true  },
  { 0x20830000, "saml11", "SAM L11E16A",  64*1024, true  },
};

static device_t target_device;
static target_options_t target_options;

static uint32_t NVMCTRL_CTRLA;
static uint32_t NVMCTRL_CTRLB;
static uint32_t NVMCTRL_CTRLC;
static uint32_t NVMCTRL_STATUS;
static uint32_t NVMCTRL_ADDR;

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
static void reset_with_extension(void)
{
  dap_reset_target_hw(0);
  sleep_ms(10);
  dap_reset_link();
  dap_write_byte(DSU_STATUSA, DSU_STATUSA_CRSTEXT);
}

//-----------------------------------------------------------------------------
static void bootrom_data(uint32_t data)
{
  dap_write_word(DSU_BCC0, data);
  while (dap_read_byte(DSU_STATUSB) & DSU_STATUSB_BCCD0);
}

//-----------------------------------------------------------------------------
static void bootrom_command(int cmd)
{
  dap_write_word(DSU_BCC0, CMD_PREFIX | cmd);
  while (dap_read_byte(DSU_STATUSB) & DSU_STATUSB_BCCD0);
}

//-----------------------------------------------------------------------------
static int bootrom_expect(int status)
{
  uint32_t v;
  int i, res;

  for (i = 1000; i > 0; i--)
  {
    if (dap_read_byte(DSU_STATUSB) & DSU_STATUSB_BCCD1)
      break;
  }

  if (0 == i)
    error_exit("no BootROM response");

  v = dap_read_word(DSU_BCC1);

  if (SIG_PREFIX != (v & 0xffffff00))
    error_exit("invalid BootROM response prefix 0x%08x", v);

  res = v & 0xff;

  if (status != -1 && status != res)
    error_exit("invalid BootROM response 0x%02x, expected 0x%02x", res, status);

  return res;
}

//-----------------------------------------------------------------------------
static void bootrom_park(void)
{
  static bool in_park_mode = false;
  int response;

  if (!in_park_mode)
  {
    reset_with_extension();

    bootrom_command(CMD_EXIT);
    response = bootrom_expect(-1);

    if (SIG_BOOTOK != response)
    {
      error_exit("invalid BootROM response 0x%02x, expected 0x%02x. Check that device is not locked.",
          response, SIG_BOOTOK);
    }

    in_park_mode = true;
  }
}

//-----------------------------------------------------------------------------
static void target_select(target_options_t *options)
{
  uint32_t dsu_did, id, rev;

  reset_with_extension();

  dsu_did = dap_read_word(DSU_DID);
  id = dsu_did & DEVICE_ID_MASK;
  rev = (dsu_did >> DEVICE_REV_SHIFT) & DEVICE_REV_MASK;

  for (int i = 0; i < ARRAY_SIZE(devices); i++)
  {
    if (devices[i].dsu_did != id)
      continue;

    verbose("Target: %s (Rev %c)\n", devices[i].name, 'A' + rev);

    target_device = devices[i];
    target_options = *options;

    if (target_device.trust_zone)
    {
      NVMCTRL_CTRLA  = NVMCTRL_NSEC_CTRLA + NVMCTRL_SEC_OFFSET;
      NVMCTRL_CTRLB  = NVMCTRL_NSEC_CTRLB + NVMCTRL_SEC_OFFSET;
      NVMCTRL_CTRLC  = NVMCTRL_NSEC_CTRLC + NVMCTRL_SEC_OFFSET;
      NVMCTRL_STATUS = NVMCTRL_NSEC_STATUS + NVMCTRL_SEC_OFFSET;
      NVMCTRL_ADDR   = NVMCTRL_NSEC_ADDR + NVMCTRL_SEC_OFFSET;
    }
    else
    {
      NVMCTRL_CTRLA  = NVMCTRL_NSEC_CTRLA;
      NVMCTRL_CTRLB  = NVMCTRL_NSEC_CTRLB;
      NVMCTRL_CTRLC  = NVMCTRL_NSEC_CTRLC;
      NVMCTRL_STATUS = NVMCTRL_NSEC_STATUS;
      NVMCTRL_ADDR   = NVMCTRL_NSEC_ADDR;
    }

    target_check_options(&target_options, target_device.flash_size, FLASH_ROW_SIZE);

    return;
  }

  error_exit("unknown target device (DSU_DID = 0x%08x)", dsu_did);
}

//-----------------------------------------------------------------------------
static void target_deselect(void)
{
  target_free_options(&target_options);
}

//-----------------------------------------------------------------------------
static void target_erase(void)
{
  reset_with_extension();

  sleep_ms(10);

  if (dap_read_byte(DSU_STATUSB) & DSU_STATUSB_BCCD1)
  {
    uint32_t status = dap_read_word(DSU_BCC1);
    error_exit("BootROM indicated an error (STATUS = 0x%08x)", status);
  }
  else
  {
    bootrom_command(CMD_INIT);
    bootrom_expect(SIG_COMM);
  }

  if (target_device.trust_zone)
  {
    bootrom_command(CMD_CE2);
    bootrom_expect(SIG_CMD_VALID);

    // TODO: Take this value from a command line argument
    bootrom_data(0xffffffff);
    bootrom_data(0xffffffff);
    bootrom_data(0xffffffff);
    bootrom_data(0xffffffff);
  }
  else
  {
    bootrom_command(CMD_CHIPERASE);
    bootrom_expect(SIG_CMD_VALID);
  }

  bootrom_expect(SIG_CMD_SUCCESS);
}

//-----------------------------------------------------------------------------
static void target_lock(void)
{
  bootrom_park();

  dap_write_half(NVMCTRL_CTRLA, NVMCTRL_CMD_SDAL0);
  while (0 == (dap_read_byte(NVMCTRL_STATUS) & NVMCTRL_STATUS_READY));
}

//-----------------------------------------------------------------------------
static void target_program(void)
{
  uint32_t addr = FLASH_ADDR + target_options.offset;
  uint32_t offs = 0;
  uint32_t number_of_rows;
  uint8_t *buf = target_options.file_data;
  uint32_t size = target_options.file_size;

  bootrom_park();

  if ((dap_read_byte(DSU_STATUSB) & 0x03) != 0x02)
    error_exit("device is locked (DAL is not 2), perform a chip erase before programming");

  number_of_rows = (size + FLASH_ROW_SIZE - 1) / FLASH_ROW_SIZE;

  dap_write_byte(NVMCTRL_CTRLC, 0); // Enable automatic write

  for (uint32_t row = 0; row < number_of_rows; row++)
  {
    dap_write_word(NVMCTRL_ADDR, addr);

    dap_write_half(NVMCTRL_CTRLA, NVMCTRL_CMD_ER);
    while (0 == (dap_read_byte(NVMCTRL_STATUS) & NVMCTRL_STATUS_READY));

    dap_write_block(addr, &buf[offs], FLASH_ROW_SIZE);

    addr += FLASH_ROW_SIZE;
    offs += FLASH_ROW_SIZE;

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

  bootrom_park();

  if ((dap_read_byte(DSU_STATUSB) & 0x03) != 0x02)
    error_exit("device is locked (DAL is not 2), unable to verify");

  bufb = buf_alloc(FLASH_ROW_SIZE);

  while (size)
  {
    dap_read_block(addr, bufb, FLASH_ROW_SIZE);

    block_size = (size > FLASH_ROW_SIZE) ? FLASH_ROW_SIZE : size;

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

    addr += FLASH_ROW_SIZE;
    offs += FLASH_ROW_SIZE;
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

  bootrom_park();

  if ((dap_read_byte(DSU_STATUSB) & 0x03) != 0x02)
    error_exit("device is locked (DAL is not 2), unable to read");

  while (size)
  {
    dap_read_block(addr, &buf[offs], FLASH_ROW_SIZE);

    addr += FLASH_ROW_SIZE;
    offs += FLASH_ROW_SIZE;
    size -= FLASH_ROW_SIZE;

    verbose(".");
  }

  save_file(target_options.name, buf, target_options.size);
}

//-----------------------------------------------------------------------------
static int target_fuse_read(int section, uint8_t *data)
{
  uint32_t addr = 0;

  if (0 == section)
    addr = USER_ROW_ADDR;
  else if (1 == section)
    addr = BOCOR_ROW_ADDR;
  else
    return 0;

  bootrom_park();

  dap_read_block(addr, data, FLASH_ROW_SIZE);

  return FLASH_ROW_SIZE;
}

//-----------------------------------------------------------------------------
static void target_fuse_write(int section, uint8_t *data)
{
  uint32_t addr = 0;

  check(section < 2, "internal: incorrect section index in target_fuse_write()");

  if (0 == section)
    addr = USER_ROW_ADDR;
  else
    addr = BOCOR_ROW_ADDR;

  bootrom_park();

  dap_write_byte(NVMCTRL_CTRLC, 0);
  dap_write_word(NVMCTRL_ADDR, addr);
  dap_write_half(NVMCTRL_CTRLA, NVMCTRL_CMD_ER);
  while (0 == (dap_read_byte(NVMCTRL_STATUS) & NVMCTRL_STATUS_READY));

  dap_write_block(addr, data, FLASH_ROW_SIZE);
}

//-----------------------------------------------------------------------------
static char *target_enumerate(int i)
{
  if (i < ARRAY_SIZE(devices))
    return devices[i].family;

  return NULL;
}

//-----------------------------------------------------------------------------
static char target_help[] =
  "Fuses:\n"
  "  This device has two fuses sections:\n"
  "   - Section 0 represents a complete User Row (256 bytes)\n"
  "   - Section 1 represents a complete BOCOR Row (256 bytes)\n";

//-----------------------------------------------------------------------------
target_ops_t target_mchp_cm23_ops =
{
  .select    = target_select,
  .deselect  = target_deselect,
  .erase     = target_erase,
  .lock      = target_lock,
  .unlock    = target_erase,
  .program   = target_program,
  .verify    = target_verify,
  .read      = target_read,
  .fread     = target_fuse_read,
  .fwrite    = target_fuse_write,
  .enumerate = target_enumerate,
  .help      = target_help,
};

