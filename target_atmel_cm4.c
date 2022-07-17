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
#define CMD_CGPB               0x5a00000c
#define CMD_GGPB               0x5a00000d

#define PAGES_IN_ERASE_BLOCK   16

#define GPNVM_SIZE             1
#define GPNVM_SIZE_BITS        8

/*- Types -------------------------------------------------------------------*/
typedef struct
{
  uint32_t  chip_id;
  uint32_t  chip_exid;
  char      *family;
  char      *name;
  uint32_t  n_planes;
  uint32_t  flash_size;
} device_t;

/*- Variables ---------------------------------------------------------------*/
static device_t devices[] =
{
  { 0x243b09e0, 0x00000000, "samg51", "SAM G51G18",          1,  256*1024 },
  { 0x243b09e8, 0x00000000, "samg51", "SAM G51N18",          1,  256*1024 },
  { 0x247e0ae0, 0x00000000, "samg53", "SAM G53G19 (Rev A)",  1,  512*1024 },
  { 0x247e0ae1, 0x00000000, "samg53", "SAM G53G19 (Rev B)",  1,  512*1024 },
  { 0x247e0ae8, 0x00000000, "samg53", "SAM G53N19 (Rev A)",  1,  512*1024 },
  { 0x247e0ae9, 0x00000000, "samg53", "SAM G53N19 (Rev B)",  1,  512*1024 },
  { 0x247e0ae2, 0x00000000, "samg54", "SAM G54G19 (Rev A)",  1,  512*1024 },
  { 0x247e0ae3, 0x00000000, "samg54", "SAM G54G19 (Rev B)",  1,  512*1024 },
  { 0x247e0ae6, 0x00000000, "samg54", "SAM G54J19 (Rev A)",  1,  512*1024 },
  { 0x247e0aea, 0x00000000, "samg54", "SAM G54N19 (Rev A)",  1,  512*1024 },
  { 0x247e0aeb, 0x00000000, "samg54", "SAM G54N19 (Rev B)",  1,  512*1024 },
  { 0x24470ae0, 0x00000000, "samg55", "SAM G55G19",          1,  512*1024 },
  { 0x24570ae0, 0x00000000, "samg55", "SAM G55J19 (Rev A)",  1,  512*1024 },
  { 0x24570ae1, 0x00000000, "samg55", "SAM G55J19 (Rev B)",  1,  512*1024 },
  { 0x29970ee0, 0x00000000, "sam4sd", "SAM4SD32B (Rev A)",   2, 1024*1024 },
  { 0x29970ee1, 0x00000000, "sam4sd", "SAM4SD32B (Rev B)",   2, 1024*1024 },
  { 0x29a70ee0, 0x00000000, "sam4sd", "SAM4SD32C (Rev A)",   2, 1024*1024 },
  { 0x29a70ee1, 0x00000000, "sam4sd", "SAM4SD32C (Rev B)",   2, 1024*1024 },
  { 0x29970ce0, 0x00000000, "sam4sd", "SAM4SD16B (Rev A)",   2,  512*1024 },
  { 0x29970ce0, 0x00000000, "sam4sd", "SAM4SD16B (Rev B)",   2,  512*1024 },
  { 0x29a70ce0, 0x00000000, "sam4sd", "SAM4SD16C (Rev A)",   2,  512*1024 },
  { 0x29a70ce1, 0x00000000, "sam4sd", "SAM4SD16C (Rev B)",   2,  512*1024 },
  { 0x28970ce0, 0x00000000, "sam4sa", "SAM4SA16B (Rev A)",   1, 1024*1024 },
  { 0x28970ce1, 0x00000000, "sam4sa", "SAM4SA16B (Rev B)",   1, 1024*1024 },
  { 0x28a70ce0, 0x00000000, "sam4sa", "SAM4SA16C (Rev A)",   1, 1024*1024 },
  { 0x28a70ce1, 0x00000000, "sam4sa", "SAM4SA16C (Rev B)",   1, 1024*1024 },
  { 0x289c0ce0, 0x00000000, "sam4s",  "SAM4S16B (Rev A)",    1, 1024*1024 },
  { 0x289c0ce1, 0x00000000, "sam4s",  "SAM4S16B (Rev B)",    1, 1024*1024 },
  { 0x28ac0ce0, 0x00000000, "sam4s",  "SAM4S16C (Rev A)",    1, 1024*1024 },
  { 0x28ac0ce1, 0x00000000, "sam4s",  "SAM4S16C (Rev B)",    1, 1024*1024 },
  { 0x289c0ae0, 0x00000000, "sam4s",  "SAM4S8B (Rev A)",     1,  512*1024 },
  { 0x289c0ae1, 0x00000000, "sam4s",  "SAM4S8B (Rev B)",     1,  512*1024 },
  { 0x28ac0ae0, 0x00000000, "sam4s",  "SAM4S8C (Rev A)",     1,  512*1024 },
  { 0x28ac0ae1, 0x00000000, "sam4s",  "SAM4S8C (Rev B)",     1,  512*1024 },
  { 0x288b09e0, 0x00000000, "sam4s",  "SAM4S4A (Rev A)",     1,  256*1024 },
  { 0x288b09e1, 0x00000000, "sam4s",  "SAM4S4A (Rev B)",     1,  256*1024 },
  { 0x289b09e0, 0x00000000, "sam4s",  "SAM4S4B (Rev A)",     1,  256*1024 },
  { 0x289b09e1, 0x00000000, "sam4s",  "SAM4S4B (Rev B)",     1,  256*1024 },
  { 0x28ab09e0, 0x00000000, "sam4s",  "SAM4S4C (Rev A)",     1,  256*1024 },
  { 0x28ab09e1, 0x00000000, "sam4s",  "SAM4S4C (Rev B)",     1,  256*1024 },
  { 0x288b07e0, 0x00000000, "sam4s",  "SAM4S2A (Rev A)",     1,  128*1024 },
  { 0x288b07e1, 0x00000000, "sam4s",  "SAM4S2A (Rev B)",     1,  128*1024 },
  { 0x289b07e0, 0x00000000, "sam4s",  "SAM4S2B (Rev A)",     1,  128*1024 },
  { 0x289b07e1, 0x00000000, "sam4s",  "SAM4S2B (Rev B)",     1,  128*1024 },
  { 0x28ab07e0, 0x00000000, "sam4s",  "SAM4S2C (Rev A)",     1,  128*1024 },
  { 0x28ab07e1, 0x00000000, "sam4s",  "SAM4S2C (Rev B)",     1,  128*1024 },
  { 0xa3cc0ce0, 0x00120200, "sam4e",  "SAM4E16E",            1, 1024*1024 },
  { 0xa3cc0ce0, 0x00120208, "sam4e",  "SAM4E8E",             1,  512*1024 },
  { 0xa3cc0ce0, 0x00120201, "sam4e",  "SAM4E16C",            1, 1024*1024 },
  { 0xa3cc0ce0, 0x00120209, "sam4e",  "SAM4E8C",             1,  512*1024 },
  { 0x29460ce0, 0x00000000, "sam4n",  "SAM4N16B (Rev A)",    1, 1024*1024 },
  { 0x29560ce0, 0x00000000, "sam4n",  "SAM4N16C (Rev A)",    1, 1024*1024 },
  { 0x293b0ae0, 0x00000000, "sam4n",  "SAM4N8A (Rev A)",     1,  512*1024 },
  { 0x294b0ae0, 0x00000000, "sam4n",  "SAM4N8B (Rev A)",     1,  512*1024 },
  { 0x295b0ae0, 0x00000000, "sam4n",  "SAM4N8C (Rev A)",     1,  512*1024 },
};

