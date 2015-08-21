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
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "edbg.h"
#include "dap.h"
#include "dbg.h"

/*- Definitions -------------------------------------------------------------*/

/*- Types -------------------------------------------------------------------*/
enum
{
  ID_DAP_INFO               = 0x00,
  ID_DAP_LED                = 0x01,
  ID_DAP_CONNECT            = 0x02,
  ID_DAP_DISCONNECT         = 0x03,
  ID_DAP_TRANSFER_CONFIGURE = 0x04,
  ID_DAP_TRANSFER           = 0x05,
  ID_DAP_TRANSFER_BLOCK     = 0x06,
  ID_DAP_TRANSFER_ABORT     = 0x07,
  ID_DAP_WRITE_ABORT        = 0x08,
  ID_DAP_DELAY              = 0x09,
  ID_DAP_RESET_TARGET       = 0x0a,
  ID_DAP_SWJ_PINS           = 0x10,
  ID_DAP_SWJ_CLOCK          = 0x11,
  ID_DAP_SWJ_SEQUENCE       = 0x12,
  ID_DAP_SWD_CONFIGURE      = 0x13,
  ID_DAP_JTAG_SEQUENCE      = 0x14,
  ID_DAP_JTAG_CONFIGURE     = 0x15,
  ID_DAP_JTAG_IDCODE        = 0x16,
};

enum
{
  DAP_INFO_VENDOR           = 0x01,
  DAP_INFO_PRODUCT          = 0x02,
  DAP_INFO_SER_NUM          = 0x03,
  DAP_INFO_FW_VER           = 0x04,
  DAP_INFO_DEVICE_VENDOR    = 0x05,
  DAP_INFO_DEVICE_NAME      = 0x06,
  DAP_INFO_CAPABILITIES     = 0xf0,
  DAP_INFO_PACKET_COUNT     = 0xfe,
  DAP_INFO_PACKET_SIZE      = 0xff,
};

enum
{
  DAP_TRANSFER_APnDP        = 1 << 0,
  DAP_TRANSFER_RnW          = 1 << 1,
  DAP_TRANSFER_A2           = 1 << 2,
  DAP_TRANSFER_A3           = 1 << 3,
  DAP_TRANSFER_MATCH_VALUE  = 1 << 4,
  DAP_TRANSFER_MATCH_MASK   = 1 << 5,
};

enum
{
  DAP_PORT_SWD   = 1 << 0,
  DAP_PORT_JTAG  = 1 << 1,
};

enum
{
  DAP_SWJ_SWCLK_TCK = 1 << 0,
  DAP_SWJ_SWDIO_TMS = 1 << 1,
  DAP_SWJ_TDI       = 1 << 2,
  DAP_SWJ_TDO       = 1 << 3,
  DAP_SWJ_nTRST     = 1 << 5,
  DAP_SWJ_nRESET    = 1 << 7,
};

enum
{
  DAP_OK    = 0x00,
  DAP_ERROR = 0xff,
};

enum
{
  SWD_DP_R_IDCODE    = 0x00,
  SWD_DP_W_ABORT     = 0x00,
  SWD_DP_R_CTRL_STAT = 0x04,
  SWD_DP_W_CTRL_STAT = 0x04, // When CTRLSEL == 0
  SWD_DP_W_WCR       = 0x04, // When CTRLSEL == 1
  SWD_DP_R_RESEND    = 0x08,
  SWD_DP_W_SELECT    = 0x08,
  SWD_DP_R_RDBUFF    = 0x0c,
};

enum
{
  SWD_AP_CSW  = 0x00 | DAP_TRANSFER_APnDP,
  SWD_AP_TAR  = 0x04 | DAP_TRANSFER_APnDP,
  SWD_AP_DRW  = 0x0c | DAP_TRANSFER_APnDP,

