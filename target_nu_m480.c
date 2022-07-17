// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021, Alex Taradov <alex@taradov.com>. All rights reserved.

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
#define FLASH_PAGE_SIZE        4096

#define PAGE_ERASE_TIME        80 // ms
#define BANK_ERASE_TIME        320 // ms

#define DHCSR                  0xe000edf0
#define DHCSR_DEBUGEN          (1 << 0)
#define DHCSR_HALT             (1 << 1)
#define DHCSR_DBGKEY           (0xa05f << 16)

#define DEMCR                  0xe000edfc
#define DEMCR_VC_CORERESET     (1 << 0)

#define AIRCR                  0xe000ed0c
#define AIRCR_VECTKEY          (0x05fa << 16)
#define AIRCR_SYSRESETREQ      (1 << 2)

#define SYS_PDID               0x40000000
#define SYS_REGLCTL            0x40000100

#define FMC_ISPCTL             0x4000c000
#define FMC_ISPCTL_ISPEN       (1 << 0)
#define FMC_ISPCTL_LDUEN       (1 << 5)
#define FMC_ISPCTL_CFGUEN      (1 << 4)
#define FMC_ISPCTL_APUEN       (1 << 3)

#define FMC_ISPADDR            0x4000c004

#define FMC_ISPDAT             0x4000c008

#define FMC_ISPCMD             0x4000c00c
#define FMC_ISPCMD_READ        0x00 // FLASH Read
#define FMC_ISPCMD_READ_UID    0x04 // Read Unique ID
#define FMC_ISPCMD_ALL_ONE     0x08 // Read Flash All-One Result
#define FMC_ISPCMD_READ_CID    0x0b // Read Company ID
#define FMC_ISPCMD_READ_DID    0x0c // Read Device ID
#define FMC_ISPCMD_READ_CS     0x0d // Read Checksum
#define FMC_ISPCMD_32B_PROG    0x21 // FLASH 32-bit Program
#define FMC_ISPCMD_PAGE_ERASE  0x22 // FLASH Page Erase
#define FMC_ISPCMD_BANK_ERASE  0x23 // FLASH Bank Erase
#define FMC_ISPCMD_BLOCK_ERASE 0x25 // FLASH Block Erase
#define FMC_ISPCMD_MASS_ERASE  0x26 // FLASH Mass Erase
#define FMC_ISPCMD_MW_PROG     0x27 // FLASH Multi-Word Program
#define FMC_ISPCMD_ALL_ONE_RUN 0x28 // Run Flash All-One Verification
#define FMC_ISPCMD_CS          0x2d // Run Checksum Calculation
#define FMC_ISPCMD_VECT_REMAP  0x2e // Vector Remap
#define FMC_ISPCMD_64B_READ    0x40 // FLASH 64-bit Read
#define FMC_ISPCMD_64B_PROG    0x61 // FLASH 64-bit Program

#define FMC_ISPTRG             0x4000c010
#define FMC_ISPTRG_ISPGO       (1 << 0)

#define FMC_ISPSTS             0x4000c040
#define FMC_ISPSTS_ISPBUSY     (1 << 0)
#define FMC_ISPSTS_PGFF        (1 << 5)
#define FMC_ISPSTS_ISPFF       (1 << 6)
#define FMC_ISPSTS_ALLONE      (1 << 7)

#define FMC_MPDAT0             0x4000c080
#define FMC_MPDAT1             0x4000c084
#define FMC_MPDAT2             0x4000c088
#define FMC_MPDAT3             0x4000c08c

#define FMC_UNDOCUMENTED       0x4000c01c // Undocumented register needed for mass erase

#define CONFIG0                0x00300000
#define CONFIG1                0x00300004
#define CONFIG2                0x00300008
#define CONFIG3                0x0030000c

#define CONFIG0_LOCK           (1 << 1)
#define CONFIG0_ICELOCK        (1 << 11)

#define CONFIG_COUNT           4
#define CONFIG_SIZE            4

#define STATUS_INTERVAL        4 // pages

/*- Types -------------------------------------------------------------------*/
typedef struct
{
  uint32_t  idcode;
  char      *family;
  char      *name;
  int       flash_size;
} device_t;

/*- Variables ---------------------------------------------------------------*/
static device_t devices[] =
{
  { 0x00d48410, "m480", "M484SIDAE", 512*1024 },
};

