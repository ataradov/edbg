// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021, Alex Taradov <alex@taradov.com>. All rights reserved.

/*- Includes ----------------------------------------------------------------*/
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "target.h"
#include "edbg.h"
#include "dap.h"

/*- Definitions -------------------------------------------------------------*/
enum
{
  CMD_IDCODE_PUB               = 0xe0,
  CMD_ISC_ENABLE_X             = 0x74,
  CMD_ISC_ENABLE               = 0xc6,
  CMD_LSC_CHECK_BUSY           = 0xf0,
  CMD_LSC_READ_STATUS          = 0x3c,
  CMD_ISC_ERASE                = 0x0e,
  CMD_LSC_ERASE_TAG            = 0xcb,
  CMD_LSC_INIT_ADDRESS         = 0x46,
  CMD_LSC_WRITE_ADDRESS        = 0xb4,
  CMD_LSC_PROG_INCR_NV         = 0x70,
  CMD_LSC_INIT_ADDR_UFM        = 0x47,
  CMD_LSC_PROG_TAG             = 0xc9,
  CMD_ISC_PROGRAM_USERCODE     = 0xc2,
  CMD_USERCODE                 = 0xc0,
  CMD_LSC_PROG_FEATURE         = 0xe4,
  CMD_LSC_READ_FEATURE         = 0xe7,
  CMD_LSC_PROG_FEABITS         = 0xf8,
  CMD_LSC_READ_FEABITS         = 0xfb,
  CMD_LSC_READ_INCR_NV         = 0x73,
  CMD_LSC_READ_UFM             = 0xca,
  CMD_ISC_PROGRAM_DONE         = 0x5e,
  CMD_LSC_PROG_OTP             = 0xf9,
  CMD_LSC_READ_OTP             = 0xfa,
  CMD_ISC_DISABLE              = 0x26,
  CMD_ISC_NOOP                 = 0xff,
  CMD_LSC_REFRESH              = 0x79,
  CMD_ISC_PROGRAM_SECURITY     = 0xce,
  CMD_ISC_PROGRAM_SECPLUS      = 0xcf,
  CMD_UIDCODE_PUB              = 0x19,
  CMD_LSC_BITSTREAM_BURST      = 0x7a,
};

#define ISC_ENABLE_SRAM        0x00
#define ISC_ENABLE_FLASH       0x08

#define ISC_ERASE_SRAM         (1 << 0)
#define ISC_ERASE_FEATURE      (1 << 1)
#define ISC_ERASE_CFG          (1 << 2)
#define ISC_ERASE_UFM          (1 << 3)
#define ISC_ERASE_ALL          (ISC_ERASE_SRAM | ISC_ERASE_FEATURE | ISC_ERASE_CFG | ISC_ERASE_UFM)
#define ISC_ERASE_ALL_NV       (ISC_ERASE_FEATURE | ISC_ERASE_CFG | ISC_ERASE_UFM)

#define STATUS_BUSY            (1 << 12)
#define STATUS_FAIL            (1 << 13)

#define FLASH_ROW_SIZE         128 // bits

#define MAX_CONFIG_SIZE        (2*1024*1024)
#define MAX_FILE_SIZE          (MAX_CONFIG_SIZE*8)
#define MAX_CHAIN_COUNT        5
#define IR_LENGTH              8

/*- Types -------------------------------------------------------------------*/
typedef struct
{
  uint32_t  idcode;
  char      *family;
  char      *name;
} device_t;

typedef struct
{
  uint8_t   *config;
  int       config_size;
  uint16_t  feabits;
  uint64_t  feature;
} jed_file_t;

/*- Variables ---------------------------------------------------------------*/
static device_t devices[] =
{
  { 0x012b9043, "lcmxo2", "LCMXO2-640HC" },
  { 0x012ba043, "lcmxo2", "LCMXO2-1200HC" },
  { 0x012bb043, "lcmxo2", "LCMXO2-2000HC" },
};

