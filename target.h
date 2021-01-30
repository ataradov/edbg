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

#ifndef _TARGET_H_
#define _TARGET_H_

/*- Includes ----------------------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>

/*- Types -------------------------------------------------------------------*/
typedef struct
{
  bool         erase;
  bool         program;
  bool         verify;
  bool         lock;
  bool         unlock;
  bool         read;
  char         *name;
  int32_t      offset;
  int32_t      size;
  char         *fuse_cmd;

  // For target use only
  int          file_size;
  uint8_t      *file_data;
} target_options_t;

typedef struct
{
  void (*select)(target_options_t *options);
  void (*deselect)(void);
  void (*erase)(void);
  void (*lock)(void);
  void (*unlock)(void);
  void (*program)(void);
  void (*verify)(void);
  void (*read)(void);
  int  (*fread)(int section, uint8_t *data);
  void (*fwrite)(int section, uint8_t *data);
  char *(*enumerate)(int i);
  char *help;
} target_ops_t;

/*- Prototypes --------------------------------------------------------------*/
void target_list(void);
target_ops_t *target_get_ops(const char *name);
void target_check_options(target_options_t *options, int size, int align);
void target_free_options(target_options_t *options);
void target_fuse_commands(target_ops_t *ops, char *cmd);

#endif // _TARGET_H_