static device_t target_device;
static target_options_t target_options;

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
static void fmc_cmd(int cmd, int delay)
{
  uint32_t sts;

  dap_write_word(FMC_ISPCMD, cmd);
  dap_write_word(FMC_ISPTRG, FMC_ISPTRG_ISPGO);

  if (delay)
    sleep_ms(delay);

  while (dap_read_word(FMC_ISPTRG));

  sts = dap_read_word(FMC_ISPSTS);
  if (sts & FMC_ISPSTS_ISPFF)
    error_exit("flash error while executing command 0x%02x", cmd);
}

//-----------------------------------------------------------------------------
static uint32_t fmc_read(uint32_t addr)
{
  dap_write_word(FMC_ISPCMD, FMC_ISPCMD_READ);
  dap_write_word(FMC_ISPADDR, addr);
  dap_write_word(FMC_ISPDAT, 0);
  dap_write_word(FMC_ISPTRG, FMC_ISPTRG_ISPGO);
  while (dap_read_word(FMC_ISPTRG));
  return dap_read_word(FMC_ISPDAT);
}

//-----------------------------------------------------------------------------
static void fmc_write(uint32_t addr, uint32_t data)
{
  dap_write_word(FMC_ISPCMD, FMC_ISPCMD_32B_PROG);
  dap_write_word(FMC_ISPADDR, addr);
  dap_write_word(FMC_ISPDAT, data);
  dap_write_word(FMC_ISPTRG, FMC_ISPTRG_ISPGO);
  while (dap_read_word(FMC_ISPTRG));
}

//-----------------------------------------------------------------------------
static void target_select(target_options_t *options)
{
  uint32_t idcode;
  bool locked;

  dap_disconnect();
  dap_connect(DAP_INTERFACE_SWD);
  dap_reset_pin(0);
  dap_reset_link();

  // Stop the core
  dap_write_word(DHCSR, DHCSR_DBGKEY | DHCSR_DEBUGEN | DHCSR_HALT);
  dap_write_word(DEMCR, DEMCR_VC_CORERESET);
  dap_write_word(AIRCR, AIRCR_VECTKEY | AIRCR_SYSRESETREQ);

  dap_reset_pin(1);
  sleep_ms(10);

  idcode = dap_read_word(SYS_PDID);

  for (int i = 0; i < ARRAY_SIZE(devices); i++)
  {
    if (devices[i].idcode != idcode)
      continue;

    verbose("Target: %s\n", devices[i].name);

    target_device = devices[i];
    target_options = *options;

    target_check_options(&target_options, devices[i].flash_size, FLASH_PAGE_SIZE);

    dap_write_word(SYS_REGLCTL, 0x59);
    dap_write_word(SYS_REGLCTL, 0x16);
    dap_write_word(SYS_REGLCTL, 0x88);

    dap_write_word(FMC_ISPCTL, FMC_ISPCTL_ISPEN);

    locked = (0 == (fmc_read(CONFIG0) & CONFIG0_LOCK));

    if (locked && !options->unlock)
      error_exit("target is locked, unlock is necessary");

    return;
  }

  error_exit("unknown target device (SYS_PDID = 0x%08x)", idcode);
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
  dap_write_word(FMC_ISPCTL, FMC_ISPCTL_ISPEN | FMC_ISPCTL_APUEN);

  // Bank 0
  dap_write_word(FMC_ISPADDR, 0);
  fmc_cmd(FMC_ISPCMD_BANK_ERASE, BANK_ERASE_TIME);

  // Bank 1
  dap_write_word(FMC_ISPADDR, target_device.flash_size / 2);
  fmc_cmd(FMC_ISPCMD_BANK_ERASE, BANK_ERASE_TIME);
}

//-----------------------------------------------------------------------------
static void target_lock(void)
{
  dap_write_word(FMC_ISPCTL, FMC_ISPCTL_ISPEN | FMC_ISPCTL_CFGUEN);
  fmc_write(CONFIG0, ~((uint32_t)CONFIG0_LOCK));
}

//-----------------------------------------------------------------------------
static void target_unlock(void)
{
  dap_write_word(FMC_ISPCTL, FMC_ISPCTL_ISPEN | FMC_ISPCTL_APUEN);
  dap_write_word(FMC_UNDOCUMENTED, 1);

  dap_write_word(FMC_ISPADDR, 0);
  fmc_cmd(FMC_ISPCMD_MASS_ERASE, BANK_ERASE_TIME);

  dap_write_word(FMC_UNDOCUMENTED, 0);
}