static device_t target_device;
static target_options_t target_options;

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
static bool bitstream_valid(uint8_t *data, int size)
{
  if (size < 1024)
    return false;

  if (NULL == mem_find(data, 1024, (uint8_t *)target_device.name, strlen(target_device.name)))
    return false;

  return true;
}

//-----------------------------------------------------------------------------
static void parse_jed_file(jed_file_t *file, uint8_t *data, int size)
{
  static const char *start_text = "L000000";
  static const char *fr_text = "NOTE FEATURE_ROW*";
  uint8_t  *ptr;
  int bit_count = 0;
  int fr_bit_count = 0;
  int offset;

  // This is a very primitive parser. It expects a fixed format and will fail
  // if it finds something unexpected. It will also ignore EBR initialization data.

  if (!bitstream_valid(data, size))
    error_exit("malformed JED file: device signature not found");

  ptr = mem_find(data, size, (uint8_t *)start_text, strlen(start_text));

  if (NULL == ptr)
    error_exit("malformed JED file: no 'L000000' found");

  offset = ptr - data + strlen(start_text);

  memset(file->config, 0, file->config_size);

  for (; offset < size; offset++)
  {
    if (data[offset] == '*')
      break;

    if (data[offset] == '0' || data[offset] == '1')
    {
      file->config[bit_count / 8] |= ((data[offset] - '0') << (bit_count % 8));
      bit_count++;
      check(bit_count < (file->config_size*8), "malformed JED file: configuration data is too big");
    }
  }

  if (offset == size)
    error_exit("malformed JED file: no field terminator found");

  if (bit_count % FLASH_ROW_SIZE)
    error_exit("malformed JED file: size of the configuration data must be a multiple of 128");

  ptr = mem_find(data, size, (uint8_t *)fr_text, strlen(fr_text));

  if (NULL == ptr)
    error_exit("malformed JED file: no feature row found");

  offset = ptr - data + strlen(fr_text);

  file->feature = 0;
  file->feabits = 0;

  for (; offset < size; offset++)
  {
    if (data[offset] == '*')
      break;

    if (data[offset] == '0' || data[offset] == '1')
    {
      int bit = data[offset] - '0';

      check(fr_bit_count < (64 + 16), "malformed JED file: feature row data is too big");

      if (fr_bit_count < 64)
        file->feature |= ((uint64_t)bit << fr_bit_count);
      else
        file->feabits |= (bit << (fr_bit_count-64));

      fr_bit_count++;
    }
  }

  if (offset == size)
    error_exit("malformed JED file: no field terminator found");

  if (fr_bit_count != (64 + 16))
    error_exit("malformed JED file: invalid feature row size");

  file->config_size = bit_count;
}

//-----------------------------------------------------------------------------
static void target_select(target_options_t *options)
{
  uint32_t chain[MAX_CHAIN_COUNT];
  int chain_count;

  dap_connect(DAP_INTERFACE_JTAG);

  chain_count = dap_jtag_scan_chain(chain, MAX_CHAIN_COUNT);

  verbose("Detected JTAG chain:\n");
  for (int i = 0; i < chain_count; i++)
    verbose("  %d: 0x%08x\n", i, chain[i]);

  if (chain_count == 0)
    error_exit("no devices detected in the JTAG chain");

  if (chain_count > 1)
    error_exit("more than one device detected in the JTAG chain");

  for (int i = 0; i < ARRAY_SIZE(devices); i++)
  {
    if (devices[i].idcode != chain[0])
      continue;

    verbose("Target: %s\n", devices[i].name);

    target_device = devices[i];
    target_options = *options;

    return;
  }

  error_exit("unknown target device (IDCODE = 0x%08x)", chain[0]);
}