static device_t target_device;
static target_options_t target_options;

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
static void target_select(target_options_t *options)
{
  uint32_t chip_id, chip_exid;

  dap_reset_target_hw(1);
  dap_reset_link();

  // Stop the core
  dap_write_word(DHCSR, DHCSR_DBGKEY | DHCSR_DEBUGEN | DHCSR_HALT);
  dap_write_word(DEMCR, DEMCR_VC_CORERESET);
  dap_write_word(AIRCR, AIRCR_VECTKEY | AIRCR_SYSRESETREQ);

  chip_id = dap_read_word(CHIPID_CIDR);
  chip_exid = dap_read_word(CHIPID_EXID);

  for (int i = 0; i < ARRAY_SIZE(devices); i++)
  {
    uint32_t fl_id, fl_size, fl_page_size, fl_nb_palne, fl_nb_lock;

    if (devices[i].chip_id != chip_id || devices[i].chip_exid != chip_exid)
      continue;

    verbose("Target: %s\n", devices[i].name);

    for (uint32_t plane = 0; plane < devices[i].n_planes; plane++)
    {
      dap_write_word(EEFC_FCR(plane), CMD_GETD);
      while (0 == (dap_read_word(EEFC_FSR(plane)) & FSR_FRDY));

      fl_id = dap_read_word(EEFC_FRR(plane));
      check(fl_id, "Cannot read flash descriptor, check Erase pin state");

      fl_size = dap_read_word(EEFC_FRR(plane));
      check(fl_size == devices[i].flash_size, "Invalid reported Flash size (%d)", fl_size);

      fl_page_size = dap_read_word(EEFC_FRR(plane));
      check(fl_page_size == FLASH_PAGE_SIZE, "Invalid reported page size (%d)", fl_page_size);

      fl_nb_palne = dap_read_word(EEFC_FRR(plane));
      for (uint32_t i = 0; i < fl_nb_palne; i++)
        dap_read_word(EEFC_FRR(plane));

      fl_nb_lock =  dap_read_word(EEFC_FRR(plane));
      for (uint32_t i = 0; i < fl_nb_lock; i++)
        dap_read_word(EEFC_FRR(plane));
    }

    target_device = devices[i];
    target_options = *options;

    target_check_options(&target_options, devices[i].flash_size * target_device.n_planes,
        FLASH_PAGE_SIZE * PAGES_IN_ERASE_BLOCK);

    return;
  }

  error_exit("unknown target device (CHIP_ID = 0x%08x)", chip_id);
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
  uint32_t addr = FLASH_START + target_options.offset;
  uint32_t number_of_pages, plane, page_offset;
  uint32_t offs = 0;
  uint8_t *buf = target_options.file_data;
  uint32_t size = target_options.file_size;

  number_of_pages = (size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;
  page_offset = target_options.offset / FLASH_PAGE_SIZE;

  for (uint32_t page = 0; page < number_of_pages; page += PAGES_IN_ERASE_BLOCK)
  {
    plane = (page + page_offset) / (target_device.flash_size / FLASH_PAGE_SIZE);

    dap_write_word(EEFC_FCR(plane), CMD_EPA | (((page_offset + page) | 2) << 8));
    while (0 == (dap_read_word(EEFC_FSR(plane)) & FSR_FRDY));

    verbose(".");
  }

  verbose(",");

  for (uint32_t page = 0; page < number_of_pages; page++)
  {
    dap_write_block(addr, &buf[offs], FLASH_PAGE_SIZE);
    addr += FLASH_PAGE_SIZE;
    offs += FLASH_PAGE_SIZE;

    plane = (page + page_offset) / (target_device.flash_size / FLASH_PAGE_SIZE);

    dap_write_word(EEFC_FCR(plane), CMD_WP | ((page + page_offset) << 8));
    while (0 == (dap_read_word(EEFC_FSR(plane)) & FSR_FRDY));

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

  dap_write_word(EEFC_FCR(0), CMD_GGPB);
  while (0 == (dap_read_word(EEFC_FSR(0)) & FSR_FRDY));
  gpnvm = dap_read_word(EEFC_FRR(0));

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
      dap_write_word(EEFC_FCR(0), CMD_SGPB | (i << 8));
    else
      dap_write_word(EEFC_FCR(0), CMD_CGPB | (i << 8));
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
target_ops_t target_atmel_cm4_ops =
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

