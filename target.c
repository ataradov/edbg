/*
 * Copyright (c) 2013-2015, Alex Taradov <alex@taradov.com>
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
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "target.h"
#include "edbg.h"
#include "dap.h"

/*- Definitions -------------------------------------------------------------*/

/*- Variables ---------------------------------------------------------------*/
extern target_ops_t target_atmel_cm0p_ops;
extern target_ops_t target_atmel_cm3_ops;
extern target_ops_t target_atmel_cm4_ops;
extern target_ops_t target_atmel_cm7_ops;
extern target_ops_t target_atmel_cm4v2_ops;
extern target_ops_t target_mchp_cm23_ops;

static target_t targets[] =
{
  { "atmel_cm0p",	"Atmel SAM C/D/R series",	&target_atmel_cm0p_ops },
  { "atmel_cm3",	"Atmel SAM3X/A series",		&target_atmel_cm3_ops },
  { "atmel_cm4",	"Atmel SAM G and SAM4 series",	&target_atmel_cm4_ops },
  { "atmel_cm7",	"Atmel SAM E7x/S7x/V7x series",	&target_atmel_cm7_ops },
  { "atmel_cm4v2",	"Atmel SAM D5x/E5x",		&target_atmel_cm4v2_ops },
  { "mchp_cm23",	"Microchip SAM L10/L11",	&target_mchp_cm23_ops },
  { NULL, NULL, NULL },
};

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
void target_list(void)
{
  message("Supported target types:\n");

  for (target_t *target = targets; NULL != target->name; target++)
    message("  %-16s - %s\n", target->name, target->description);
}

//-----------------------------------------------------------------------------
target_t *target_get_ops(char *name)
{
  for (target_t *target = targets; NULL != target->name; target++)
  {
    if (0 == strcmp(target->name, name))
      return target;
  }

  error_exit("unknown target type (%s); use '-t' option", name);

  return NULL;
}

//-----------------------------------------------------------------------------
void target_check_options(target_options_t *options, int size, int align, int fuse_size)
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

  if (options->fuse_name)
  {
    options->fuse_data = buf_alloc(fuse_size);
    memset(options->fuse_data, 0xff, fuse_size);

    if (options->fuse_write || options->fuse_verify)
    {
      options->fuse_size = load_file(options->fuse_name, options->fuse_data, fuse_size);
    }
    else if (options->fuse_read)
    {
      options->file_size = fuse_size;
    }
  }
}

//-----------------------------------------------------------------------------
void target_free_options(target_options_t *options)
{
  if (options->file_data)
    buf_free(options->file_data);
}