//-----------------------------------------------------------------------------
static void poll_busy_flag(void)
{
  uint32_t status = 0;
  uint8_t busy = 1;

  while (busy)
  {
    dap_jtag_write_ir(CMD_LSC_CHECK_BUSY, IR_LENGTH);
    dap_jtag_read_dr(&busy, 1);
  }

  dap_jtag_write_ir(CMD_LSC_READ_STATUS, IR_LENGTH);
  dap_jtag_read_dr((uint8_t *)&status, 32);
  dap_jtag_idle(8);

  if (status & STATUS_BUSY)
    error_exit("poll_busy_flag(): busy");

  if (status & STATUS_FAIL)
    error_exit("poll_busy_flag(): fail");
}

//-----------------------------------------------------------------------------
static void target_deselect(void)
{
  dap_jtag_write_ir(CMD_ISC_PROGRAM_DONE, IR_LENGTH);
  dap_jtag_idle(1000);
  poll_busy_flag();

  dap_jtag_write_ir(CMD_ISC_DISABLE, IR_LENGTH);
  dap_jtag_idle(8);

  dap_jtag_write_ir(CMD_ISC_NOOP, IR_LENGTH);
  dap_jtag_idle(100);

  dap_jtag_write_ir(CMD_LSC_REFRESH, IR_LENGTH);
  dap_jtag_idle(8);

  dap_jtag_write_ir(CMD_ISC_NOOP, IR_LENGTH);
  dap_jtag_idle(100);

  dap_jtag_flush();
}

//-----------------------------------------------------------------------------
static void erase_sram(void)
{
  dap_jtag_write_ir(CMD_ISC_ENABLE, IR_LENGTH);
  dap_jtag_write_dr((uint8_t[]){ ISC_ENABLE_SRAM }, 8);
  dap_jtag_idle(8);

  dap_jtag_write_ir(CMD_ISC_ERASE, IR_LENGTH);
  dap_jtag_write_dr((uint8_t[]){ ISC_ERASE_SRAM }, 8);
  dap_jtag_idle(8);

  dap_jtag_write_ir(CMD_ISC_NOOP, IR_LENGTH);
}

//-----------------------------------------------------------------------------
static void target_erase(void)
{
  erase_sram();

  // Enable Flash
  dap_jtag_write_ir(CMD_ISC_ENABLE, IR_LENGTH);
  dap_jtag_write_dr((uint8_t[]){ ISC_ENABLE_FLASH }, 8);
  dap_jtag_idle(8);

  dap_jtag_write_ir(CMD_ISC_ERASE, IR_LENGTH);
  dap_jtag_write_dr((uint8_t[]){ ISC_ERASE_ALL_NV }, 8);
  dap_jtag_idle(8);

  poll_busy_flag();
}

//-----------------------------------------------------------------------------
static void target_lock(void)
{
  error_exit("locking is not supported for this target");
}

//-----------------------------------------------------------------------------
static void target_unlock(void)
{
  error_exit("unlocking is not supported for this target");
}

//-----------------------------------------------------------------------------
static void target_program(void)
{
  uint8_t *file_data;
  int file_size, row_count;
  jed_file_t jed;

  file_data = buf_alloc(MAX_FILE_SIZE);
  file_size = load_file(target_options.name, file_data, MAX_FILE_SIZE);

  jed.config = buf_alloc(MAX_CONFIG_SIZE);
  jed.config_size = MAX_CONFIG_SIZE;

  parse_jed_file(&jed, file_data, file_size);

  row_count = jed.config_size / FLASH_ROW_SIZE;

  if (!target_options.erase)
    target_erase();
  else
    erase_sram();

  // Enable Flash
  dap_jtag_write_ir(CMD_ISC_ENABLE, IR_LENGTH);
  dap_jtag_write_dr((uint8_t[]){ ISC_ENABLE_FLASH }, 8);
  dap_jtag_idle(8);

  // Program configuration data
  dap_jtag_write_ir(CMD_LSC_INIT_ADDRESS, IR_LENGTH);
  dap_jtag_idle(8);

  for (int row = 0; row < row_count; row++)
  {
    dap_jtag_write_ir(CMD_LSC_PROG_INCR_NV, IR_LENGTH);
    dap_jtag_write_dr(&jed.config[row * 16], FLASH_ROW_SIZE);
    dap_jtag_idle(1000);
    poll_busy_flag();

    if (row % 256 == 0)
      verbose(".");
  }

  verbose(",");

  dap_jtag_write_ir(CMD_LSC_INIT_ADDRESS, IR_LENGTH);
  dap_jtag_idle(8);

  // Program Feature Row
  dap_jtag_write_ir(CMD_LSC_PROG_FEATURE, IR_LENGTH);
  dap_jtag_write_dr((uint8_t *)&jed.feature, 64);
  dap_jtag_idle(8);

  poll_busy_flag();

  verbose(",");

  // Program and verify FEABITS
  dap_jtag_write_ir(CMD_LSC_PROG_FEABITS, IR_LENGTH);
  dap_jtag_write_dr((uint8_t *)&jed.feabits, 16);
  dap_jtag_idle(8);

  poll_busy_flag();

  buf_free(file_data);
  buf_free(jed.config);
}

