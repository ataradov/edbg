// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2019, Alex Taradov <alex@taradov.com>. All rights reserved.

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
#define FLASH_ADDR             0x08000000
#define FLASH_ALIGN_SIZE       256
#define FLASH_SECTOR_COUNT     (12 + 12 + 4)

#define DHCSR                  0xe000edf0
#define DHCSR_DEBUGEN          (1 << 0)
#define DHCSR_HALT             (1 << 1)
#define DHCSR_DBGKEY           (0xa05f << 16)

#define DEMCR                  0xe000edfc
#define DEMCR_VC_CORERESET     (1 << 0)

#define AIRCR                  0xe000ed0c
#define AIRCR_VECTKEY          (0x05fa << 16)
#define AIRCR_SYSRESETREQ      (1 << 2)

#define FMC_KEY                0x40023c04
#define FMC_OBKEY              0x40023c08
#define FMC_STAT               0x40023c0c
#define FMC_CTL                0x40023c10
#define FMC_OBCTL0             0x40023c14
#define FMC_OBCTL1             0x40023c18
#define FMC_WSEN               0x40023cfc
#define FMC_PID                0x40023d00

#define DBG_ID                 0xe0042000
#define FLASH_SRAM_SIZE_REG    0x1fff7a20

#define FLASH_SIZE_REG_OFFS    16
#define FLASH_SIZE_REG_MULT    1024

#define FMC_KEY_KEY1           0x45670123
#define FMC_KEY_KEY2           0xcdef89ab

#define FMC_OBKEY_KEY1         0x08192a3b
#define FMC_OBKEY_KEY2         0x4c5d6e7f

#define FMC_STAT_END           (1 << 0)
#define FMC_STAT_OPERR         (1 << 1)
#define FMC_STAT_WPERR         (1 << 4)
#define FMC_STAT_PGMERR        (1 << 6)
#define FMC_STAT_PGSERR        (1 << 7)
#define FMC_STAT_RDDERR        (1 << 8)
#define FMC_STAT_BUSY          (1 << 16)

#define FMC_STAT_ALL_ERRORS    (FMC_STAT_OPERR | FMC_STAT_WPERR | \
    FMC_STAT_PGMERR | FMC_STAT_PGSERR | FMC_STAT_RDDERR)

#define FMC_CTL_PG             (1 << 0)
#define FMC_CTL_SER            (1 << 1)
#define FMC_CTL_MER0           (1 << 2)
#define FMC_CTL_SN(x)          ((x) << 3)
#define FMC_CTL_PSZ_BYTE       (0 << 8)
#define FMC_CTL_PSZ_HALF       (1 << 8)
#define FMC_CTL_PSZ_WORD       (2 << 8)
#define FMC_CTL_MER1           (1 << 15)
#define FMC_CTL_START          (1 << 16)
#define FMC_CTL_ENDIE          (1 << 24)
#define FMC_CTL_ERIE           (1 << 25)
#define FMC_CTL_LK             (1 << 31)

#define FMC_OBCTL0_OB_LK       (1 << 0)
#define FMC_OBCTL0_OB_START    (1 << 1)
#define FMC_OBCTL0_BOR_TH(x)   ((x) << 2)
#define FMC_OBCTL0_BB          (1 << 4)
#define FMC_OBCTL0_nWDG_HW     (1 << 5)
#define FMC_OBCTL0_nRST_DPSLP  (1 << 6)
#define FMC_OBCTL0_nRST_STDBY  (1 << 7)
#define FMC_OBCTL0_SPC(x)      ((x) << 8)
#define FMC_OBCTL0_WP0(x)      ((x) << 16)
#define FMC_OBCTL0_DBS         (1 << 30)
#define FMC_OBCTL0_DRP         (1 << 31)

#define FMC_OBCTL1_WP1(x)      ((x) << 16)

#define OPTIONS_USER           0x1fffc000
#define OPTIONS_SPC            0x1fffc001
#define OPTIONS_WP0            0x1fffc008
#define OPTIONS_WP0H           0x1fffc009
#define OPTIONS_WP1            0x1ffec008
#define OPTIONS_WP1H           0x1ffec009

#define OPTIONS_COUNT          2

/*- Types -------------------------------------------------------------------*/
typedef struct
{
  uint32_t     idcode;
  char         *family;
  char         *name;
} device_t;

/*- Constants ---------------------------------------------------------------*/
static const device_t devices[] =
{
  { 0x16080413, "gd32f4xx", "GD32F407VET6" },
};

static const int flash_sector_size[FLASH_SECTOR_COUNT] =
{
  16, 16, 16, 16, 64, 128, 128, 128, 128, 128, 128, 128,
  16, 16, 16, 16, 64, 128, 128, 128, 128, 128, 128, 128,
  256, 256, 256, 256,
};

