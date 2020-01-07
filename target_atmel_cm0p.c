/*
 * Copyright (c) 2013-2019, Alex Taradov <alex@taradov.com>
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
#define USER_ROW_SIZE          256

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
#define DSU_STATUSA_DONE       (1 << 0)
#define DSU_STATUSA_CRSTEXT    (1 << 1)
#define DSU_STATUSB_PROT       (1 << 0)

#define SERIAL_NUMBER_0        0x0080a00c
#define SERIAL_NUMBER_1        0x0080a040
#define SERIAL_NUMBER_2        0x0080a044
#define SERIAL_NUMBER_3        0x0080a048

#define NVMCTRL_CTRLA          0x41004000
#define NVMCTRL_CTRLB          0x41004004
#define NVMCTRL_PARAM          0x41004008
#define NVMCTRL_INTFLAG        0x41004014
#define NVMCTRL_STATUS         0x41004018
#define NVMCTRL_ADDR           0x4100401c

#define NVMCTRL_INTFLAG_READY  (1 << 0)

#define NVMCTRL_CMD_ER         0xa502
#define NVMCTRL_CMD_WP         0xa504
#define NVMCTRL_CMD_EAR        0xa505
#define NVMCTRL_CMD_WAP        0xa506
#define NVMCTRL_CMD_WL         0xa50f
#define NVMCTRL_CMD_UR         0xa541
#define NVMCTRL_CMD_PBC        0xa544
#define NVMCTRL_CMD_SSB        0xa545

#define DEVICE_ID_MASK         0xfffff0ff
#define DEVICE_REV_SHIFT       8
#define DEVICE_REV_MASK        0xf

/*- Types -------------------------------------------------------------------*/
typedef struct
{
  uint32_t  dsu_did;
  char      *family;
  char      *name;
  uint32_t  flash_size;
} device_t;

