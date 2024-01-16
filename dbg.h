// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2013-2024, Alex Taradov <alex@taradov.com>. All rights reserved.

#ifndef _DBG_H_
#define _DBG_H_

/*- Includes ----------------------------------------------------------------*/
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/*- Definitions -------------------------------------------------------------*/
#define DBG_MAX_EP_SIZE    1024

#define DBG_CMSIS_DAP_V1   (1 << 1)
#define DBG_CMSIS_DAP_V2   (1 << 2)

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

  int      versions;
  bool     use_v2;

  char     *v1_path;
  int      v1_interface;
  int      v1_ep_size;
  int      v1_tx_ep;
  int      v1_rx_ep;

  char     *v2_path;
  int      v2_interface;
  int      v2_ep_size;
  int      v2_tx_ep;
  int      v2_rx_ep;
} debugger_t;

/*- Prototypes --------------------------------------------------------------*/
int dbg_enumerate(debugger_t *debuggers, int size);
void dbg_open(debugger_t *debugger, int version);
void dbg_close(void);
int dbg_get_packet_size(void);
int dbg_dap_cmd(uint8_t *data, int resp_size, int req_size);

#endif // _DBG_H_