static const int flash_sector_index[FLASH_SECTOR_COUNT] =
{
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
  16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
  12, 13, 14, 15
};

/*- Variables ---------------------------------------------------------------*/
static device_t target_device;
static target_options_t target_options;

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
static void flash_wait_done(void)
{
  uint32_t stat;

  while (dap_read_word(FMC_STAT) & FMC_STAT_BUSY);

  stat = dap_read_word(FMC_STAT);

  if (stat & FMC_STAT_ALL_ERRORS)
    error_exit("flash operation failed. FMC_STAT = 0x%08x", stat);
}

//-----------------------------------------------------------------------------
static void target_select(target_options_t *options)
{
  uint32_t idcode, flash_size;
  bool locked, ctl_lk, ob_lk;

  dap_reset_pin(0);
  dap_reset_link();
  dap_write_word(DHCSR, DHCSR_DBGKEY | DHCSR_DEBUGEN | DHCSR_HALT);
  dap_write_word(DEMCR, DEMCR_VC_CORERESET);
  dap_write_word(AIRCR, AIRCR_VECTKEY | AIRCR_SYSRESETREQ);
  dap_reset_pin(1);

  idcode = dap_read_word(DBG_ID);

  for (int i = 0; i < ARRAY_SIZE(devices); i++)
  {
    if (devices[i].idcode != idcode)
      continue;

    verbose("Target: %s\n", devices[i].name);

    target_device = devices[i];
    target_options = *options;

    locked = (0xaa != dap_read_byte(OPTIONS_SPC));

    if (locked && !options->unlock)
      error_exit("target is locked, unlock is necessary");

    flash_size = (dap_read_word(FLASH_SRAM_SIZE_REG) >> FLASH_SIZE_REG_OFFS) * FLASH_SIZE_REG_MULT;

    target_check_options(&target_options, flash_size, FLASH_ALIGN_SIZE);

    dap_write_word(FMC_KEY, FMC_KEY_KEY1);
    dap_write_word(FMC_KEY, FMC_KEY_KEY2);
    dap_write_word(FMC_OBKEY, FMC_OBKEY_KEY1);
    dap_write_word(FMC_OBKEY, FMC_OBKEY_KEY2);
    dap_write_word(FMC_CTL, 0);

    ctl_lk = (dap_read_word(FMC_CTL) & FMC_CTL_LK) > 0;
    ob_lk  = (dap_read_word(FMC_OBCTL0) & FMC_OBCTL0_OB_LK) > 0;
    check(!ctl_lk && !ob_lk, "Failed to unlock the flash for write operation. Try to power cycle the target.");

    return;
  }

  error_exit("unknown target device (DBG_ID = 0x%08x)", idcode);
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
  dap_write_word(FMC_CTL, FMC_CTL_MER0 | FMC_CTL_MER1);
  dap_write_word(FMC_CTL, FMC_CTL_MER0 | FMC_CTL_MER1 | FMC_CTL_START);
  flash_wait_done();
  dap_write_word(FMC_CTL, 0);
}

//-----------------------------------------------------------------------------
static void target_lock(void)
{
  uint32_t ob0 = dap_read_word(FMC_OBCTL0);
  uint32_t ob1 = dap_read_word(FMC_OBCTL1);

  ob0 |= FMC_OBCTL0_SPC(0xff); // Set low level security

  dap_write_word(FMC_OBCTL1, ob1);
  dap_write_word(FMC_OBCTL0, ob0);
  dap_write_word(FMC_OBCTL0, ob0 | FMC_OBCTL0_OB_START);
  flash_wait_done();
}

//-----------------------------------------------------------------------------
static void target_unlock(void)
{
  uint32_t ob0 = dap_read_word(FMC_OBCTL0);
  uint32_t ob1 = dap_read_word(FMC_OBCTL1);

  ob0 = (ob0 & ~FMC_OBCTL0_SPC(0xff)) | FMC_OBCTL0_SPC(0xaa);

  dap_write_word(FMC_OBCTL1, ob1);
  dap_write_word(FMC_OBCTL0, ob0);
  dap_write_word(FMC_OBCTL0, ob0 | FMC_OBCTL0_OB_START);
  flash_wait_done();
}