//-----------------------------------------------------------------------------
static void target_verify(void)
{
  uint8_t *file_data;
  int file_size, row_count;
  jed_file_t jed;
  uint16_t feabits;
  uint64_t feature;

  file_data = buf_alloc(MAX_FILE_SIZE);
  file_size = load_file(target_options.name, file_data, MAX_FILE_SIZE);

  jed.config = buf_alloc(MAX_CONFIG_SIZE);
  jed.config_size = MAX_CONFIG_SIZE;

  parse_jed_file(&jed, file_data, file_size);

  row_count = jed.config_size / FLASH_ROW_SIZE;

  erase_sram();

  // Enable flash
  dap_jtag_write_ir(CMD_ISC_ENABLE, IR_LENGTH);
  dap_jtag_write_dr((uint8_t[]){ ISC_ENABLE_FLASH }, 8);
  dap_jtag_idle(8);

  // Verify configuration data
  dap_jtag_write_ir(CMD_LSC_INIT_ADDRESS, IR_LENGTH);
  dap_jtag_idle(8);

  dap_jtag_write_ir(CMD_LSC_READ_INCR_NV, IR_LENGTH);
  dap_jtag_idle(8);

  for (int row = 0; row < row_count; row++)
  {
    uint8_t tmp[16];
    dap_jtag_read_dr(tmp, FLASH_ROW_SIZE);
    dap_jtag_idle(8);

    if (memcmp(tmp, &jed.config[row * 16], 16))
      error_exit("configuration verification failed");
  }

  dap_jtag_write_ir(CMD_LSC_INIT_ADDRESS, IR_LENGTH);
  dap_jtag_idle(8);

  // Verify Feature Row
  dap_jtag_write_ir(CMD_LSC_READ_FEATURE, IR_LENGTH);
  dap_jtag_read_dr((uint8_t *)&feature, 64);
  dap_jtag_idle(8);

  check(feature == jed.feature, "Feature Row verification failed");

  // Verify FEABITS
  dap_jtag_write_ir(CMD_LSC_READ_FEABITS, IR_LENGTH);
  dap_jtag_idle(8);
  dap_jtag_read_dr((uint8_t *)&feabits, 16);

  check(feabits == jed.feabits, "FEABITS verification failed");

  buf_free(file_data);
  buf_free(jed.config);
}

//-----------------------------------------------------------------------------
static void target_read(void)
{
  error_exit("reading is not supported for this target");
}

//-----------------------------------------------------------------------------
static int target_fuse_read(int section, uint8_t *data)
{
  error_exit("direct access to fuses is not supported for this target");
  (void)section;
  (void)data;
  return 0;
}

//-----------------------------------------------------------------------------
static void target_fuse_write(int section, uint8_t *data)
{
  error_exit("direct access to fuses is not supported for this target");
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
  "  Feature Row and FEABITS are taken from the JED file\n";

//-----------------------------------------------------------------------------
target_ops_t target_lattice_lcmxo2_ops =
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