/*- Variables ---------------------------------------------------------------*/
static device_t devices[] =
{
  { 0x10040007, "samd09", "SAM D09C13A",    8*1024 },
  { 0x10040000, "samd09", "SAM D09D14AM",  16*1024 },
  { 0x10020007, "samd10", "SAM D10C13A",    8*1024 },
  { 0x10020001, "samd10", "SAM D10D13AM",   8*1024 },
  { 0x10020004, "samd10", "SAM D10D13AS",   8*1024 },
  { 0x10020006, "samd10", "SAM D10C14A",   16*1024 },
  { 0x10020000, "samd10", "SAM D10D14AM",  16*1024 },
  { 0x10020003, "samd10", "SAM D10D14AS",  16*1024 },
  { 0x10020009, "samd10", "SAM D10D14AU",  16*1024 },
  { 0x10030006, "samd11", "SAM D11C14A",   16*1024 },
  { 0x10030000, "samd11", "SAM D11D14AM",  16*1024 },
  { 0x10030003, "samd11", "SAM D11D14AS",  16*1024 },
  { 0x10030009, "samd11", "SAM D11D14AU",  16*1024 },
  { 0x1000100d, "samd20", "SAM D20E15A",   32*1024 },
  { 0x1000100a, "samd20", "SAM D20E18A",  256*1024 },
  { 0x10001000, "samd20", "SAM D20J18A",  256*1024 },
  { 0x1001000d, "samd21", "SAM D21E15A",   32*1024 },
  { 0x1001000b, "samd21", "SAM D21E17A",  128*1024 },
  { 0x10011020, "samd21", "SAM D21J16B",   64*1024 },
  { 0x10012092, "samd21", "SAM D21J17D",  128*1024 },
  { 0x10010000, "samd21", "SAM D21J18A",  256*1024 },
  { 0x1001000a, "samd21", "SAM D21E18A",  256*1024 },
  { 0x10010006, "samd21", "SAM D21G17A",  128*1024 },
  { 0x10010005, "samd21", "SAM D21G18A",  256*1024 },
  { 0x11010000, "samc21", "SAM C21J18A",  256*1024 },
  { 0x11010005, "samc21", "SAM C21G18A",  256*1024 },
  { 0x1101000a, "samc21", "SAM C21E18A",  256*1024 },
  { 0x11011000, "samc21", "SAM C21N18A",  256*1024 },
  { 0x10810019, "saml21", "SAM L21E18B",  256*1024 },
  { 0x10810000, "saml21", "SAM L21J18A",  256*1024 },
  { 0x1081000f, "saml21", "SAM L21J18B",  256*1024 },
  { 0x10810014, "saml21", "SAM L21G18B",  256*1024 },
  { 0x10820000, "saml22", "SAM L22N18A",  256*1024 },
  { 0x10010019, "samr21", "SAM R21G18",   256*1024 },
  { 0x1001001c, "samr21", "SAM R21E18A",  256*1024 },
  { 0x1081001e, "samr30", "SAM R30G18A",  256*1024 },
  { 0x1081001f, "samr30", "SAM R30E18A",  256*1024 },
  { 0x10810028, "samr34", "SAM R34J18B",  256*1024 },
  { 0x10810029, "samr34", "SAM R34J17B",  128*1024 },
  { 0x1081002A, "samr34", "SAM R34J16B",   64*1024 },
  { 0x1081002B, "samr35", "SAM R35J18B",  256*1024 },
  { 0x1081002C, "samr35", "SAM R35J17B",  128*1024 },
  { 0x1081002D, "samr35", "SAM R35J16B",   64*1024 },
  { 0x10011064, "samda1", "SAM DA1J16B",   64*1024 },
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
  uint32_t serial_number[4];
  bool locked;

  reset_with_extension();

  dsu_did = dap_read_word(DSU_DID);
  id = dsu_did & DEVICE_ID_MASK;
  rev = (dsu_did >> DEVICE_REV_SHIFT) & DEVICE_REV_MASK;

  for (int i = 0; i < ARRAY_SIZE(devices); i++)
  {
    if (devices[i].dsu_did != id)
      continue;

    verbose("Target: %s (Rev %c)\n", devices[i].name, 'A' + rev);

    serial_number[0] = dap_read_word(SERIAL_NUMBER_0);
    serial_number[1] = dap_read_word(SERIAL_NUMBER_1);
    serial_number[2] = dap_read_word(SERIAL_NUMBER_2);
    serial_number[3] = dap_read_word(SERIAL_NUMBER_3);

    verbose("Serial number: %08x %08x %08x %08x\n",
        serial_number[0], serial_number[1], serial_number[2], serial_number[3]);

    target_device = devices[i];
    target_options = *options;

    target_check_options(&target_options, devices[i].flash_size, FLASH_ROW_SIZE);

    locked = dap_read_byte(DSU_STATUSB) & DSU_STATUSB_PROT;

    if (locked && !options->unlock)
      error_exit("target is locked, unlock is necessary");

    if (!locked)
      finish_reset();

    return;
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
  dap_write_byte(DSU_STATUSA, DSU_STATUSA_DONE);
  dap_write_byte(DSU_CTRL, DSU_CTRL_CE);
  sleep_ms(100);
  while (0 == (dap_read_byte(DSU_STATUSA) & DSU_STATUSA_DONE));

  reset_with_extension();
  finish_reset();
}

//-----------------------------------------------------------------------------
static void target_lock(void)
{
  dap_write_half(NVMCTRL_CTRLA, NVMCTRL_CMD_SSB); // Set Security Bit
}

//-----------------------------------------------------------------------------
static void target_program(void)
{
  uint32_t addr = FLASH_ADDR + target_options.offset;
  uint32_t offs = 0;
  uint32_t number_of_rows;
  uint8_t *buf = target_options.file_data;
  uint32_t size = target_options.file_size;

  number_of_rows = (size + FLASH_ROW_SIZE - 1) / FLASH_ROW_SIZE;

  dap_write_word(NVMCTRL_CTRLB, 0); // Enable automatic write

  for (uint32_t row = 0; row < number_of_rows; row++)
  {
    dap_write_word(NVMCTRL_ADDR, addr >> 1);

    dap_write_half(NVMCTRL_CTRLA, NVMCTRL_CMD_UR); // Unlock Region
    while (0 == (dap_read_byte(NVMCTRL_INTFLAG) & NVMCTRL_INTFLAG_READY));

    dap_write_half(NVMCTRL_CTRLA, NVMCTRL_CMD_ER); // Erase Row
    while (0 == (dap_read_byte(NVMCTRL_INTFLAG) & NVMCTRL_INTFLAG_READY));

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
  if (section > 0)
    return 0;

  dap_read_block(USER_ROW_ADDR, data, USER_ROW_SIZE);

  return USER_ROW_SIZE;
}

//-----------------------------------------------------------------------------
static void target_fuse_write(int section, uint8_t *data)
{
  check(0 == section, "internal: incorrect section index in target_fuse_write()");

  dap_write_word(NVMCTRL_CTRLB, 0);
  dap_write_word(NVMCTRL_ADDR, USER_ROW_ADDR >> 1);
  dap_write_half(NVMCTRL_CTRLA, NVMCTRL_CMD_EAR);
  while (0 == (dap_read_byte(NVMCTRL_INTFLAG) & NVMCTRL_INTFLAG_READY));

  dap_write_block(USER_ROW_ADDR, data, USER_ROW_SIZE);
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
  "  This device has one fuses section, which represents a complete User Row (256 bytes).\n";

//-----------------------------------------------------------------------------
target_ops_t target_atmel_cm0p_ops =
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