//-----------------------------------------------------------------------------
static void target_program(void)
{
  uint32_t addr = FLASH_ADDR + target_options.offset;
  uint32_t offs = 0;
  uint8_t *buf = target_options.file_data;
  uint32_t size = target_options.file_size;
  int start_sector, end_sector;
  int sector_offset = 0;

  size = round_up(size, FLASH_ALIGN_SIZE);

  for (start_sector = 0; start_sector < FLASH_SECTOR_COUNT; start_sector++)
  {
    int sector_end = sector_offset + flash_sector_size[start_sector] * 1024;
    int start = target_options.offset;

    if (sector_offset <= start && start < sector_end)
      break;

    sector_offset = sector_end;
  }

  for (end_sector = start_sector; end_sector < FLASH_SECTOR_COUNT; end_sector++)
  {
    int sector_end = sector_offset + flash_sector_size[end_sector] * 1024;
    int end = target_options.offset + size;

    if (sector_offset <= end && end <= sector_end)
      break;

    sector_offset = sector_end;
  }

  for (int i = start_sector; i <= end_sector; i++)
  {
    uint32_t cmd = FMC_CTL_SER | FMC_CTL_SN(flash_sector_index[i]);

    dap_write_word(FMC_CTL, cmd);
    dap_write_word(FMC_CTL, cmd | FMC_CTL_START);
    flash_wait_done();

    verbose(".");
  }

  verbose(",");

  // TODO: Verify this works with fast SWD clocks

  dap_write_word(FMC_CTL, FMC_CTL_PSZ_WORD | FMC_CTL_PG);

  while (size)
  {
    dap_write_block(addr, &buf[offs], FLASH_ALIGN_SIZE);

    addr += FLASH_ALIGN_SIZE;
    offs += FLASH_ALIGN_SIZE;
    size -= FLASH_ALIGN_SIZE;

    if (0 == (offs % (FLASH_ALIGN_SIZE * 64)))
      verbose(".");
  }

  flash_wait_done();

  dap_write_word(FMC_CTL, 0);
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

  bufb = buf_alloc(FLASH_ALIGN_SIZE);

  while (size)
  {
    dap_read_block(addr, bufb, FLASH_ALIGN_SIZE);

    block_size = (size > FLASH_ALIGN_SIZE) ? FLASH_ALIGN_SIZE : size;

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

    addr += FLASH_ALIGN_SIZE;
    offs += FLASH_ALIGN_SIZE;
    size -= block_size;

    if (0 == (offs % (FLASH_ALIGN_SIZE * 64)))
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

  size = round_up(size, FLASH_ALIGN_SIZE);

  while (size)
  {
    dap_read_block(addr, &buf[offs], FLASH_ALIGN_SIZE);

    addr += FLASH_ALIGN_SIZE;
    offs += FLASH_ALIGN_SIZE;
    size -= FLASH_ALIGN_SIZE;

    if (0 == (offs % (FLASH_ALIGN_SIZE * 64)))
      verbose(".");
  }

  save_file(target_options.name, buf, target_options.size);
}

//-----------------------------------------------------------------------------
static int target_fuse_read(int section, uint8_t *data)
{
  uint32_t value;

  if (0 == section)
    value = dap_read_word(FMC_OBCTL0);
  else if (1 == section)
    value = dap_read_word(FMC_OBCTL1);
  else
    return 0;

  data[0] = value;
  data[1] = value >> 8;
  data[2] = value >> 16;
  data[3] = value >> 24;

  return 4;
}

//-----------------------------------------------------------------------------
static void target_fuse_write(int section, uint8_t *data)
{
  uint32_t value = (data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0];
  uint32_t ob0, ob1;

  check(section < OPTIONS_COUNT, "internal: incorrect section index in target_fuse_write()");

  if (0 == section)
  {
    ob0 = value;
    ob1 = dap_read_word(FMC_OBCTL1);
  }
  else
  {
    ob0 = dap_read_word(FMC_OBCTL0);
    ob1 = value;
  }

  ob0 &= ~(FMC_OBCTL0_OB_LK | FMC_OBCTL0_OB_START);

  dap_write_word(FMC_OBCTL1, ob1);
  dap_write_word(FMC_OBCTL0, ob0);
  dap_write_word(FMC_OBCTL0, ob0 | FMC_OBCTL0_OB_START);
  flash_wait_done();
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
  "Notes:\n"
  "  This device has variable erase sector size. The size and offset granularity\n"
  "  is set to 256 bytes for user input verification purposes. But keep in mind\n"
  "  that write operation will erase the full sector affected by the operation.\n"
  "\n"
  "Fuses:\n"
  "  The option bytes are represented by the following sections (32-bits each):\n"
  "    0 - FMC_OBCTL0\n"
  "    1 - FMC_OBCTL1\n";

//-----------------------------------------------------------------------------
target_ops_t target_gd_gd32f4xx_ops =
{
  .select    = target_select,
  .deselect  = target_deselect,
  .erase     = target_erase,
  .lock      = target_lock,
  .unlock    = target_unlock,
  .program   = target_program,
  .verify    = target_verify,
  .read      = target_read,
  .fread     = target_fuse_read,
  .fwrite    = target_fuse_write,
  .enumerate = target_enumerate,
  .help      = target_help,
};

