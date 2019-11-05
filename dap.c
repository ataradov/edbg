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
  DAP_TRANSFER_INVALID      = 0,
  DAP_TRANSFER_OK           = 1 << 0,
  DAP_TRANSFER_WAIT         = 1 << 1,
  DAP_TRANSFER_FAULT        = 1 << 2,
  DAP_TRANSFER_ERROR        = 1 << 3,
  DAP_TRANSFER_MISMATCH     = 1 << 4,
  DAP_TRANSFER_NO_TARGET    = 7,
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

#define DP_ABORT_DAPABORT      (1 << 0)
#define DP_ABORT_STKCMPCLR     (1 << 1)
#define DP_ABORT_STKERRCLR     (1 << 2)
#define DP_ABORT_WDERRCLR      (1 << 3)
#define DP_ABORT_ORUNERRCLR    (1 << 4)

#define DP_CST_ORUNDETECT      (1 << 0)
#define DP_CST_STICKYORUN      (1 << 1)
#define DP_CST_TRNMODE_NORMAL  (0 << 2)
#define DP_CST_TRNMODE_VERIFY  (1 << 2)
#define DP_CST_TRNMODE_COMPARE (2 << 2)
#define DP_CST_STICKYCMP       (1 << 4)
#define DP_CST_STICKYERR       (1 << 5)
#define DP_CST_READOK          (1 << 6)
#define DP_CST_WDATAERR        (1 << 7)
#define DP_CST_MASKLANE(x)     ((x) << 8)
#define DP_CST_TRNCNT(x)       ((x) << 12)
#define DP_CST_CDBGRSTREQ      (1 << 26)
#define DP_CST_CDBGRSTACK      (1 << 27)
#define DP_CST_CDBGPWRUPREQ    (1 << 28)
#define DP_CST_CDBGPWRUPACK    (1 << 29)
#define DP_CST_CSYSPWRUPREQ    (1 << 30)
#define DP_CST_CSYSPWRUPACK    (1 << 31)

#define DP_SELECT_CTRLSEL      (1 << 0)
#define DP_SELECT_APBANKSEL(x) ((x) << 4)
#define DP_SELECT_APSEL(x)     ((x) << 24)

#define AP_CSW_SIZE_BYTE       (0 << 0)
#define AP_CSW_SIZE_HALF       (1 << 0)
#define AP_CSW_SIZE_WORD       (2 << 0)
#define AP_CSW_ADDRINC_OFF     (0 << 4)
#define AP_CSW_ADDRINC_SINGLE  (1 << 4)
#define AP_CSW_ADDRINC_PACKED  (2 << 4)
#define AP_CSW_DEVICEEN        (1 << 6)
#define AP_CSW_TRINPROG        (1 << 7)
#define AP_CSW_SPIDEN          (1 << 23)
#define AP_CSW_PROT(x)         ((x) << 24)
#define AP_CSW_DBGSWENABLE     (1 << 31)

/*- Variables ---------------------------------------------------------------*/
static bool dap_is_prepared = false;
static int dap_transfer_size = AP_CSW_SIZE_WORD;

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
  char str[500] = "";

  buf[0] = ID_DAP_INFO;
  buf[1] = DAP_INFO_VENDOR;
  dbg_dap_cmd(buf, sizeof(buf), 2);
  strncat(str, (char *)&buf[1], buf[0]);
  strcat(str, " ");

  buf[0] = ID_DAP_INFO;
  buf[1] = DAP_INFO_PRODUCT;
  dbg_dap_cmd(buf, sizeof(buf), 2);
  strncat(str, (char *)&buf[1], buf[0]);
  strcat(str, " ");

  buf[0] = ID_DAP_INFO;
  buf[1] = DAP_INFO_SER_NUM;
  dbg_dap_cmd(buf, sizeof(buf), 2);
  strncat(str, (char *)&buf[1], buf[0]);
  strcat(str, " ");

  buf[0] = ID_DAP_INFO;
  buf[1] = DAP_INFO_FW_VER;
  dbg_dap_cmd(buf, sizeof(buf), 2);
  strncat(str, (char *)&buf[1], buf[0]);
  strcat(str, " ");

  buf[0] = ID_DAP_INFO;
  buf[1] = DAP_INFO_CAPABILITIES;
  dbg_dap_cmd(buf, sizeof(buf), 2);

  strcat(str, "(");

  if (buf[1] & DAP_PORT_SWD)
    strcat(str, "S");

  if (buf[1] & DAP_PORT_JTAG)
    strcat(str, "J");

  strcat(str, ")");

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
void dap_reset_target_hw(int state)
{
  uint8_t buf[7];
  int value = state ? (DAP_SWJ_SWCLK_TCK | DAP_SWJ_SWDIO_TMS) : 0;

  //-------------
  buf[0] = ID_DAP_SWJ_PINS;
  buf[1] = value; // Value
  buf[2] = DAP_SWJ_nRESET | DAP_SWJ_SWCLK_TCK | DAP_SWJ_SWDIO_TMS; // Select
  buf[3] = 0; // Wait
  buf[4] = 0;
  buf[5] = 0;
  buf[6] = 0;
  dbg_dap_cmd(buf, sizeof(buf), 7);

  sleep_ms(10);

  //-------------
  buf[0] = ID_DAP_SWJ_PINS;
  buf[1] = DAP_SWJ_nRESET | value; // Value
  buf[2] = DAP_SWJ_nRESET | DAP_SWJ_SWCLK_TCK | DAP_SWJ_SWDIO_TMS; // Select
  buf[3] = 0; // Wait
  buf[4] = 0;
  buf[5] = 0;
  buf[6] = 0;
  dbg_dap_cmd(buf, sizeof(buf), 7);
}

