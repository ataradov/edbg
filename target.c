// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2013-2022, Alex Taradov <alex@taradov.com>. All rights reserved.

/*- Includes ----------------------------------------------------------------*/
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "target.h"
#include "edbg.h"
#include "dap.h"

/*- Definitions -------------------------------------------------------------*/
#define MAX_FAMILIES   100 // Maximum number of families supported by a single driver
#define MAX_FUSE_SIZE  2048

/*- Types -------------------------------------------------------------------*/
typedef struct
{
  char         *name;
  char         *description;
  target_ops_t *ops;
} target_t;

/*- Variables ---------------------------------------------------------------*/
extern target_ops_t target_atmel_cm0p_ops;
extern target_ops_t target_atmel_cm3_ops;
extern target_ops_t target_atmel_cm4_ops;
extern target_ops_t target_atmel_cm7_ops;
extern target_ops_t target_atmel_cm4v2_ops;
extern target_ops_t target_mchp_cm23_ops;
extern target_ops_t target_st_stm32g0_ops;
extern target_ops_t target_st_stm32wb55_ops;
extern target_ops_t target_gd_gd32f4xx_ops;
extern target_ops_t target_nu_m480_ops;
extern target_ops_t target_lattice_lcmxo2_ops;
extern target_ops_t target_rpi_rp2040_ops;
extern target_ops_t target_puya_py32f0_ops;

static target_t targets[] =
{
  { "atmel_cm0p",	"Atmel SAM C/D/L/R, PIC32CM MC",			&target_atmel_cm0p_ops },
  { "atmel_cm3",	"Atmel SAM3X/A/U",					&target_atmel_cm3_ops },
  { "atmel_cm4",	"Atmel SAM G and SAM4",					&target_atmel_cm4_ops },
  { "atmel_cm7",	"Atmel SAM E7x/S7x/V7x",				&target_atmel_cm7_ops },
  { "atmel_cm4v2",	"Atmel SAM D5x/E5x",					&target_atmel_cm4v2_ops },
  { "mchp_cm23",	"Microchip SAM L10/L11, PIC32CM LE00/LS00/LS60",	&target_mchp_cm23_ops },
  { NULL,		"STMicroelectronics STM32G0",				&target_st_stm32g0_ops },
  { NULL,		"STMicroelectronics STM32WB55",				&target_st_stm32wb55_ops },
  { NULL,		"GigaDevice GD32F4xx",					&target_gd_gd32f4xx_ops },
  { NULL,		"Nuvoton M480",						&target_nu_m480_ops },
  { NULL,		"Lattice LCMXO2",					&target_lattice_lcmxo2_ops },
  { NULL,		"Raspberry Pi RP2040 (external flash)", 		&target_rpi_rp2040_ops },
  { NULL,		"Puya PY32F0xx", 					&target_puya_py32f0_ops },
};

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
void target_list(void)
{
  message("Supported device families:\n");

  for (int i = 0; i < ARRAY_SIZE(targets); i++)
  {
    char *family_name[MAX_FAMILIES];
    int family_count = 0;

    message("  %s:\n", targets[i].description);

    for (int j = 0; true; j++)
    {
      char *family = targets[i].ops->enumerate(j);
      bool match = false;

      if (NULL == family)
        break;

      for (int k = 0; k < family_count; k++)
      {
        if (0 == strcmp(family_name[k], family))
        {
          match = true;
          break;
        }
      }

      if (!match)
        family_name[family_count++] = family;

      check(family_count < MAX_FAMILIES, "internal: too many families in enumeration");
    }

    message("    ");

    for (int j = 0; j < family_count; j++)
      message("%s ", family_name[j]);

    message("\n\n");
  }
}

