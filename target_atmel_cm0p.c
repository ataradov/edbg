// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2013-2019, Alex Taradov <alex@taradov.com>. All rights reserved.

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

#define STATUS_INTERVAL        32 // rows

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
  { 0x10001005, "samd20", "SAM D20G18A",  256*1024 },
  { 0x10001000, "samd20", "SAM D20J18A",  256*1024 },

  { 0x10010003, "samd21", "SAM D21J15A",   32*1024 },
  { 0x10010008, "samd21", "SAM D21G15A",   32*1024 },
  { 0x1001000d, "samd21", "SAM D21E15A",   32*1024 },
  { 0x10011021, "samd21", "SAM D21J15B",   32*1024 },
  { 0x10011024, "samd21", "SAM D21G15B",   32*1024 },
  { 0x10011027, "samd21", "SAM D21E15B",   32*1024 },
  { 0x10011056, "samd21", "SAM D21E15BU",  32*1024 },
  { 0x1001103f, "samd21", "SAM D21E15L",   32*1024 },
  { 0x10011063, "samd21", "SAM D21E15CU",  32*1024 },
  { 0x10010002, "samd21", "SAM D21J16A",   64*1024 },
  { 0x10010007, "samd21", "SAM D21G16A",   64*1024 },
  { 0x1001000c, "samd21", "SAM D21E16A",   64*1024 },
  { 0x10011020, "samd21", "SAM D21J16B",   64*1024 },
  { 0x10011023, "samd21", "SAM D21G16B",   64*1024 },
  { 0x10011026, "samd21", "SAM D21E16B",   64*1024 },
  { 0x10011055, "samd21", "SAM D21E16BU",  64*1024 },
  { 0x10011057, "samd21", "SAM D21G16L",   64*1024 },
  { 0x1001103e, "samd21", "SAM D21E16L",   64*1024 },
  { 0x10011062, "samd21", "SAM D21E16CU",  64*1024 },
  { 0x10010001, "samd21", "SAM D21J17A",  128*1024 },
  { 0x10010006, "samd21", "SAM D21G17A",  128*1024 },
  { 0x10010010, "samd21", "SAM D21G17AU", 128*1024 },
  { 0x1001000b, "samd21", "SAM D21E17A",  128*1024 },
  { 0x10012094, "samd21", "SAM D21E17D",  128*1024 },
  { 0x10012095, "samd21", "SAM D21E17DU", 128*1024 },
  { 0x10012097, "samd21", "SAM D21E17L",  128*1024 },
  { 0x10012093, "samd21", "SAM D21G17D",  128*1024 },
  { 0x10012096, "samd21", "SAM D21G17L",  128*1024 },
  { 0x10012092, "samd21", "SAM D21J17D",  128*1024 },
  { 0x10010000, "samd21", "SAM D21J18A",  256*1024 },
  { 0x10010005, "samd21", "SAM D21G18A",  256*1024 },
  { 0x1001000f, "samd21", "SAM D21G18AU", 256*1024 },
  { 0x1001000a, "samd21", "SAM D21E18A",  256*1024 },

  { 0x10011031, "samda1", "SAM DA1E14A",   16*1024 },
  { 0x1001102e, "samda1", "SAM DA1G14A",   16*1024 },
  { 0x1001102b, "samda1", "SAM DA1J14A",   16*1024 },
  { 0x1001106c, "samda1", "SAM DA1E14B",   16*1024 },
  { 0x10011069, "samda1", "SAM DA1G14B",   16*1024 },
  { 0x10011066, "samda1", "SAM DA1J14B",   16*1024 },
  { 0x10011030, "samda1", "SAM DA1E15A",   32*1024 },
  { 0x1001102d, "samda1", "SAM DA1G15A",   32*1024 },
  { 0x1001102a, "samda1", "SAM DA1J15A",   32*1024 },
  { 0x1001106b, "samda1", "SAM DA1E15B",   32*1024 },
  { 0x10011068, "samda1", "SAM DA1G15B",   32*1024 },
  { 0x10011065, "samda1", "SAM DA1J15B",   32*1024 },
  { 0x1001102f, "samda1", "SAM DA1E16A",   64*1024 },
  { 0x1001102c, "samda1", "SAM DA1G16A",   64*1024 },
  { 0x10011029, "samda1", "SAM DA1J16A",   64*1024 },
  { 0x1001106a, "samda1", "SAM DA1E16B",   64*1024 },
  { 0x10011067, "samda1", "SAM DA1G16B",   64*1024 },
  { 0x10011064, "samda1", "SAM DA1J16B",   64*1024 },

  { 0x1100000d, "samc20", "SAM C20E15A",   32*1024 },
  { 0x11000008, "samc20", "SAM C20G15A",   32*1024 },
  { 0x11000003, "samc20", "SAM C20J15A",   32*1024 },
  { 0x1100000c, "samc20", "SAM C20E16A",   64*1024 },
  { 0x11000007, "samc20", "SAM C20G16A",   64*1024 },
  { 0x11000002, "samc20", "SAM C20J16A",   64*1024 },
  { 0x1100000b, "samc20", "SAM C20E17A",  128*1024 },
  { 0x11000006, "samc20", "SAM C20G17A",  128*1024 },
  { 0x11000001, "samc20", "SAM C20J17A",  128*1024 },
  { 0x11000010, "samc20", "SAM C20J17AU", 128*1024 },
  { 0x11001021, "samc20", "SAM C20N17A",  128*1024 },
  { 0x1100000a, "samc20", "SAM C20E18A",  256*1024 },
  { 0x11000005, "samc20", "SAM C20G18A",  256*1024 },
  { 0x11000000, "samc20", "SAM C20J18A",  256*1024 },
  { 0x1100000f, "samc20", "SAM C20J18AU", 256*1024 },
  { 0x11001020, "samc20", "SAM C20N18A",  256*1024 },

  { 0x1101000d, "samc21", "SAM C21E15A",   32*1024 },
  { 0x11010008, "samc21", "SAM C21G15A",   32*1024 },
  { 0x11010003, "samc21", "SAM C21J15A",   32*1024 },
  { 0x1101000c, "samc21", "SAM C21E16A",   64*1024 },
  { 0x11010007, "samc21", "SAM C21G16A",   64*1024 },
  { 0x11010002, "samc21", "SAM C21J16A",   64*1024 },
  { 0x1101000b, "samc21", "SAM C21E17A",  128*1024 },
  { 0x11010006, "samc21", "SAM C21G17A",  128*1024 },
  { 0x11010001, "samc21", "SAM C21J17A",  128*1024 },
  { 0x11010010, "samc21", "SAM C21J17AU", 128*1024 },
  { 0x11011021, "samc21", "SAM C21N17A",  128*1024 },
  { 0x1101000a, "samc21", "SAM C21E18A",  256*1024 },
  { 0x11010005, "samc21", "SAM C21G18A",  256*1024 },
  { 0x11010000, "samc21", "SAM C21J18A",  256*1024 },
  { 0x1101000f, "samc21", "SAM C21J18AU", 256*1024 },
  { 0x11011020, "samc21", "SAM C21N18A",  256*1024 },

  { 0x1081001b, "saml21", "SAM L21E16B",  64*1024 },
  { 0x1081001a, "saml21", "SAM L21E17B",  128*1024 },
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

  { 0x11070000, "pic32cm_mc", "PIC32CM1216MC00032", 128*1024 },
  { 0x11070001, "pic32cm_mc", "PIC32CM6408MC00032",  64*1024 },
  { 0x11070006, "pic32cm_mc", "PIC32CM1216MC00048", 128*1024 },
  { 0x11070007, "pic32cm_mc", "PIC32CM6408MC00048",  64*1024 },

  { 0x1106000e, "pic32cm_jh", "PIC32CM5164JH00100", 512*1024 },
  { 0x1106000f, "pic32cm_jh", "PIC32CM5164JH00064", 512*1024 },
  { 0x11060014, "pic32cm_jh", "PIC32CM5164JH00048", 512*1024 },
  { 0x11060015, "pic32cm_jh", "PIC32CM5164JH00032", 512*1024 },
  { 0x1106000d, "pic32cm_jh", "PIC32CM2532JH00100", 256*1024 },
  { 0x11060010, "pic32cm_jh", "PIC32CM2532JH00064", 256*1024 },
  { 0x11060013, "pic32cm_jh", "PIC32CM2532JH00048", 256*1024 },
  { 0x11060016, "pic32cm_jh", "PIC32CM2532JH00032", 256*1024 },

  { 0x11060000, "pic32cm_jh", "PIC32CM5164JH01100", 512*1024 },
  { 0x11060001, "pic32cm_jh", "PIC32CM5164JH01064", 512*1024 },
  { 0x11060002, "pic32cm_jh", "PIC32CM5164JH01048", 512*1024 },
  { 0x11060003, "pic32cm_jh", "PIC32CM5164JH01032", 512*1024 },
  { 0x11060004, "pic32cm_jh", "PIC32CM2532JH01100", 256*1024 },
  { 0x11060005, "pic32cm_jh", "PIC32CM2532JH01064", 256*1024 },
  { 0x11060006, "pic32cm_jh", "PIC32CM2532JH01048", 256*1024 },
  { 0x11060007, "pic32cm_jh", "PIC32CM2532JH01032", 256*1024 },
};

static device_t target_device;
static target_options_t target_options;

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
static void reset_with_extension(void)
{
  dap_reset_target_hw(0);
  sleep_ms(10);
  dap_reset_link();
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

  for (int i = 0; i < ARRAY_SIZE(devices); i++)
  {
    if (devices[i].dsu_did != id)
      continue;

    verbose("Target: %s (Rev %c)\n", devices[i].name, 'A' + rev);

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

    if (0 == (row % STATUS_INTERVAL))
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
  int row = 0;

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

    if (0 == (row++ % STATUS_INTERVAL))
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
  int row = 0;

  while (size)
  {
    dap_read_block(addr, &buf[offs], FLASH_ROW_SIZE);

    addr += FLASH_ROW_SIZE;
    offs += FLASH_ROW_SIZE;
    size -= FLASH_ROW_SIZE;

    if (0 == (row++ % STATUS_INTERVAL))
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

