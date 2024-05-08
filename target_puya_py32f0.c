// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024, Alex Taradov <alex@taradov.com>. All rights reserved.

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
#define FLASH_PAGE_SIZE        128

#define DHCSR                  0xe000edf0
#define DHCSR_DEBUGEN          (1 << 0)
#define DHCSR_HALT             (1 << 1)
#define DHCSR_DBGKEY           (0xa05f << 16)

#define DEMCR                  0xe000edfc
#define DEMCR_VC_CORERESET     (1 << 0)

#define AIRCR                  0xe000ed0c
#define AIRCR_VECTKEY          (0x05fa << 16)
#define AIRCR_SYSRESETREQ      (1 << 2)

#define FLASH_ACR              0x40022000
#define FLASH_KEYR             0x40022008
#define FLASH_OPTKEYR          0x4002200c
#define FLASH_SR               0x40022010
#define FLASH_CR               0x40022014
#define FLASH_OPTR             0x40022020
#define FLASH_SDKR             0x40022024
#define FLASH_BTCR             0x40022028
#define FLASH_WRPR             0x4002202c

#define DBG_IDCODE             0x40015800

#define FLASH_KEYR_KEY1        0x45670123
#define FLASH_KEYR_KEY2        0xcdef89ab

#define FLASH_OPTKEYR_KEY1     0x08192a3b
#define FLASH_OPTKEYR_KEY2     0x4c5d6e7f

#define FLASH_SR_EOP           (1 << 0)
#define FLASH_SR_WRPERR        (1 << 4)
#define FLASH_SR_OPTVERR       (1 << 15)
#define FLASH_SR_BSY           (1 << 16)

#define FLASH_SR_ALL_ERRORS    (FLASH_SR_WRPERR | FLASH_SR_OPTVERR)

#define FLASH_CR_PG            (1 << 0)
#define FLASH_CR_PER           (1 << 1)
#define FLASH_CR_MER           (1 << 2)
#define FLASH_CR_SER           (1 << 11)
#define FLASH_CR_OPTSTRT       (1 << 17)
#define FLASH_CR_PGSTRT        (1 << 19)
#define FLASH_CR_EOPIE         (1 << 24)
#define FLASH_CR_ERRIE         (1 << 25)
#define FLASH_CR_OBL_LAUNCH    (1 << 27)
#define FLASH_CR_OPT_LOCK      (1 << 30)
#define FLASH_CR_LOCK          (1 << 31)

#define FLASH_OPTR_RDP_MASK    0x000000ff

#define OPTIONS_OPTR           0x1fff0e80
#define OPTIONS_SDKR           0x1fff0e84
#define OPTIONS_BOOT           0x1fff0e88 // Only on PY32F002B
#define OPTIONS_WRPR           0x1fff0e8c

#define STATUS_INTERVAL        32 // pages

/*- Types -------------------------------------------------------------------*/
typedef struct
{
  uint32_t  idcode;
  char      *family;
  char      *name;
  uint32_t  flash_size;
} device_t;

/*- Variables ---------------------------------------------------------------*/
static device_t devices[] =
{
  { 0x60001000, "py32f0", "PY32F002Axx5", 20*1024 },
  { 0x20220064, "py32f0", "PY32F002Bxx5", 24*1024 },
};

static device_t target_device;
static target_options_t target_options;

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
static void flash_wait_done(void)
{
  while (dap_read_word(FLASH_SR) & FLASH_SR_BSY);

  uint32_t sr = dap_read_word(FLASH_SR);

  if (sr & FLASH_SR_ALL_ERRORS)
    error_exit("flash operation failed. FLASH_SR = 0x%08x", sr);
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

  idcode = dap_read_word(DBG_IDCODE);

  for (int i = 0; i < ARRAY_SIZE(devices); i++)
  {
    if (devices[i].idcode != idcode)
      continue;

    verbose("Target: %s\n", devices[i].name);

    target_device = devices[i];
    target_options = *options;

    target_check_options(&target_options, target_device.flash_size, FLASH_PAGE_SIZE);

    locked = (0xaa != (dap_read_word(OPTIONS_OPTR) & FLASH_OPTR_RDP_MASK));

    if (locked && !options->unlock)
      error_exit("target is locked, unlock is necessary");

    dap_write_word_req(FLASH_KEYR, FLASH_KEYR_KEY1);
    dap_write_word_req(FLASH_KEYR, FLASH_KEYR_KEY2);
    dap_write_word_req(FLASH_OPTKEYR, FLASH_OPTKEYR_KEY1);
    dap_write_word_req(FLASH_OPTKEYR, FLASH_OPTKEYR_KEY2);
    dap_write_word_req(FLASH_CR, 0);
    dap_transfer();

    check(0 == dap_read_word(FLASH_CR), "Failed to unlock the flash for write operation. Try to power cycle the target.");

    return;
  }

  error_exit("unknown target device (DBG_IDCODE = 0x%08x)", idcode);
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
  dap_write_word_req(FLASH_CR, FLASH_CR_MER);
  dap_write_word_req(FLASH_ADDR, 0);
  dap_transfer();

  flash_wait_done();

  dap_write_word(FLASH_CR, 0);
}

