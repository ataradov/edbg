/*
 * Copyright (c) 2013-2017, Alex Taradov <alex@taradov.com>
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

#ifndef _TARGET_H_
#define _TARGET_H_

/*- Includes ----------------------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>

/*- Definitions -------------------------------------------------------------*/
enum
{
  TARGET_FUSE_READ   = (1 << 0),
  TARGET_FUSE_WRITE  = (1 << 1),
  TARGET_FUSE_VERIFY = (1 << 2),
};

/*- Types -------------------------------------------------------------------*/
typedef struct
{
  bool         erase;
  bool         program;
  bool         verify;
  bool         lock;
  bool         read;
  bool         fuse;
  bool         fuse_read;
  bool         fuse_write;
  bool         fuse_verify;
  int          fuse_start;
  int          fuse_end;
  uint32_t     fuse_value;
  char         *fuse_name;
  char         *name;
  int32_t      offset;
  int32_t      size;

  // For target use only
  int          file_size;
  uint8_t      *file_data;

  int          fuse_size;
  uint8_t      *fuse_data;
} target_options_t;

typedef struct
{
  void (*select)(target_options_t *options);
  void (*deselect)(void);
  void (*erase)(void);
  void (*lock)(void);
  void (*program)(void);
  void (*verify)(void);
  void (*read)(void);
  void (*fuse)(void);
} target_ops_t;

typedef struct
{
  char         *name;
  char         *description;
  target_ops_t *ops;
} target_t;

/*- Prototypes --------------------------------------------------------------*/
void target_list(void);
target_t *target_get_ops(char *name);
void target_check_options(target_options_t *options, int size, int align);
void target_free_options(target_options_t *options);

#endif // _TARGET_H_