//-----------------------------------------------------------------------------
target_ops_t *target_get_ops(const char *name)
{
  for (int i = 0; i < ARRAY_SIZE(targets); i++)
  {
    for (int j = 0; true; j++)
    {
      char *family = targets[i].ops->enumerate(j);

      if (NULL == family)
        break;

      if (0 == strcmp(family, name))
        return targets[i].ops;
    }
  }

  // Note: This is deprecated and will be removed in future versions
  for (int i = 0; i < ARRAY_SIZE(targets); i++)
  {
    if (NULL != targets[i].name && 0 == strcmp(targets[i].name, name))
    {
      warning("specifying '%s' as a target name is deprecated; see '-t list' for a list of supported targets", name);
      return targets[i].ops;
    }
  }

  error_exit("unknown target type '%s'; see '-t list' for a list of supported targets", name);

  return NULL;
}

//-----------------------------------------------------------------------------
void target_check_options(target_options_t *options, int size, int align)
{
  options->file_data = NULL;
  options->file_size = 0;

  if (-1 == options->offset)
    options->offset = 0;

  if (-1 == options->size)
    options->size = size - options->offset;

  if (0 != (options->offset % align))
    error_exit("offset must be a multiple of %d for the selected target", align);

  if (0 != (options->size % align))
    error_exit("size must be a multiple of %d for the selected target", align);

  check(options->size <= size, "size is too big for the selected target");
  check(options->offset < size, "offset is too big for the selected target");

  if (options->program || options->verify)
  {
    options->file_data = buf_alloc(options->size);
    options->file_size = load_file(options->name, options->file_data, options->size);
    memset(&options->file_data[options->file_size], 0xff, options->size - options->file_size);

    check((options->file_size + options->offset) <= size, "file is too big for the selected target");
  }
  else if (options->read)
  {
    options->file_data = buf_alloc(options->size);
    options->file_size = options->size;
  }
}

//-----------------------------------------------------------------------------
void target_free_options(target_options_t *options)
{
  buf_free(options->file_data);
}

//-----------------------------------------------------------------------------
static uint32_t extract_value(uint8_t *buf, int start, int end)
{
  uint32_t value = 0;
  int bit = start;
  int index = 0;

  do
  {
    int by = bit / 8;
    int bt = bit % 8;

    if (buf[by] & (1 << bt))
      value |= (1 << index);

    bit++;
    index++;
  } while (bit <= end);

  return value;
}

//-----------------------------------------------------------------------------
static void apply_value(uint8_t *buf, uint32_t value, int start, int end)
{
  int bit = start;
  int index = 0;

  do
  {
    int by = bit / 8;
    int bt = bit % 8;

    if (value & (1 << index))
      buf[by] |= (1 << bt);
    else
      buf[by] &= ~(1 << bt);

    bit++;
    index++;
  } while (bit <= end);
}

//-----------------------------------------------------------------------------
static char *get_file_name(char **str)
{
  char *end = *str;
  char *res;
  int size;

  while (*end != 0 && *end != ';')
    end++;

  size = end - *str + 1;

  res = buf_alloc(size);

  memcpy(res, *str, size - 1);

  *str = end;

  return res;
}

