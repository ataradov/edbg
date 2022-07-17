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
#define FLASH_PAGE_SIZE        2048
#define FLASH_ROW_SIZE         256

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
#define FLASH_ECCR             0x40022018
#define FLASH_OPTR             0x40022020
#define FLASH_PCROP1ASR        0x40022024
#define FLASH_PCROP1AER        0x40022028
#define FLASH_WRP1AR           0x4002202c
#define FLASH_WRP1BR           0x40022030
#define FLASH_PCROP1BSR        0x40022034
#define FLASH_PCROP1BER        0x40022038
#define FLASH_SECR             0x40022080

#define DBG_IDCODE             0x40015800
#define FLASH_SIZE_REG         0x1FFF75E0
#define PACKAGE_REG            0x1FFF7500

#define FLASH_KEYR_KEY1        0x45670123
#define FLASH_KEYR_KEY2        0xcdef89ab

#define FLASH_OPTKEYR_KEY1     0x08192a3b
#define FLASH_OPTKEYR_KEY2     0x4c5d6e7f

#define FLASH_SR_EOP           (1 << 0)
#define FLASH_SR_OPERR         (1 << 1)
#define FLASH_SR_PROGERR       (1 << 3)
#define FLASH_SR_WRPERR        (1 << 4)
#define FLASH_SR_PGAERR        (1 << 5)
#define FLASH_SR_SIZERR        (1 << 6)
#define FLASH_SR_PGSERR        (1 << 7)
#define FLASH_SR_MISSERR       (1 << 8)
#define FLASH_SR_FASTERR       (1 << 9)
#define FLASH_SR_RDERR         (1 << 14)
#define FLASH_SR_OPTVERR       (1 << 15)
#define FLASH_SR_BSY1          (1 << 16)

#define FLASH_SR_ALL_ERRORS    (FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR | \
    FLASH_SR_PGAERR | FLASH_SR_SIZERR | FLASH_SR_PGSERR | FLASH_SR_MISSERR | \
    FLASH_SR_FASTERR | FLASH_SR_RDERR | FLASH_SR_OPTVERR)

#define FLASH_CR_PG            (1 << 0)
#define FLASH_CR_PER           (1 << 1)
#define FLASH_CR_MER1          (1 << 2)
#define FLASH_CR_PNB(x)        ((x) << 3)
#define FLASH_CR_STRT          (1 << 16)
#define FLASH_CR_OPTSTRT       (1 << 17)
#define FLASH_CR_FSTPG         (1 << 18)
#define FLASH_CR_OBL_LAUNCH    (1 << 27)
#define FLASH_CR_SEC_PROT      (1 << 28)
#define FLASH_CR_OPT_LOCK      (1 << 30)
#define FLASH_CR_LOCK          (1 << 31)

#define FLASH_SIZE_REG_MASK    0x000000ff
#define FLASH_SIZE_REG_MULT    1024

#define FLASH_OPTR_RDP_MASK    0x000000ff

#define OPTIONS_OPTR           0x1fff7800
#define OPTIONS_PCROP1ASR      0x1fff7808
#define OPTIONS_PCROP1AER      0x1fff7810
#define OPTIONS_PCROP1BSR      0x1fff7828
#define OPTIONS_PCROP1BER      0x1fff7830
#define OPTIONS_WRP1AR         0x1fff7818
#define OPTIONS_WRP1BR         0x1fff7820
#define OPTIONS_SECR           0x1fff7870

#define OPTIONS_SIZE           4

#define DEVICE_ID_MASK         0x0000ffff

/*- Types -------------------------------------------------------------------*/
typedef struct
{
  uint32_t  idcode;
  char      *family;
  char      *name;
} device_t;

typedef struct
{
  uint32_t     optr;
  uint32_t     pcrop1asr;
  uint32_t     pcrop1aer;
  uint32_t     pcrop1bsr;
  uint32_t     pcrop1ber;
  uint32_t     wrp1ar;
  uint32_t     wrp1br;
  uint32_t     secr;
} fuse_options_t;

/*- Variables ---------------------------------------------------------------*/
static device_t devices[] =
{
  { 0x6460, "stm32g0", "STM32G071/81" },
  { 0x6466, "stm32g0", "STM32G031/41" },
};

