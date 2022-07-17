// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2013-2019, Alex Taradov <alex@taradov.com>
// Copyright (c) 2015, Thibaut VIARD for derivative work from original target_atmel_cm4.c and SAM3X/A port
// All rights reserved.

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
#define DHCSR_DEBUGEN          (1 << 0)
#define DHCSR_HALT             (1 << 1)
#define DHCSR_DBGKEY           (0xa05f << 16)

#define DEMCR                  0xe000edfc
#define DEMCR_VC_CORERESET     (1 << 0)

#define AIRCR                  0xe000ed0c
#define AIRCR_VECTKEY          (0x05fa << 16)
#define AIRCR_SYSRESETREQ      (1 << 2)

#define CHIPID_CIDR(b)         ((b) + 0x00)
#define CHIPID_EXID(b)         ((b) + 0x04)

#define EEFC_FMR(b)            ((b) + 0x00) // EEFC Flash Mode Register
#define EEFC_FCR(b)            ((b) + 0x04) // EEFC Flash Command Register
#define EEFC_FSR(b)            ((b) + 0x08) // EEFC Flash Status Register
#define EEFC_FRR(b)            ((b) + 0x0c) // EEFC Flash Result Register
#define FSR_FRDY               (1ul)

#define CMD_GETD               0x5a000000 // Get Flash Descriptor
#define CMD_EWP                0x5a000003 // Erase page and write page
#define CMD_EA                 0x5a000005 // Erase all
#define CMD_SGPB               0x5a00000b // Set GPNVM Bit
#define CMD_CGPB               0x5a00000c // Clear GPNVM Bit
#define CMD_GGPB               0x5a00000d // Get GPNVM Bit

#define FLASH_PAGE_SIZE        256
#define CHIPID_EXID_VALUE      0

#define GPNVM_SIZE             1
#define GPNVM_SIZE_BITS        8

/*- Types -------------------------------------------------------------------*/
typedef struct
{
  uint32_t  chip_id;
  char      *family;
  char      *name;
  uint32_t  chipid_base;
  int       n_planes;
  struct
  {
    uint32_t  eefc_base;
    uint32_t  addr;
    uint32_t  size;
  } plane[2];
} device_t;

/*- Variables ---------------------------------------------------------------*/
static device_t devices[] =
{
  { 0x286E0A60, "sam3x", "ATSAM3X8H", 0x400e0940, 2, {{ 0x400e0a00, 0x80000, 256*1024 }, { 0x400e0c00, 0xc0000, 256*1024 }} },
  { 0x285E0A60, "sam3x", "ATSAM3X8E", 0x400e0940, 2, {{ 0x400e0a00, 0x80000, 256*1024 }, { 0x400e0c00, 0xc0000, 256*1024 }} },
  { 0x285B0960, "sam3x", "ATSAM3X4E", 0x400e0940, 2, {{ 0x400e0a00, 0x80000, 128*1024 }, { 0x400e0c00, 0xa0000, 128*1024 }} },
  { 0x284E0A60, "sam3x", "ATSAM3X8C", 0x400e0940, 2, {{ 0x400e0a00, 0x80000, 256*1024 }, { 0x400e0c00, 0xc0000, 256*1024 }} },
  { 0x284B0960, "sam3x", "ATSAM3X4C", 0x400e0940, 2, {{ 0x400e0a00, 0x80000, 128*1024 }, { 0x400e0c00, 0xa0000, 128*1024 }} },
  { 0x283E0A60, "sam3a", "ATSAM3A8C", 0x400e0940, 2, {{ 0x400e0a00, 0x80000, 256*1024 }, { 0x400e0c00, 0xc0000, 256*1024 }} },
  { 0x283B0960, "sam3a", "ATSAM3A4C", 0x400e0940, 2, {{ 0x400e0a00, 0x80000, 128*1024 }, { 0x400e0c00, 0xa0000, 128*1024 }} },
  { 0x28000961, "sam3u", "ATSAM3U4C", 0x400e0740, 2, {{ 0x400e0800, 0x80000, 128*1024 }, { 0x400e0a00, 0x100000, 128*1024 }} },
  { 0x280A0761, "sam3u", "ATSAM3U2C", 0x400e0740, 1, {{ 0x400e0800, 0x80000, 128*1024 }} },
  { 0x28090561, "sam3u", "ATSAM3U1C", 0x400e0740, 1, {{ 0x400e0800, 0x80000,  64*1024 }} },
  { 0x28100961, "sam3u", "ATSAM3U4E", 0x400e0740, 2, {{ 0x400e0800, 0x80000, 128*1024 }, { 0x400e0a00, 0x100000, 128*1024 }} },
  { 0x281A0761, "sam3u", "ATSAM3U2E", 0x400e0740, 1, {{ 0x400e0800, 0x80000, 128*1024 }} },
  { 0x28190561, "sam3u", "ATSAM3U1E", 0x400e0740, 1, {{ 0x400e0800, 0x80000,  64*1024 }} },
};

static device_t target_device;
static target_options_t target_options;

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
static uint32_t get_flash_addr(uint32_t addr)
{
  uint32_t offs = addr;

  for (int i = 0; i < target_device.n_planes; i++)
  {
    if (offs >= target_device.plane[i].size)
      offs -= target_device.plane[i].size;
    else
      return target_device.plane[i].addr + offs;
  }

  error_exit("internal error in get_flash_addr()");

  return 0;
}

//-----------------------------------------------------------------------------
static uint32_t get_eefc_base(uint32_t addr)
{
  uint32_t flash_addr = get_flash_addr(addr);

  for (int i = 0; i < target_device.n_planes; i++)
  {
    uint32_t a = target_device.plane[i].addr;
    uint32_t s = target_device.plane[i].size;

    if (a <= flash_addr && flash_addr < (a + s))
      return target_device.plane[i].eefc_base;
  }

  error_exit("internal error in get_eefc_base()");

  return 0;
}