//-----------------------------------------------------------------------------
static char *process_fuse_commands(target_ops_t *ops, char *cmd)
{
  bool use_file = false;
  bool full_range = false;
  bool read = false;
  bool write = false;
  bool verify = false;
  int section = 0;
  int end = 0;
  int start = 0;
  char *name = NULL;
  uint32_t value = 0;
  uint8_t data[MAX_FUSE_SIZE];
  int size;

  while (*cmd)
  {
    if ('r' == *cmd)
      read = true;
    else if ('w' == *cmd)
      write = true;
    else if ('v' == *cmd)
      verify = true;
    else
      break;

    cmd++;
  }

  check(read || write || verify, "no fuse operations spefified");

  if (',' != *cmd)
  {
    section = strtoul(cmd, &cmd, 0);
  }

  if (',' == *cmd)
  {
    cmd++;

    if ('*' == *cmd)
    {
      cmd++;
      use_file = true;
    }
    else if (':' == *cmd)
    {
      cmd++;
      full_range = true;
    }
    else
    {
      end = strtoul(cmd, &cmd, 0);
      start = end;

      if (':' == *cmd)
      {
        cmd++;
        start = strtoul(cmd, &cmd, 0);
      }
    }
  }
  else
  {
    error_exit("fuse index is required");
  }

  if (',' == *cmd)
  {
    cmd++;

    if (use_file)
      name = get_file_name(&cmd);
    else
      value = strtoul(cmd, &cmd, 0);
  }
  else if (write || verify)
  {
    error_exit("value or name is required for fuse write and verify operations");
  }

  check(end >= start, "bit range must be specified in a descending order");
  check((end - start) <= 32, "bit range must be 32 bits or less");

  if (use_file && read && (write || verify))
    error_exit("mutually exclusive fuse actions specified");

  // Perform fuse operations
  size = ops->fread(section, data);

  check(size > 0, "requested section (%d) does not exist on the target", section);

  if (read)
  {
    if (use_file)
    {
      verbose("  saving %d byte(s) from section %d into file '%s': ", size, section, name);
      save_file(name, data, size);
      verbose("OK\n");
    }
    else if (full_range)
    {
      verbose("  reading %d byte(s) from section %d: ", size, section);

      for (int i = 0; i < size; i++)
        message("0x%02x ", data[i]);

      message("\n");
    }
    else
    {
      uint32_t v = extract_value(data, start, end);

      if (start == end)
        verbose("  reading bit %d from section %d: ", start, section);
      else
        verbose("  reading bits %d:%d from section %d: ", end, start, section);

      message("0x%x (%u)\n", v, v);
    }
  }

  if (write)
  {
    if (use_file)
    {
      uint8_t file_data[MAX_FUSE_SIZE];
      int rsize;

      verbose("  writing %d byte(s) to section %d from file '%s': ", size, section, name);

      rsize = load_file(name, file_data, size);

      check(rsize == size, "file size (%d byte(s)) is less than section size (%d byte(s))", rsize, size);

      memcpy(data, file_data, size);
    }
    else if (full_range)
    {
      error_exit("write operation requires a bit or a bit range specification");
    }
    else
    {
      if (start == end)
        verbose("  writing value 0x%x to bit %d in section %d: ", value, start, section);
      else
        verbose("  writing value 0x%x to bits %d:%d in section %d: ", value, end, start, section);

      apply_value(data, value, start, end);
    }

    ops->fwrite(section, data);

    verbose("OK\n");
  }

  if (verify)
  {
    ops->fread(section, data);

    if (use_file)
    {
      uint8_t file_data[MAX_FUSE_SIZE];
      int rsize;

      rsize = load_file(name, file_data, size);

      verbose("  verifying %d byte(s) from section %d using file '%s': ", rsize, section, name);

      for (int i = 0; i < rsize; i++)
      {
        if (data[i] != file_data[i])
          error_exit("at offset %d expected 0x%02x, got 0x%02x", i, file_data[i], data[i]);
      }
    }
    else if (full_range)
    {
      error_exit("verify operation requires a bit or a bit range specification");
    }
    else
    {
      uint32_t v = extract_value(data, start, end);

      if (start == end)
        verbose("  verifying bit %d from section %d: ", start, section);
      else
        verbose("  verifying bits %d:%d from section %d: ", end, start, section);

      if (value != v)
        error_exit("expected 0x%x (%u), got 0x%x (%u)", value, value, v, v);
    }

    verbose("OK\n");
  }

  return cmd;
}

//-----------------------------------------------------------------------------
void target_fuse_commands(target_ops_t *ops, char *cmd)
{
  while (true)
  {
    cmd = process_fuse_commands(ops, cmd);

    if (*cmd == ';')
    {
      cmd++;
      continue;
    }
    else if (*cmd == 0)
    {
      break;
    }
    else
    {
      error_exit("junk at the end of the fuse operations: '%s'", cmd);
    }
  }
}