//-----------------------------------------------------------------------------
void dap_reset_pin(int state)
{
  uint8_t buf[7];

  buf[0] = ID_DAP_SWJ_PINS;
  buf[1] = state ? DAP_SWJ_nRESET : 0; // Value
  buf[2] = DAP_SWJ_nRESET; // Select
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

  if (1 != buf[0] || DAP_TRANSFER_OK != buf[1])
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

  if (1 != buf[0] || DAP_TRANSFER_OK != buf[1])
  {
    error_exit("invalid response while writing the register 0x%02x (count = %d, value = %d)",
        reg, buf[0], buf[1]);
  }
}

//-----------------------------------------------------------------------------
static void dap_set_transfer_size(int size)
{
  uint32_t csw = AP_CSW_ADDRINC_SINGLE | AP_CSW_DEVICEEN | AP_CSW_PROT(0x23);

  if (!dap_is_prepared)
  {
    dap_write_reg(SWD_DP_W_ABORT, DP_ABORT_STKCMPCLR | DP_ABORT_STKERRCLR | DP_ABORT_ORUNERRCLR);
    dap_write_reg(SWD_DP_W_SELECT, DP_SELECT_APBANKSEL(0) | DP_SELECT_APSEL(0));
    dap_write_reg(SWD_DP_W_CTRL_STAT, DP_CST_CDBGPWRUPREQ | DP_CST_CSYSPWRUPREQ | DP_CST_MASKLANE(0xf));

    dap_write_reg(SWD_AP_CSW, csw | AP_CSW_SIZE_WORD);

    dap_transfer_size = AP_CSW_SIZE_WORD;
    dap_is_prepared = true;
  }

  if (dap_transfer_size != size)
  {
    dap_write_reg(SWD_AP_CSW, csw | size);
    dap_transfer_size = size;
  }
}

//-----------------------------------------------------------------------------
uint8_t dap_read_byte(uint32_t addr)
{
  uint32_t data;

  dap_set_transfer_size(AP_CSW_SIZE_BYTE);
  dap_write_reg(SWD_AP_TAR, addr);
  data = dap_read_reg(SWD_AP_DRW);

  return (data >> ((addr & 3) * 8)) & 0xff;
}

//-----------------------------------------------------------------------------
uint16_t dap_read_half(uint32_t addr)
{
  uint32_t data;

  dap_set_transfer_size(AP_CSW_SIZE_HALF);
  dap_write_reg(SWD_AP_TAR, addr);
  data = dap_read_reg(SWD_AP_DRW);

  return (data >> ((addr & 2) * 8)) & 0xffff;
}

//-----------------------------------------------------------------------------
uint32_t dap_read_word(uint32_t addr)
{
  dap_set_transfer_size(AP_CSW_SIZE_WORD);
  dap_write_reg(SWD_AP_TAR, addr);
  return dap_read_reg(SWD_AP_DRW);
}

//-----------------------------------------------------------------------------
void dap_write_byte(uint32_t addr, uint8_t data)
{
  dap_set_transfer_size(AP_CSW_SIZE_BYTE);
  dap_write_reg(SWD_AP_TAR, addr);
  dap_write_reg(SWD_AP_DRW, (uint32_t)data << ((addr & 3) * 8));
}