//-----------------------------------------------------------------------------
static void target_program(void)
{
  uint32_t addr = target_options.offset;
  uint32_t offs = 0;
  uint32_t start_page, number_of_pages;
  uint8_t *buf = target_options.file_data;
  uint32_t size = target_options.file_size;

  start_page = target_options.offset / FLASH_PAGE_SIZE;
  number_of_pages = (size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;

  dap_write_word(FMC_ISPCTL, FMC_ISPCTL_ISPEN | FMC_ISPCTL_APUEN);

  for (uint32_t page = 0; page < number_of_pages; page++)
  {
    dap_write_word(FMC_ISPADDR, (start_page + page) * FLASH_PAGE_SIZE);

    fmc_cmd(FMC_ISPCMD_PAGE_ERASE, PAGE_ERASE_TIME);

    if (0 == (page % STATUS_INTERVAL))
      verbose(".");
  }

  verbose(",");

  dap_write_word(FMC_ISPCMD, FMC_ISPCMD_64B_PROG);

  for (uint32_t page = 0; page < number_of_pages; page++)
  {
    for (uint32_t i = 0; i < FLASH_PAGE_SIZE / sizeof(uint64_t); i++)
    {
      dap_write_word_req(FMC_ISPADDR, addr);
      dap_write_word_req(FMC_MPDAT0, *(uint32_t *)&buf[offs]);
      dap_write_word_req(FMC_MPDAT1, *(uint32_t *)&buf[offs+4]);
      dap_write_word_req(FMC_ISPTRG, FMC_ISPTRG_ISPGO);
      addr += sizeof(uint64_t);
      offs += sizeof(uint64_t);
    }

    dap_transfer();

    if (0 == (page % STATUS_INTERVAL))
      verbose(".");
  }

  if (dap_read_word(FMC_ISPSTS) & FMC_ISPSTS_ISPFF)
    error_exit("flash error");
}

//-----------------------------------------------------------------------------
static void target_verify(void)
{
  uint32_t addr = target_options.offset;
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

    if (0 == (offs % (FLASH_PAGE_SIZE * STATUS_INTERVAL)))
      verbose(".");
  }

  buf_free(bufb);
}

//-----------------------------------------------------------------------------
static void target_read(void)
{
  uint32_t addr = target_options.offset;
  uint32_t offs = 0;
  uint8_t *buf = target_options.file_data;
  uint32_t size = target_options.size;

  while (size)
  {
    dap_read_block(addr, &buf[offs], FLASH_PAGE_SIZE);

    addr += FLASH_PAGE_SIZE;
    offs += FLASH_PAGE_SIZE;
    size -= FLASH_PAGE_SIZE;

    if (0 == (offs % (FLASH_PAGE_SIZE * STATUS_INTERVAL)))
      verbose(".");
  }

  save_file(target_options.name, buf, target_options.size);
}

//-----------------------------------------------------------------------------
static int target_fuse_read(int section, uint8_t *data)
{
  uint32_t *value = (uint32_t *)data;

  if (section >= CONFIG_COUNT)
    return 0;

  dap_write_word(FMC_ISPCTL, FMC_ISPCTL_ISPEN);

  *value = fmc_read(CONFIG0 + section * CONFIG_SIZE);

  return CONFIG_SIZE;
}

//-----------------------------------------------------------------------------
static void target_fuse_write(int section, uint8_t *data)
{
  uint32_t config[CONFIG_COUNT];

  if (section >= CONFIG_COUNT)
    return;

  dap_write_word(FMC_ISPCTL, FMC_ISPCTL_ISPEN | FMC_ISPCTL_CFGUEN);

  for (int i = 0; i < CONFIG_COUNT; i++)
    config[i] = fmc_read(CONFIG0 + i*4);

  config[section] = *(uint32_t *)data;
  config[0] |= CONFIG0_ICELOCK; // Make sure ICE is never locked for now

  dap_write_word(FMC_ISPADDR, CONFIG0);
  fmc_cmd(FMC_ISPCMD_PAGE_ERASE, PAGE_ERASE_TIME);

  for (int i = 0; i < CONFIG_COUNT; i++)
    fmc_write(CONFIG0 + i*4, config[i]);
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
  "  User Configuration words are represented by the following sections (32-bits each):\n"
  "    0 - CONFIG0\n"
  "    1 - CONFIG1\n"
  "    2 - CONFIG2\n"
  "    3 - CONFIG3\n";

//-----------------------------------------------------------------------------
target_ops_t target_nu_m480_ops =
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