//-----------------------------------------------------------------------------
static void target_lock(void)
{
  uint32_t value = (dap_read_word(OPTIONS_OPTR) & 0xff00) | 0xee;

  dap_write_word_req(FLASH_OPTR, value);
  dap_write_word_req(FLASH_CR, FLASH_CR_OPTSTRT);
  dap_write_word_req(0x40022080, 0);
  dap_transfer();

  flash_wait_done();
}

//-----------------------------------------------------------------------------
static void target_unlock(void)
{
  uint32_t value = (dap_read_word(OPTIONS_OPTR) & 0xff00) | 0xaa;

  dap_write_word_req(FLASH_OPTR, value);
  dap_write_word_req(FLASH_CR, FLASH_CR_OPTSTRT);
  dap_write_word_req(0x40022080, 0);
  dap_transfer();

  flash_wait_done();
}

//-----------------------------------------------------------------------------
static void target_program(void)
{
  uint32_t addr = FLASH_ADDR + target_options.offset;
  uint32_t offs = 0;
  uint32_t number_of_pages;
  uint32_t *buf = (uint32_t *)target_options.file_data;
  uint32_t size = target_options.file_size;
  int word_size = FLASH_PAGE_SIZE / sizeof(uint32_t);

  number_of_pages = (size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;

  for (uint32_t page = 0; page < number_of_pages; page++)
  {
    // Erase Page
    dap_write_word_req(FLASH_CR, FLASH_CR_PER);
    dap_write_word_req(addr, 0);
    dap_transfer();

    flash_wait_done();

    // Program Page
    dap_write_word(FLASH_CR, FLASH_CR_PG);

    for (int i = 0; i < word_size; i++)
    {
      if (i == (word_size-1))
        dap_write_word(FLASH_CR, FLASH_CR_PG | FLASH_CR_PGSTRT);

      dap_write_word_req(addr, buf[offs]);
      addr += sizeof(uint32_t);
      offs++;
    }

    dap_transfer();

    flash_wait_done();

    if (0 == (page % STATUS_INTERVAL))
      verbose(".");
  }

  dap_write_word(FLASH_CR, 0);
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
  int page = 0;

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

    if (0 == (page++ % STATUS_INTERVAL))
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
  int page = 0;

  while (size)
  {
    dap_read_block(addr, &buf[offs], FLASH_PAGE_SIZE);

    addr += FLASH_PAGE_SIZE;
    offs += FLASH_PAGE_SIZE;
    size -= FLASH_PAGE_SIZE;

    if (0 == (page++ % STATUS_INTERVAL))
      verbose(".");
  }

  save_file(target_options.name, buf, target_options.size);
}

//-----------------------------------------------------------------------------
static int target_fuse_read(int section, uint8_t *data)
{
  uint32_t value;

  if (0 == section)
    value = dap_read_word(OPTIONS_OPTR);
  else if (1 == section)
    value = dap_read_word(OPTIONS_SDKR);
  else if (2 == section)
    value = dap_read_word(OPTIONS_BOOT);
  else if (3 == section)
    value = dap_read_word(OPTIONS_WRPR);
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
  uint32_t value = (data[1] << 8) | data[0];

  if (0 == section)
    dap_write_word_req(FLASH_OPTR, value);
  else if (1 == section)
    dap_write_word_req(FLASH_SDKR, value);
  else if (2 == section)
    dap_write_word_req(FLASH_BTCR, value);
  else if (3 == section)
    dap_write_word_req(FLASH_WRPR, value);
  else
    error_exit("internal: incorrect section index in target_fuse_write()");

  dap_write_word_req(FLASH_CR, FLASH_CR_OPTSTRT);
  dap_write_word_req(0x40022080, 0);
  dap_transfer();

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
  "Fuses:\n"
  "  The option bytes are represented by the following sections (32-bits each):\n"
  "    0 - OPTR (User Options)\n"
  "    1 - SDKR (Software Design Kit Protection)\n"
  "    2 - BTCR (Boot Control, only for PY32F002B)\n"
  "    3 - WRPR (Write Protection)\n";

//-----------------------------------------------------------------------------
target_ops_t target_puya_py32f0_ops =
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