  SWD_AP_DB0  = 0x00 | DAP_TRANSFER_APnDP, // 0x10
  SWD_AP_DB1  = 0x04 | DAP_TRANSFER_APnDP, // 0x14
  SWD_AP_DB2  = 0x08 | DAP_TRANSFER_APnDP, // 0x18
  SWD_AP_DB3  = 0x0c | DAP_TRANSFER_APnDP, // 0x1c

  SWD_AP_CFG  = 0x04 | DAP_TRANSFER_APnDP, // 0xf4
  SWD_AP_BASE = 0x08 | DAP_TRANSFER_APnDP, // 0xf8
  SWD_AP_IDR  = 0x0c | DAP_TRANSFER_APnDP, // 0xfc
};

/*- Variables ---------------------------------------------------------------*/

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
void dap_led(int index, int state)
{
  uint8_t buf[3];

  buf[0] = ID_DAP_LED;
  buf[1] = index;
  buf[2] = state;
  dbg_dap_cmd(buf, sizeof(buf), 3);

  check(DAP_OK == buf[0], "DAP_LED failed");
}

//-----------------------------------------------------------------------------
void dap_connect(void)
{
  uint8_t buf[2];

  buf[0] = ID_DAP_CONNECT;
  buf[1] = DAP_PORT_SWD;
  dbg_dap_cmd(buf, sizeof(buf), 2);

  check(DAP_PORT_SWD == buf[0], "DAP_CONNECT failed");
}

//-----------------------------------------------------------------------------
void dap_disconnect(void)
{
  uint8_t buf[1];

  buf[0] = ID_DAP_DISCONNECT;
  dbg_dap_cmd(buf, sizeof(buf), 1);
}

//-----------------------------------------------------------------------------
void dap_swj_clock(uint32_t clock)
{
  uint8_t buf[5];

  buf[0] = ID_DAP_SWJ_CLOCK;
  buf[1] = clock & 0xff;
  buf[2] = (clock >> 8) & 0xff;
  buf[3] = (clock >> 16) & 0xff;
  buf[4] = (clock >> 24) & 0xff;
  dbg_dap_cmd(buf, sizeof(buf), 5);

  check(DAP_OK == buf[0], "SWJ_CLOCK failed");
}

//-----------------------------------------------------------------------------
void dap_transfer_configure(uint8_t idle, uint16_t count, uint16_t retry)
{
  uint8_t buf[6];

  buf[0] = ID_DAP_TRANSFER_CONFIGURE;
  buf[1] = idle;
  buf[2] = count & 0xff;
  buf[3] = (count >> 8) & 0xff;
  buf[4] = retry & 0xff;
  buf[5] = (retry >> 8) & 0xff;
  dbg_dap_cmd(buf, sizeof(buf), 6);

  check(DAP_OK == buf[0], "TRANSFER_CONFIGURE failed");
}

//-----------------------------------------------------------------------------
void dap_swd_configure(uint8_t cfg)
{
  uint8_t buf[2];

  buf[0] = ID_DAP_SWD_CONFIGURE;
  buf[1] = cfg;
  dbg_dap_cmd(buf, sizeof(buf), 2);

  check(DAP_OK == buf[0], "SWD_CONFIGURE failed");
}