//-----------------------------------------------------------------------------
void dap_write_half(uint32_t addr, uint16_t data)
{
  dap_set_transfer_size(AP_CSW_SIZE_HALF);
  dap_write_reg(SWD_AP_TAR, addr);
  dap_write_reg(SWD_AP_DRW, (uint32_t)data << ((addr & 2) * 8));
}

//-----------------------------------------------------------------------------
void dap_write_word(uint32_t addr, uint32_t data)
{
  dap_set_transfer_size(AP_CSW_SIZE_WORD);
  dap_write_reg(SWD_AP_TAR, addr);
  dap_write_reg(SWD_AP_DRW, data);
}

//-----------------------------------------------------------------------------
void dap_read_block(uint32_t addr, uint8_t *data, int size)
{
  int max_size = (dbg_get_report_size() - 5) & ~3;
  int offs = 0;

  dap_set_transfer_size(AP_CSW_SIZE_WORD);

  while (size)
  {
    int align, sz;
    uint8_t buf[1024];

    align = 0x400 - (addr - (addr & ~0x3ff));
    sz = (size > max_size) ? max_size : size;
    sz = (sz > align) ? align : sz;

    dap_write_reg(SWD_AP_TAR, addr);

    buf[0] = ID_DAP_TRANSFER_BLOCK;
    buf[1] = 0x00; // DAP index
    buf[2] = (sz / 4) & 0xff;
    buf[3] = ((sz / 4) >> 8) & 0xff;
    buf[4] = SWD_AP_DRW | DAP_TRANSFER_RnW | DAP_TRANSFER_APnDP;
    dbg_dap_cmd(buf, sizeof(buf), 5);

    if (DAP_TRANSFER_OK != buf[2])
    {
      error_exit("invalid response while reading the block at 0x%08x (value = %d)",
          addr, buf[2]);
    }

    memcpy(&data[offs], &buf[3], sz);

    size -= sz;
    addr += sz;
    offs += sz;
  }
}

//-----------------------------------------------------------------------------
void dap_write_block(uint32_t addr, uint8_t *data, int size)
{
  int max_size = (dbg_get_report_size() - 5) & ~3;
  int offs = 0;

  dap_set_transfer_size(AP_CSW_SIZE_WORD);

  while (size)
  {
    int align, sz;
    uint8_t buf[1024];

    align = 0x400 - (addr - (addr & ~0x3ff));
    sz = (size > max_size) ? max_size : size;
    sz = (sz > align) ? align : sz;

    dap_write_reg(SWD_AP_TAR, addr);

    buf[0] = ID_DAP_TRANSFER_BLOCK;
    buf[1] = 0x00; // DAP index
    buf[2] = (sz / 4) & 0xff;
    buf[3] = ((sz / 4) >> 8) & 0xff;
    buf[4] = SWD_AP_DRW | DAP_TRANSFER_APnDP;
    memcpy(&buf[5], &data[offs], sz);
    dbg_dap_cmd(buf, sizeof(buf), 5 + sz);

    if (DAP_TRANSFER_OK != buf[2])
    {
      error_exit("invalid response while writing the block at 0x%08x (value = %d)",
          addr, buf[2]);
    }

    size -= sz;
    addr += sz;
    offs += sz;
  }
}

//-----------------------------------------------------------------------------
void dap_reset_link(void)
{
  uint8_t buf[128];

  //-------------
  buf[0] = ID_DAP_SWJ_SEQUENCE;
  buf[1] = (7 + 2 + 7 + 1) * 8;
  buf[2] = 0xff;
  buf[3] = 0xff;
  buf[4] = 0xff;
  buf[5] = 0xff;
  buf[6] = 0xff;
  buf[7] = 0xff;
  buf[8] = 0xff;
  buf[9] = 0x9e;
  buf[10] = 0xe7;
  buf[11] = 0xff;
  buf[12] = 0xff;
  buf[13] = 0xff;
  buf[14] = 0xff;
  buf[15] = 0xff;
  buf[16] = 0xff;
  buf[17] = 0xff;
  buf[18] = 0x00;

  dbg_dap_cmd(buf, sizeof(buf), 19);
  check(DAP_OK == buf[0], "SWJ_SEQUENCE failed");

  //-------------
  buf[0] = ID_DAP_TRANSFER;
  buf[1] = 0; // DAP index
  buf[2] = 1; // Request size
  buf[3] = SWD_DP_R_IDCODE | DAP_TRANSFER_RnW;
  dbg_dap_cmd(buf, sizeof(buf), 4);

  dap_is_prepared = false;
}

//-----------------------------------------------------------------------------
uint32_t dap_read_idcode(void)
{
  return dap_read_reg(SWD_DP_R_IDCODE);
}