static device_t target_device;
static target_options_t target_options;
//static fuse_options_t fuse_options;

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
static void flash_wait_done(void)
{
  uint32_t sr;

  while (dap_read_word(FLASH_SR) & FLASH_SR_BSY1);

  sr = dap_read_word(FLASH_SR);

  if (sr & FLASH_SR_ALL_ERRORS)
    error_exit("flash operation failed. FLASH_SR = 0x%08x", sr);
}

//-----------------------------------------------------------------------------
static void target_select(target_options_t *options)
{
  uint32_t idcode, flash_size;
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
    if (devices[i].idcode != (idcode & DEVICE_ID_MASK))
      continue;

    verbose("Target: %s\n", devices[i].name);

    target_device = devices[i];
    target_options = *options;

    flash_size = (dap_read_word(FLASH_SIZE_REG) & FLASH_SIZE_REG_MASK) * FLASH_SIZE_REG_MULT;

    target_check_options(&target_options, flash_size, FLASH_PAGE_SIZE);

    locked = (0xaa != (dap_read_word(OPTIONS_OPTR) & FLASH_OPTR_RDP_MASK));

    if (locked && !options->unlock)
      error_exit("target is locked, unlock is necessary");

    dap_write_word(FLASH_KEYR, FLASH_KEYR_KEY1);
    dap_write_word(FLASH_KEYR, FLASH_KEYR_KEY2);
    dap_write_word(FLASH_OPTKEYR, FLASH_OPTKEYR_KEY1);
    dap_write_word(FLASH_OPTKEYR, FLASH_OPTKEYR_KEY2);
    dap_write_word(FLASH_CR, 0);

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
  dap_write_word(FLASH_CR, FLASH_CR_MER1);
  dap_write_word(FLASH_CR, FLASH_CR_MER1 | FLASH_CR_STRT);
  flash_wait_done();
  dap_write_word(FLASH_CR, 0);
}

//-----------------------------------------------------------------------------
static void target_lock(void)
{
  error_exit("target_lock() is not implemented yet");
}

//-----------------------------------------------------------------------------
static void target_unlock(void)
{
  error_exit("target_unlock() is not implemented yet");
}

//-----------------------------------------------------------------------------
static void target_program(void)
{
  uint32_t addr = FLASH_ADDR + target_options.offset;
  uint32_t offs = 0;
  uint32_t start_page, number_of_pages;
  uint8_t *buf = target_options.file_data;
  uint32_t size = target_options.file_size;

  start_page = target_options.offset / FLASH_PAGE_SIZE;
  number_of_pages = (size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;

  for (uint32_t page = 0; page < number_of_pages; page++)
  {
    // Erase Page
    dap_write_word(FLASH_CR, FLASH_CR_PER | FLASH_CR_PNB(start_page + page));
    dap_write_word(FLASH_CR, FLASH_CR_PER | FLASH_CR_PNB(start_page + page) | FLASH_CR_STRT);
    flash_wait_done();

    dap_write_word(FLASH_CR, FLASH_CR_PG);

    // Program Page
    for (int i = 0; i < (FLASH_PAGE_SIZE / FLASH_ROW_SIZE); i++)
    {
      dap_write_block(addr, &buf[offs], FLASH_ROW_SIZE);
      addr += FLASH_ROW_SIZE;
      offs += FLASH_ROW_SIZE;
    }

    verbose(".");

    flash_wait_done();
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

    if (0 == (offs % FLASH_PAGE_SIZE))
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
  error_exit("target_fuse_read() is not implemented yet");
  (void)section;
  (void)data;
  return 0;
}

//-----------------------------------------------------------------------------
static void target_fuse_write(int section, uint8_t *data)
{
  error_exit("target_fuse_write() is not implemented yet");
  (void)section;
  (void)data;
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
  "    0 - OPTR (option register)\n"
  "    1 - PCROP1ASR (PCROP area A start address register)\n"
  "    2 - PCROP1AER (PCROP area A end address register)\n"
  "    3 - PCROP1BSR (PCROP area B start address register)\n"
  "    4 - PCROP1BER (PCROP area B end address register)\n"
  "    5 - WRP1AR (WRP area A address register)\n"
  "    6 - WRP1BR (WRP area B address register)\n"
  "    7 - SECR (security register)\n";

//-----------------------------------------------------------------------------
target_ops_t target_st_stm32g0_ops =
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