//-----------------------------------------------------------------------------
void dap_get_debugger_info(void)
{
  uint8_t buf[100];
  char str[500];
  char *ptr = str;

  buf[0] = ID_DAP_INFO;
  buf[1] = DAP_INFO_VENDOR;
  dbg_dap_cmd(buf, sizeof(buf), 2);
  memcpy(ptr, &buf[1], buf[0]-1); // -1 to adjust for trailing zero
  ptr += buf[0]-1;
  *ptr++ = ' ';

  buf[0] = ID_DAP_INFO;
  buf[1] = DAP_INFO_PRODUCT;
  dbg_dap_cmd(buf, sizeof(buf), 2);
  memcpy(ptr, &buf[1], buf[0]-1);
  ptr += buf[0]-1;
  *ptr++ = ' ';

  buf[0] = ID_DAP_INFO;
  buf[1] = DAP_INFO_SER_NUM;
  dbg_dap_cmd(buf, sizeof(buf), 2);
  memcpy(ptr, &buf[1], buf[0]-1);
  ptr += buf[0]-1;
  *ptr++ = ' ';

  buf[0] = ID_DAP_INFO;
  buf[1] = DAP_INFO_FW_VER;
  dbg_dap_cmd(buf, sizeof(buf), 2);
  memcpy(ptr, &buf[1], buf[0]-1);
  ptr += buf[0]-1;
  *ptr++ = ' ';

  buf[0] = ID_DAP_INFO;
  buf[1] = DAP_INFO_CAPABILITIES;
  dbg_dap_cmd(buf, sizeof(buf), 2);

  *ptr++ = '(';
  if (buf[1] & DAP_PORT_SWD)
    *ptr++ = 'S';
  if (buf[1] & DAP_PORT_JTAG)
    *ptr++ = 'J';
  *ptr++ = ')';
  *ptr = 0;

  verbose("Debugger: %s\n", str);

  check(buf[1] & DAP_PORT_SWD, "SWD support required");
}

//-----------------------------------------------------------------------------
void dap_reset_target(void)
{
  uint8_t buf[1];

  buf[0] = ID_DAP_RESET_TARGET;
  dbg_dap_cmd(buf, sizeof(buf), 1);

  check(DAP_OK == buf[0], "RESET_TARGET failed");
}

//-----------------------------------------------------------------------------
void dap_reset_target_hw(void)
{
  uint8_t buf[7];

  //-------------
  buf[0] = ID_DAP_SWJ_PINS;
  buf[1] = 0; // Value
  buf[2] = DAP_SWJ_nRESET | DAP_SWJ_SWCLK_TCK | DAP_SWJ_SWDIO_TMS; // Select
  buf[3] = 0; // Wait
  buf[4] = 0;
  buf[5] = 0;
  buf[6] = 0;
  dbg_dap_cmd(buf, sizeof(buf), 7);

  //-------------
  buf[0] = ID_DAP_SWJ_PINS;
  buf[1] = DAP_SWJ_nRESET; // Value
  buf[2] = DAP_SWJ_nRESET | DAP_SWJ_SWCLK_TCK | DAP_SWJ_SWDIO_TMS; // Select
  buf[3] = 0; // Wait
  buf[4] = 0;
  buf[5] = 0;
  buf[6] = 0;
  dbg_dap_cmd(buf, sizeof(buf), 7);
}

//-----------------------------------------------------------------------------
uint32_t dap_read_reg(uint8_t reg)
{
  uint8_t buf[8];

  buf[0] = ID_DAP_TRANSFER;
  buf[1] = 0x00; // DAP index
  buf[2] = 0x01; // Request size
  buf[3] = reg | DAP_TRANSFER_RnW;
  dbg_dap_cmd(buf, sizeof(buf), 4);

  if (1 != buf[0] || 1 != buf[1])
  {
    error_exit("invalid response while reading the register 0x%02x (count = %d, value = %d)",
        reg, buf[0], buf[1]);
  }

  return ((uint32_t)buf[5] << 24) | ((uint32_t)buf[4] << 16) |
         ((uint32_t)buf[3] << 8) | (uint32_t)buf[2];
}

//-----------------------------------------------------------------------------
void dap_write_reg(uint8_t reg, uint32_t data)
{
  uint8_t buf[8];

  buf[0] = ID_DAP_TRANSFER;
  buf[1] = 0x00; // DAP index
  buf[2] = 0x01; // Request size
  buf[3] = reg;
  buf[4] = data & 0xff;
  buf[5] = (data >> 8) & 0xff;
  buf[6] = (data >> 16) & 0xff;
  buf[7] = (data >> 24) & 0xff;
  dbg_dap_cmd(buf, sizeof(buf), 8);

  if (1 != buf[0] || 1 != buf[1])
  {
    error_exit("invalid response while writing the register 0x%02x (count = %d, value = %d)",
        reg, buf[0], buf[1]);
  }
}

