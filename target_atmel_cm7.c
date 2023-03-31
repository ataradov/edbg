// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2013-2019, Alex Taradov <alex@taradov.com>. All rights reserved.

/*- Includes ----------------------------------------------------------------*/
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "target.h"
#include "edbg.h"
#include "dap.h"

/*- Definitions -------------------------------------------------------------*/
#define FLASH_START            0x00400000
#define FLASH_PAGE_SIZE        512

#define DHCSR                  0xe000edf0
#define DHCSR_DEBUGEN          (1 << 0)
#define DHCSR_HALT             (1 << 1)
#define DHCSR_DBGKEY           (0xa05f << 16)

#define DEMCR                  0xe000edfc
#define DEMCR_VC_CORERESET     (1 << 0)

#define AIRCR                  0xe000ed0c
#define AIRCR_VECTKEY          (0x05fa << 16)
#define AIRCR_SYSRESETREQ      (1 << 2)

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

#define GPNVM_SIZE             2
#define GPNVM_SIZE_BITS        9

#define DEVICE_ID_MASK         0xfffffff0
#define DEVICE_REV_MASK        0xf

/*- Types -------------------------------------------------------------------*/
typedef struct
{
  uint32_t  chip_id;
  uint32_t  chip_exid;
  char      *family;
  char      *name;
  uint32_t  flash_size;
} device_t;

/*- Variables ---------------------------------------------------------------*/
static device_t devices[] =
{
  { 0xa10d0a00, 0x00000002, "same70", "SAM E70Q19",    512*1024 },
  { 0xa1020c00, 0x00000002, "same70", "SAM E70Q20",   1024*1024 },
  { 0xa1020e00, 0x00000002, "same70", "SAM E70Q21", 2*1024*1024 },
  { 0xa10d0a00, 0x00000001, "same70", "SAM E70N19",    512*1024 },
  { 0xa1020c00, 0x00000001, "same70", "SAM E70N20",   1024*1024 },
  { 0xa1020e00, 0x00000001, "same70", "SAM E70N21", 2*1024*1024 },
  { 0xa10d0a00, 0x00000000, "same70", "SAM E70J19",    512*1024 },
  { 0xa1020c00, 0x00000000, "same70", "SAM E70J20",   1024*1024 },
  { 0xa1020e00, 0x00000000, "same70", "SAM E70J21", 2*1024*1024 },

  { 0xa11d0a00, 0x00000002, "sams70", "SAM S70Q19",    512*1024 },
  { 0xa1120c00, 0x00000002, "sams70", "SAM S70Q20",   1024*1024 },
  { 0xa1120e00, 0x00000002, "sams70", "SAM S70Q21", 2*1024*1024 },
  { 0xa11d0a00, 0x00000001, "sams70", "SAM S70N19",    512*1024 },
  { 0xa1120c00, 0x00000001, "sams70", "SAM S70N20",   1024*1024 },
  { 0xa1120e00, 0x00000001, "sams70", "SAM S70N21", 2*1024*1024 },
  { 0xa11d0a00, 0x00000000, "sams70", "SAM S70J19",    512*1024 },
  { 0xa1120c00, 0x00000000, "sams70", "SAM S70J20",   1024*1024 },
  { 0xa1120e00, 0x00000000, "sams70", "SAM S70J21", 2*1024*1024 },

  { 0xa13d0a00, 0x00000002, "samv70", "SAM V70Q19",    512*1024 },
  { 0xa1320c00, 0x00000002, "samv70", "SAM V70Q20",   1024*1024 },
  { 0xa13d0a00, 0x00000001, "samv70", "SAM V70N19",    512*1024 },
  { 0xa1320c00, 0x00000001, "samv70", "SAM V70N20",   1024*1024 },
  { 0xa13d0a00, 0x00000000, "samv70", "SAM V70J19",    512*1024 },
  { 0xa1320c00, 0x00000000, "samv70", "SAM V70J20",   1024*1024 },

  { 0xa12d0a00, 0x00000002, "samv71", "SAM V71Q19",    512*1024 },
  { 0xa1220c00, 0x00000002, "samv71", "SAM V71Q20",   1024*1024 },
  { 0xa1220e00, 0x00000002, "samv71", "SAM V71Q21", 2*1024*1024 },
  { 0xa12d0a00, 0x00000001, "samv71", "SAM V71N19",    512*1024 },
  { 0xa1220c00, 0x00000001, "samv71", "SAM V71N20",   1024*1024 },
  { 0xa1220e00, 0x00000001, "samv71", "SAM V71N21", 2*1024*1024 },
  { 0xa12d0a00, 0x00000000, "samv71", "SAM V71J19",    512*1024 },
  { 0xa1220c00, 0x00000000, "samv71", "SAM V71J20",   1024*1024 },
  { 0xa1220e00, 0x00000000, "samv71", "SAM V71J21", 2*1024*1024 },
};

