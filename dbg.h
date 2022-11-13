// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2013-2022, Alex Taradov <alex@taradov.com>. All rights reserved.

#ifndef _DBG_H_
#define _DBG_H_

/*- Includes ----------------------------------------------------------------*/
#include <stddef.h>
#include <stdint.h>

/*- Definitions -------------------------------------------------------------*/
#define DBG_MAX_EP_SIZE        1024

/*- Types -------------------------------------------------------------------*/
typedef struct
{
  char     *path;
  uint64_t entry_id;
  char     *serial;
  char     *manufacturer;
  char     *product;
  int      vid;
  int      pid;
} debugger_t;

/*- Prototypes --------------------------------------------------------------*/
int dbg_enumerate(debugger_t *debuggers, int size);
void dbg_open(debugger_t *debugger);
void dbg_close(void);
int dbg_get_report_size(void);
int dbg_dap_cmd(uint8_t *data, int resp_size, int req_size);

#endif // _DBG_H_

