// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2013-2022, Alex Taradov <alex@taradov.com>. All rights reserved.

#ifndef _TARGET_H_
#define _TARGET_H_

/*- Includes ----------------------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>

/*- Types -------------------------------------------------------------------*/
typedef struct
{
  int          reset;
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