static device_t target_device;
static target_options_t target_options;

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
static void target_select(target_options_t *options)
{
  uint32_t chip_id, chip_exid, id, rev;

  dap_reset_link();

  // Stop the core
  dap_write_word(DHCSR, DHCSR_DBGKEY | DHCSR_DEBUGEN | DHCSR_HALT);
  dap_write_word(DEMCR, DEMCR_VC_CORERESET);

  dap_reset_target_hw(1);
  dap_reset_link();

  chip_id = dap_read_word(CHIPID_CIDR);
  chip_exid = dap_read_word(CHIPID_EXID);

  id  = chip_id & DEVICE_ID_MASK;
  rev = chip_id & DEVICE_REV_MASK;

  for (int i = 0; i < ARRAY_SIZE(devices); i++)
  {
    uint32_t fl_id, fl_size, fl_page_size, fl_nb_palne, fl_nb_lock;

    if (devices[i].chip_id != id || devices[i].chip_exid != chip_exid)
      continue;

    verbose("Target: %s (Rev %c)\n", devices[i].name, 'A' + rev);

    dap_write_word(EEFC_FCR, CMD_GETD);
    while (0 == (dap_read_word(EEFC_FSR) & FSR_FRDY));

    fl_id = dap_read_word(EEFC_FRR);
    check(fl_id, "Cannot read flash descriptor, check Erase pin state");

    fl_size = dap_read_word(EEFC_FRR);
    check(fl_size == devices[i].flash_size, "Invalid reported Flash size (%d)", fl_size);

    fl_page_size = dap_read_word(EEFC_FRR);
    check(fl_page_size == FLASH_PAGE_SIZE, "Invalid reported page size (%d)", fl_page_size);

    fl_nb_palne = dap_read_word(EEFC_FRR);
    for (uint32_t i = 0; i < fl_nb_palne; i++)
      dap_read_word(EEFC_FRR);

    fl_nb_lock =  dap_read_word(EEFC_FRR);
    for (uint32_t i = 0; i < fl_nb_lock; i++)
      dap_read_word(EEFC_FRR);

    target_device = devices[i];
    target_options = *options;

    target_check_options(&target_options, devices[i].flash_size,
        FLASH_PAGE_SIZE * PAGES_IN_ERASE_BLOCK);

    return;
  }

  error_exit("unknown target device (CHIP_ID = 0x%08x)", chip_id);
}

//-----------------------------------------------------------------------------
static void target_deselect(void)
{
  dap_write_word(DHCSR, DHCSR_DBGKEY);
  dap_write_word(DEMCR, 0);
  dap_reset_target_hw(1);

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
static void target_program(void)
{
  uint32_t addr = FLASH_START + target_options.offset;
  uint32_t number_of_pages, page_offset;
  uint32_t offs = 0;
  uint8_t *buf = target_options.file_data;
  uint32_t size = target_options.file_size;

  number_of_pages = (size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;
  page_offset = target_options.offset / FLASH_PAGE_SIZE;

  for (uint32_t page = 0; page < number_of_pages; page += PAGES_IN_ERASE_BLOCK)
  {
    dap_write_word(EEFC_FCR, CMD_EPA | (((page_offset + page) | 2) << 8));
    while (0 == (dap_read_word(EEFC_FSR) & FSR_FRDY));

    verbose(".");
  }

  verbose(",");

  for (uint32_t page = 0; page < number_of_pages; page++)
  {
    dap_write_block(addr, &buf[offs], FLASH_PAGE_SIZE);
    addr += FLASH_PAGE_SIZE;
    offs += FLASH_PAGE_SIZE;

    dap_write_word(EEFC_FCR, CMD_WP | ((page + page_offset) << 8));
    while (0 == (dap_read_word(EEFC_FSR) & FSR_FRDY));

    verbose(".");
  }
}

//-----------------------------------------------------------------------------
static void target_verify(void)
{
  uint32_t addr = FLASH_START + target_options.offset;
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
  uint32_t addr = FLASH_START + target_options.offset;
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
static int target_fuse_read(int section, uint8_t *data)
{
  uint32_t gpnvm;

  if (section > 0)
    return 0;

  dap_write_word(EEFC_FCR, CMD_GGPB);
  while (0 == (dap_read_word(EEFC_FSR) & FSR_FRDY));
  gpnvm = dap_read_word(EEFC_FRR);

  data[0] = gpnvm;
  data[1] = gpnvm >> 8;

  return GPNVM_SIZE;
}

//-----------------------------------------------------------------------------
static void target_fuse_write(int section, uint8_t *data)
{
  uint32_t gpnvm = (data[1] << 8) | data[0];

  check(0 == section, "internal: incorrect section index in target_fuse_write()");

  for (int i = 0; i < GPNVM_SIZE_BITS; i++)
  {
    if (gpnvm & (1 << i))
      dap_write_word(EEFC_FCR, CMD_SGPB | (i << 8));
    else
      dap_write_word(EEFC_FCR, CMD_CGPB | (i << 8));
  }
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
  "  This device has one fuses section, which represents GPNVM bits.\n";

//-----------------------------------------------------------------------------
target_ops_t target_atmel_cm7_ops =
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