//-----------------------------------------------------------------------------
static void target_select(target_options_t *options)
{
  dap_reset_target_hw(1);
  dap_reset_link();

  // Stop the core
  dap_write_word(DHCSR, DHCSR_DBGKEY | DHCSR_DEBUGEN | DHCSR_HALT);
  dap_write_word(DEMCR, DEMCR_VC_CORERESET);
  dap_write_word(AIRCR, AIRCR_VECTKEY | AIRCR_SYSRESETREQ);

  for (int i = 0; i < ARRAY_SIZE(devices); i++)
  {
    uint32_t chip_id, chip_exid;
    uint32_t fl_id, fl_size, fl_page_size, fl_nb_palne, fl_nb_lock;
    uint32_t flash_size = 0;

    chip_id = dap_read_word(CHIPID_CIDR(devices[i].chipid_base));
    chip_exid = dap_read_word(CHIPID_EXID(devices[i].chipid_base));

    if (devices[i].chip_id != chip_id || CHIPID_EXID_VALUE != chip_exid)
      continue;

    verbose("Target: %s\n", devices[i].name);

    for (int i = 0; i < devices[i].n_planes; i++)
    {
      uint32_t eefc_base = devices[i].plane[i].eefc_base;

      dap_write_word(EEFC_FCR(eefc_base), CMD_GETD);
      while (0 == (dap_read_word(EEFC_FSR(eefc_base)) & FSR_FRDY));

      fl_id = dap_read_word(EEFC_FRR(eefc_base));
      check(fl_id, "Cannot read flash descriptor, check Erase pin state");

      fl_size = dap_read_word(EEFC_FRR(eefc_base));
      check(fl_size == devices[i].plane[i].size, "Invalid reported Flash size (%d)", fl_size);

      fl_page_size = dap_read_word(EEFC_FRR(eefc_base));
      check(fl_page_size == FLASH_PAGE_SIZE, "Invalid reported page size (%d)", fl_page_size);

      fl_nb_palne = dap_read_word(EEFC_FRR(eefc_base));
      for (uint32_t i = 0; i < fl_nb_palne; i++)
        dap_read_word(EEFC_FRR(eefc_base));

      fl_nb_lock =  dap_read_word(EEFC_FRR(eefc_base));
      for (uint32_t i = 0; i < fl_nb_lock; i++)
        dap_read_word(EEFC_FRR(eefc_base));

      flash_size += fl_size;
    }

    target_device = devices[i];
    target_options = *options;

    target_check_options(&target_options, flash_size, FLASH_PAGE_SIZE);

    return;
  }

  error_exit("unknown target device");
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
  for (int i = 0; i < target_device.n_planes; i++)
    dap_write_word(EEFC_FCR(target_device.plane[i].eefc_base), CMD_EA);

  for (int i = 0; i < target_device.n_planes; i++)
    while (0 == (dap_read_word(EEFC_FSR(target_device.plane[i].eefc_base)) & FSR_FRDY));
}

//-----------------------------------------------------------------------------
static void target_lock(void)
{
  for (int i = 0; i < target_device.n_planes; i++)
    dap_write_word(EEFC_FCR(target_device.plane[i].eefc_base), CMD_SGPB | (0 << 8));
}

//-----------------------------------------------------------------------------
static void target_program(void)
{
  uint32_t addr = target_options.offset;
  uint32_t number_of_pages, eefc_base;
  uint32_t offs = 0;
  uint8_t *buf = target_options.file_data;
  uint32_t size = target_options.file_size;

  number_of_pages = (size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;

  for (uint32_t page = 0; page < number_of_pages; page++)
  {
    eefc_base = get_eefc_base(addr);

    dap_write_block(get_flash_addr(addr), &buf[offs], FLASH_PAGE_SIZE);

    dap_write_word(EEFC_FCR(eefc_base), CMD_EWP | (page << 8));
    while (0 == (dap_read_word(EEFC_FSR(eefc_base)) & FSR_FRDY));

    addr += FLASH_PAGE_SIZE;
    offs += FLASH_PAGE_SIZE;

    verbose(".");
  }
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
    dap_read_block(get_flash_addr(addr), bufb, FLASH_PAGE_SIZE);

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
  uint32_t addr = target_options.offset;
  uint32_t offs = 0;
  uint8_t *buf = target_options.file_data;
  uint32_t size = target_options.size;

  while (size)
  {
    dap_read_block(get_flash_addr(addr), &buf[offs], FLASH_PAGE_SIZE);

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

  dap_write_word(EEFC_FCR(get_eefc_base(0)), CMD_GGPB);
  while (0 == (dap_read_word(EEFC_FSR(get_eefc_base(0))) & FSR_FRDY));
  gpnvm = dap_read_word(EEFC_FRR(get_eefc_base(0)));

  data[0] = gpnvm;

  return GPNVM_SIZE;
}

//-----------------------------------------------------------------------------
static void target_fuse_write(int section, uint8_t *data)
{
  uint32_t gpnvm = data[0];

  check(0 == section, "internal: incorrect section index in target_fuse_write()");

  for (int i = 0; i < GPNVM_SIZE_BITS; i++)
  {
    if (gpnvm & (1 << i))
      dap_write_word(EEFC_FCR(get_eefc_base(0)), CMD_SGPB | (i << 8));
    else
      dap_write_word(EEFC_FCR(get_eefc_base(0)), CMD_CGPB | (i << 8));
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
target_ops_t target_atmel_cm3_ops =
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