//-----------------------------------------------------------------------------
uint32_t dap_read_word(uint32_t addr)
{
  dap_write_reg(SWD_AP_TAR, addr);
  return dap_read_reg(SWD_AP_DRW);
}

//-----------------------------------------------------------------------------
void dap_write_word(uint32_t addr, uint32_t data)
{
  dap_write_reg(SWD_AP_TAR, addr);
  dap_write_reg(SWD_AP_DRW, data);
}

//-----------------------------------------------------------------------------
void dap_read_block(uint32_t addr, uint8_t *data, int size)
{
  uint8_t buf[512];

  dap_write_reg(SWD_AP_TAR, addr);

  buf[0] = ID_DAP_TRANSFER_BLOCK;
  buf[1] = 0x00; // DAP index
  buf[2] = (size / 4) & 0xff;
  buf[3] = ((size / 4) >> 8) & 0xff;
  buf[4] = SWD_AP_DRW | DAP_TRANSFER_RnW | DAP_TRANSFER_APnDP;
  dbg_dap_cmd(buf, sizeof(buf), 5);

  memcpy(data, &buf[3], size);
}

//-----------------------------------------------------------------------------
void dap_write_block(uint32_t addr, uint8_t *data, int size)
{
  uint8_t buf[512];

  dap_write_reg(SWD_AP_TAR, addr);

  buf[0] = ID_DAP_TRANSFER_BLOCK;
  buf[1] = 0x00; // DAP index
  buf[2] = (size / 4) & 0xff;
  buf[3] = ((size / 4) >> 8) & 0xff;
  buf[4] = SWD_AP_DRW | DAP_TRANSFER_APnDP;
  memcpy(&buf[5], data, size);
  dbg_dap_cmd(buf, sizeof(buf), 5 + size);
}

//-----------------------------------------------------------------------------
void dap_reset_link(void)
{
  uint8_t buf[9];

  //-------------
  buf[0] = ID_DAP_SWJ_SEQUENCE;
  buf[1] = 7 * 8;
  memset(&buf[2], 0xff, 7);
  dbg_dap_cmd(buf, sizeof(buf), 9);

  check(DAP_OK == buf[0], "SWJ_SEQUENCE failed");

  //-------------
  buf[0] = ID_DAP_SWJ_SEQUENCE;
  buf[1] = 2 * 8;
  buf[2] = 0x9e;
  buf[3] = 0xe7;
  dbg_dap_cmd(buf, sizeof(buf), 4);

  check(DAP_OK == buf[0], "SWJ_SEQUENCE failed");

  //-------------
  buf[0] = ID_DAP_SWJ_SEQUENCE;
  buf[1] = 7 * 8;
  memset(&buf[2], 0xff, 7);
  dbg_dap_cmd(buf, sizeof(buf), 9);

  check(DAP_OK == buf[0], "SWJ_SEQUENCE failed");

  //-------------
  buf[0] = ID_DAP_SWJ_SEQUENCE;
  buf[1] = 8;
  buf[2] = 0x00;
  dbg_dap_cmd(buf, sizeof(buf), 3);

  check(DAP_OK == buf[0], "SWJ_SEQUENCE failed");
}

//-----------------------------------------------------------------------------
uint32_t dap_read_idcode(void)
{
  return dap_read_reg(SWD_DP_R_IDCODE);
}

//-----------------------------------------------------------------------------
void dap_write_abort(void)
{
  dap_write_reg(SWD_DP_W_ABORT, 1);
}

//-----------------------------------------------------------------------------
void dap_target_prepare(void)
{
  dap_write_reg(SWD_DP_W_SELECT, 0x00000000);
  dap_write_reg(SWD_DP_W_CTRL_STAT, 0x50000f00);
  dap_write_reg(SWD_AP_CSW, 0x23000052);
}

