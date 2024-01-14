// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2013-2024, Alex Taradov <alex@taradov.com>. All rights reserved.

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
  ID_DAP_SWD_SEQUENCE       = 0x1d,
  ID_DAP_JTAG_SEQUENCE      = 0x14,
  ID_DAP_JTAG_CONFIGURE     = 0x15,
  ID_DAP_JTAG_IDCODE        = 0x16,
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
  SWD_DP_R_IDCODE    = 0x00, // DPIDR in ADIv5.2
  SWD_DP_W_ABORT     = 0x00,
  SWD_DP_CTRL_STAT   = 0x04, // DPBANKSEL == 0
  SWD_DP_DLCR        = 0x04, // DPBANKSEL == 1, WCR in ADIv5.1
  SWD_DP_TARGETID    = 0x04, // DPBANKSEL == 2
  SWD_DP_DLPIDR      = 0x04, // DPBANKSEL == 3
  SWD_DP_EVENTSTAT   = 0x04, // DPBANKSEL == 4
  SWD_DP_R_RESEND    = 0x08,
  SWD_DP_W_SELECT    = 0x08,
  SWD_DP_R_RDBUFF    = 0x0c,
  SWD_DP_W_TARGETSEL = 0x0c,
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

#define JTAG_SEQUENCE_COUNT(x) (((x) == 64) ? 0 : (x))
#define JTAG_SEQUENCE_TMS      (1 << 6)
#define JTAG_SEQUENCE_TDO      (1 << 7)

#define SWD_SEQUENCE_COUNT(x)  (((x) == 64) ? 0 : (x))
#define SWD_SEQUENCE_DIN       (1 << 7)

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

#define DP_SELECT_DPBANKSEL(x) ((x) << 0)
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

#define TRANSFER_SIZE          16384
#define TRANSFER_BUF_SIZE      (DBG_MAX_EP_SIZE + 64)

#define JTAG_TRANSFER_SIZE     65536
#define JTAG_RESPONSE_BUF_SIZE (JTAG_TRANSFER_SIZE / 8)

enum
{
  TRANSFER_TYPE_READ,
  TRANSFER_TYPE_WRITE,
  TRANSFER_TYPE_WRITE_READ,
  TRANSFER_TYPE_READ_REG,
  TRANSFER_TYPE_WRITE_REG,
};

enum
{
  TRANSFER_SIZE_BYTE,
  TRANSFER_SIZE_HALF,
  TRANSFER_SIZE_WORD,
  TRANSFER_SIZE_UNKNOWN,
};

enum
{
  OP_CLEAR,
  OP_SIZE,
  OP_ADDRESS,
  OP_SKIP,
  OP_READ,
  OP_WRITE,
};

/*- Types -------------------------------------------------------------------*/
typedef struct
{
  uint8_t  type;
  uint8_t  size;
  uint32_t addr;
  uint32_t data;
} dap_request_t;

typedef struct
{
  uint8_t  opt;
  uint8_t  tdi;
} dap_jtag_request_t;

/*- Prototypes --------------------------------------------------------------*/
static void dap_add_req(int type, int size, uint32_t addr, uint32_t data);

/*- Variables ---------------------------------------------------------------*/
static int dap_dp_version = 1;
static uint32_t dap_target_id = DAP_INVALID_TARGET_ID;
static int dap_interface = DAP_INTERFACE_NONE;

static dap_request_t dap_request[TRANSFER_SIZE];
static int dap_request_count = 0;

static uint32_t dap_response[TRANSFER_SIZE];
static int dap_response_count = 0;
static int dap_response_size = 0;

static uint8_t dap_buf[TRANSFER_BUF_SIZE];
static int dap_buf_size = 0;

static uint8_t dap_ops[TRANSFER_BUF_SIZE];
static int dap_ops_size = 0;

static bool dap_set_address = true;
static int dap_address_inc = 0;
static uint32_t dap_address = 0;
static uint32_t dap_csw;

static int dap_jtag_index = 0;

static dap_jtag_request_t dap_jtag_request[JTAG_TRANSFER_SIZE];
static int dap_jtag_request_count = 0;

static uint8_t dap_jtag_response_buf[JTAG_RESPONSE_BUF_SIZE];
static int dap_jtag_response_count = 0;

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
void dap_set_dp_version(int version)
{
  dap_dp_version = version;
}

//-----------------------------------------------------------------------------
void dap_set_target_id(uint32_t id)
{
  dap_target_id = id;
}

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
void dap_connect(int interf)
{
  uint8_t buf[2];
  int cap = (DAP_INTERFACE_SWD == interf) ? DAP_CAP_SWD : DAP_CAP_JTAG;

  buf[0] = ID_DAP_CONNECT;
  buf[1] = cap;
  dbg_dap_cmd(buf, sizeof(buf), 2);

  check(buf[0] == cap, "DAP_CONNECT failed");

  dap_interface = interf;
}

//-----------------------------------------------------------------------------
void dap_disconnect(void)
{
  uint8_t buf[1];

  buf[0] = ID_DAP_DISCONNECT;
  dbg_dap_cmd(buf, sizeof(buf), 1);

  dap_interface = DAP_INTERFACE_NONE;
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
void dap_transfer_configure(uint8_t idle, uint16_t retry, uint16_t match_retry)
{
  uint8_t buf[6];

  buf[0] = ID_DAP_TRANSFER_CONFIGURE;
  buf[1] = idle;
  buf[2] = retry & 0xff;
  buf[3] = (retry >> 8) & 0xff;
  buf[4] = match_retry & 0xff;
  buf[5] = (match_retry >> 8) & 0xff;
  dbg_dap_cmd(buf, sizeof(buf), 6);

  check(DAP_OK == buf[0], "TRANSFER_CONFIGURE failed");
}

//-----------------------------------------------------------------------------
void dap_swd_configure(int cfg)
{
  uint8_t buf[2];

  buf[0] = ID_DAP_SWD_CONFIGURE;
  buf[1] = cfg;
  dbg_dap_cmd(buf, sizeof(buf), 2);

  check(DAP_OK == buf[0], "SWD_CONFIGURE failed");
}

//-----------------------------------------------------------------------------
void dap_jtag_configure(int count, int *ir_len)
{
  uint8_t buf[32];

  buf[0] = ID_DAP_JTAG_CONFIGURE;
  buf[1] = count;
  for (int i = 0; i < count; i++)
    buf[2+i] = ir_len[i];
  dbg_dap_cmd(buf, sizeof(buf), 2 + count);

  check(DAP_OK == buf[0], "JTAG_CONFIGURE failed");
}

//-----------------------------------------------------------------------------
void dap_jtag_set_index(int index)
{
  dap_jtag_index = index;
}

//-----------------------------------------------------------------------------
int dap_info(int info, uint8_t *data, int size)
{
  uint8_t buf[256];
  int rsize;

  buf[0] = ID_DAP_INFO;
  buf[1] = info;
  dbg_dap_cmd(buf, sizeof(buf), 2);

  rsize = (size < buf[0]) ? size : buf[0];
  memcpy(data, &buf[1], rsize);

  if (rsize < size)
    data[rsize] = 0;

  return rsize;
}

//-----------------------------------------------------------------------------
static uint32_t dap_parity(uint32_t value)
{
  value ^= value >> 16;
  value ^= value >> 8;
  value ^= value >> 4;
  value &= 0x0f;

  return (0x6996 >> value) & 1;
}

//-----------------------------------------------------------------------------
void dap_reset_link(void)
{
  uint8_t buf[32];

  if (DAP_INTERFACE_SWD == dap_interface)
  {
    if (dap_dp_version == 1)
    {
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

      dap_read_idcode();
    }
    else if (dap_dp_version == 2)
    {
      // Switch to SWD
      buf[0] = ID_DAP_SWJ_SEQUENCE;
      buf[1] = (1 + 16 + 7 + 1) * 8;
      buf[2] = 0xff;
      buf[3] = 0x92; // Selection Alert Sequence
      buf[4] = 0xf3;
      buf[5] = 0x09;
      buf[6] = 0x62;
      buf[7] = 0x95;
      buf[8] = 0x2d;
      buf[9] = 0x85;
      buf[10] = 0x86;
      buf[11] = 0xe9;
      buf[12] = 0xaf;
      buf[13] = 0xdd;
      buf[14] = 0xe3;
      buf[15] = 0xa2;
      buf[16] = 0x0e;
      buf[17] = 0xbc;
      buf[18] = 0x19;
      buf[19] = 0xa0; // 4 cycles with SWDIO/TMS low and Activation Code
      buf[20] = 0xf1; // Activation Code and SWD Line Reset
      buf[21] = 0xff;
      buf[22] = 0xff;
      buf[23] = 0xff;
      buf[24] = 0xff;
      buf[25] = 0xff;
      buf[26] = 0x3f;

      dbg_dap_cmd(buf, sizeof(buf), 27);
      check(DAP_OK == buf[0], "SWJ_SEQUENCE failed");

      // Target Select
      buf[0] = ID_DAP_SWD_SEQUENCE;
      buf[1] = 5; // Request Count
      // 1
      buf[2] = SWD_SEQUENCE_COUNT(7 * 8);
      buf[3] = 0xff;
      buf[4] = 0xff;
      buf[5] = 0xff;
      buf[6] = 0xff;
      buf[7] = 0xff;
      buf[8] = 0xff;
      buf[9] = 0x3f;
      // 2
      buf[10] = SWD_SEQUENCE_COUNT(8);
      buf[11] = 0x99; // DP, Write, TARGETSEL
      // 3
      buf[12] = SWD_SEQUENCE_COUNT(5) | SWD_SEQUENCE_DIN;
      // 4
      buf[13] = SWD_SEQUENCE_COUNT(32+1);
      buf[14] = dap_target_id >> 0;
      buf[15] = dap_target_id >> 8;
      buf[16] = dap_target_id >> 16;
      buf[17] = dap_target_id >> 24;
      buf[18] = dap_parity(dap_target_id);
      // 5
      buf[19] = SWD_SEQUENCE_COUNT(2);
      buf[20] = 0x00;

      dbg_dap_cmd(buf, sizeof(buf), 21);
      check(DAP_OK == buf[0], "SWD_SEQUENCE failed");

      dap_read_idcode();
    }
    else
    {
      error_exit("internal: unknown dap_dp_version value (%d)", dap_dp_version);
    }
  }
  else if (DAP_INTERFACE_JTAG == dap_interface)
  {
    buf[0] = ID_DAP_SWJ_SEQUENCE;
    buf[1] = (7 + 2 + 1 + 1) * 8;
    buf[2] = 0xff;
    buf[3] = 0xff;
    buf[4] = 0xff;
    buf[5] = 0xff;
    buf[6] = 0xff;
    buf[7] = 0xff;
    buf[8] = 0xff;
    buf[9] = 0x3c;
    buf[10] = 0xe7;
    buf[11] = 0xff;
    buf[12] = 0x00;

    dbg_dap_cmd(buf, sizeof(buf), 13);
    check(DAP_OK == buf[0], "SWJ_SEQUENCE failed");
  }
  else
  {
    error_exit("no interface selected in dap_reset_link()");
  }

  dap_add_req(TRANSFER_TYPE_WRITE_REG, TRANSFER_SIZE_WORD, SWD_DP_W_ABORT,
      DP_ABORT_STKCMPCLR | DP_ABORT_STKERRCLR | DP_ABORT_ORUNERRCLR | DP_ABORT_WDERRCLR);
  dap_add_req(TRANSFER_TYPE_WRITE_REG, TRANSFER_SIZE_WORD, SWD_DP_W_SELECT,
      DP_SELECT_APBANKSEL(0) | DP_SELECT_APSEL(0));
  dap_add_req(TRANSFER_TYPE_WRITE_REG, TRANSFER_SIZE_WORD, SWD_DP_CTRL_STAT,
      DP_CST_CDBGPWRUPREQ | DP_CST_CSYSPWRUPREQ | DP_CST_MASKLANE(0xf));
  dap_transfer();
}

//-----------------------------------------------------------------------------
void dap_clear_pwrup_req(void)
{
  dap_add_req(TRANSFER_TYPE_WRITE_REG, TRANSFER_SIZE_WORD, SWD_DP_CTRL_STAT, 0);
  dap_transfer();
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

  sleep_ms(10);
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
static void dap_add_req(int type, int size, uint32_t addr, uint32_t data)
{
  dap_request_t req;
  req.type = type;
  req.size = size;
  req.addr = addr;
  req.data = data;
  assert(dap_request_count < TRANSFER_SIZE);
  dap_request[dap_request_count++] = req;
}

//-----------------------------------------------------------------------------
void dap_read_byte_req(uint32_t addr)
{
  dap_add_req(TRANSFER_TYPE_READ, TRANSFER_SIZE_BYTE, addr, 0);
}

//-----------------------------------------------------------------------------
void dap_read_half_req(uint32_t addr)
{
  dap_add_req(TRANSFER_TYPE_READ, TRANSFER_SIZE_HALF, addr, 0);
}

//-----------------------------------------------------------------------------
void dap_read_word_req(uint32_t addr)
{
  dap_add_req(TRANSFER_TYPE_READ, TRANSFER_SIZE_WORD, addr, 0);
}

//-----------------------------------------------------------------------------
void dap_write_byte_req(uint32_t addr, uint32_t data)
{
  dap_add_req(TRANSFER_TYPE_WRITE, TRANSFER_SIZE_BYTE, addr, data);
}

//-----------------------------------------------------------------------------
void dap_write_half_req(uint32_t addr, uint32_t data)
{
  dap_add_req(TRANSFER_TYPE_WRITE, TRANSFER_SIZE_HALF, addr, data);
}

//-----------------------------------------------------------------------------
void dap_write_word_req(uint32_t addr, uint32_t data)
{
  dap_add_req(TRANSFER_TYPE_WRITE, TRANSFER_SIZE_WORD, addr, data);
}

//-----------------------------------------------------------------------------
void dap_read_idcode_req(void)
{
  dap_add_req(TRANSFER_TYPE_READ_REG, TRANSFER_SIZE_WORD, SWD_DP_R_IDCODE, 0);
}

//-----------------------------------------------------------------------------
void dap_readback_req(void)
{
  assert(dap_request_count > 0);
  assert(dap_request[dap_request_count-1].type == TRANSFER_TYPE_WRITE);
  dap_request[dap_request_count-1].type = TRANSFER_TYPE_WRITE_READ;
}

//-----------------------------------------------------------------------------
static uint32_t to_lane(int size, uint32_t addr, uint32_t data)
{
  if (TRANSFER_SIZE_WORD == size)
    return data;
  else if (TRANSFER_SIZE_HALF == size)
    return (data << ((addr & 2) * 8));
  else if (TRANSFER_SIZE_BYTE == size)
    return (data << ((addr & 3) * 8));
  else
    return 0;
}

//-----------------------------------------------------------------------------
static uint32_t from_lane(int size, uint32_t addr, uint32_t data)
{
  if (TRANSFER_SIZE_WORD == size)
    return data;
  else if (TRANSFER_SIZE_HALF == size)
    return (data >> ((addr & 2) * 8)) & 0xffff;
  else if (TRANSFER_SIZE_BYTE == size)
    return (data >> ((addr & 3) * 8)) & 0xff;
  else
    return 0;
}

//-----------------------------------------------------------------------------
static void append_word(uint32_t value)
{
  dap_buf[dap_buf_size + 0] = value & 0xff;
  dap_buf[dap_buf_size + 1] = (value >> 8) & 0xff;
  dap_buf[dap_buf_size + 2] = (value >> 16) & 0xff;
  dap_buf[dap_buf_size + 3] = (value >> 24) & 0xff;
  dap_buf_size += sizeof(uint32_t);
}

//-----------------------------------------------------------------------------
static bool buffer_request(dap_request_t *req)
{
  int packet_size = dbg_get_packet_size();
  int buf_size, ops_size, response_size, address_inc;
  uint32_t address, csw;
  bool set_address;

  buf_size      = dap_buf_size;
  ops_size      = dap_ops_size;
  response_size = dap_response_size;
  set_address   = dap_set_address;
  address_inc   = dap_address_inc;
  address       = dap_address;
  csw           = dap_csw;

  if (TRANSFER_TYPE_READ == req->type || TRANSFER_TYPE_WRITE == req->type ||
      TRANSFER_TYPE_WRITE_READ == req->type)
  {
    dap_csw = AP_CSW_DBGSWENABLE | AP_CSW_PROT(0x23);

    if (TRANSFER_SIZE_BYTE == req->size)
    {
      dap_csw |= AP_CSW_SIZE_BYTE;
      dap_address_inc = sizeof(uint8_t);
    }
    else if (TRANSFER_SIZE_HALF == req->size)
    {
      dap_csw |= AP_CSW_SIZE_HALF;
      dap_address_inc = sizeof(uint16_t);
    }
    else if (TRANSFER_SIZE_WORD == req->size)
    {
      dap_csw |= AP_CSW_SIZE_WORD;
      dap_address_inc = sizeof(uint32_t);
    }

    if (TRANSFER_TYPE_WRITE_READ == req->type)
      dap_address_inc = 0;
    else
      dap_csw |= AP_CSW_ADDRINC_SINGLE;

    if (dap_csw != csw)
    {
      dap_buf[dap_buf_size++] = SWD_AP_CSW;
      append_word(dap_csw);
      dap_ops[dap_ops_size++] = OP_SIZE;
    }

    if (dap_set_address || dap_address != req->addr || 0 == (dap_address & 0x3ff))
    {
      dap_buf[dap_buf_size++] = SWD_AP_TAR;
      append_word(req->addr);
      dap_ops[dap_ops_size++] = OP_ADDRESS;
      dap_address = req->addr;
      dap_set_address = false;
    }

    if (TRANSFER_TYPE_WRITE == req->type || TRANSFER_TYPE_WRITE_READ == req->type)
    {
      dap_buf[dap_buf_size++] = SWD_AP_DRW;
      append_word(to_lane(req->size, req->addr, req->data));
      dap_ops[dap_ops_size++] = (TRANSFER_TYPE_WRITE == req->type) ? OP_WRITE : OP_SKIP;
    }

    if (TRANSFER_TYPE_READ == req->type || TRANSFER_TYPE_WRITE_READ == req->type)
    {
      dap_buf[dap_buf_size++] = SWD_AP_DRW | DAP_TRANSFER_RnW;
      dap_ops[dap_ops_size++] = OP_READ;
      dap_response_size += sizeof(uint32_t);
    }

    dap_address += dap_address_inc;
  }
  else if (TRANSFER_TYPE_WRITE_REG == req->type)
  {
    dap_buf[dap_buf_size++] = req->addr;
    append_word(req->data);
    dap_ops[dap_ops_size++] = OP_WRITE;
  }
  else // TRANSFER_TYPE_READ_REG
  {
    dap_buf[dap_buf_size++] = req->addr | DAP_TRANSFER_RnW;
    dap_ops[dap_ops_size++] = OP_READ;
    dap_response_size += sizeof(uint32_t);
  }

  if (dap_buf_size > packet_size || dap_response_size > packet_size || dap_ops_size > 255)
  {
    dap_buf_size      = buf_size;
    dap_ops_size      = ops_size;
    dap_response_size = response_size;
    dap_set_address   = set_address;
    dap_address_inc   = address_inc;
    dap_address       = address;
    dap_csw           = csw;
    return false;
  }

  return true;
}

//-----------------------------------------------------------------------------
void dap_transfer(void)
{
  int count, status;
  uint32_t *data;

  dap_response_count = 0;
  dap_csw = 0;

  while (dap_response_count < dap_request_count)
  {
    dap_buf[0] = ID_DAP_TRANSFER;
    dap_buf[1] = dap_jtag_index;
    dap_buf[2] = 0; // Request size (placeholder)

    dap_buf_size = 3;
    dap_ops_size = 0;
    dap_response_size = 2; // count and status

    for (int i = dap_response_count; i < dap_request_count; i++)
    {
      if (!buffer_request(&dap_request[i]))
        break;
    }

    dap_buf[2] = dap_ops_size;

    //verbose("--- %d / %d, req_cnt = %d, resp_cnt = %d, resp_size = %d\n",
    //  dap_ops_size, dap_buf_size, dap_request_count, dap_response_count, dap_response_size);

    dbg_dap_cmd(dap_buf, sizeof(dap_buf), dap_buf_size);

    count  = dap_buf[0];
    status = dap_buf[1];
    data   = (uint32_t *)&dap_buf[2];

    if (dap_ops_size != count || DAP_TRANSFER_OK != status)
      error_exit("invalid response during transfer (count = %d/%d, status = %d)", count, dap_ops_size, status);

    for (int i = 0; i < count; i++)
    {
      dap_request_t *req = &dap_request[dap_response_count];

      if (OP_READ == dap_ops[i])
      {
        dap_response[dap_response_count++] = from_lane(req->size, req->addr, *data);
        data++;
      }
      else if (OP_WRITE == dap_ops[i])
      {
        dap_response[dap_response_count++] = req->data;
      }
    }
  }

  dap_request_count = 0;
}

//-----------------------------------------------------------------------------
uint32_t dap_get_response(int index)
{
  assert(index < dap_response_count);
  return dap_response[index];
}

//-----------------------------------------------------------------------------
uint8_t dap_read_byte(uint32_t addr)
{
  dap_read_byte_req(addr);
  dap_transfer();
  return dap_response[0];
}

//-----------------------------------------------------------------------------
uint16_t dap_read_half(uint32_t addr)
{
  dap_read_half_req(addr);
  dap_transfer();
  return dap_response[0];
}

//-----------------------------------------------------------------------------
uint32_t dap_read_word(uint32_t addr)
{
  dap_read_word_req(addr);
  dap_transfer();
  return dap_response[0];
}

//-----------------------------------------------------------------------------
void dap_write_byte(uint32_t addr, uint8_t data)
{
  dap_write_byte_req(addr, data);
  dap_transfer();
}

//-----------------------------------------------------------------------------
void dap_write_half(uint32_t addr, uint16_t data)
{
  dap_write_half_req(addr, data);
  dap_transfer();
}

//-----------------------------------------------------------------------------
void dap_write_word(uint32_t addr, uint32_t data)
{
  dap_write_word_req(addr, data);
  dap_transfer();
}

//-----------------------------------------------------------------------------
void dap_read_block(uint32_t addr, uint8_t *data, int size)
{
  uint32_t ptr = addr;
  uint32_t rem = size;
  int cnt = 0;

  if (size == 0)
    return;

  while (rem && (ptr % sizeof(uint32_t)))
  {
    dap_read_byte_req(ptr);
    ptr += sizeof(uint8_t);
    rem -= sizeof(uint8_t);
  }

  while (rem >= sizeof(uint32_t))
  {
    dap_read_word_req(ptr);
    ptr += sizeof(uint32_t);
    rem -= sizeof(uint32_t);
  }

  while (rem)
  {
    dap_read_byte_req(ptr);
    ptr += sizeof(uint8_t);
    rem -= sizeof(uint8_t);
  }

  dap_transfer();

  while (size && (addr % sizeof(uint32_t)))
  {
    *data = dap_response[cnt++];
    data += sizeof(uint8_t);
    addr += sizeof(uint8_t);
    size -= sizeof(uint8_t);
  }

  while (size >= (int)sizeof(uint32_t))
  {
    *(uint32_t *)data = dap_response[cnt++];
    data += sizeof(uint32_t);
    size -= sizeof(uint32_t);
  }

  while (size)
  {
    *data = dap_response[cnt++];
    data += sizeof(uint8_t);
    size -= sizeof(uint8_t);
  }
}

//-----------------------------------------------------------------------------
void dap_write_block(uint32_t addr, uint8_t *data, int size)
{
  if (size == 0)
    return;

  while (size && (addr % sizeof(uint32_t)))
  {
    dap_write_byte_req(addr, *data);
    data += sizeof(uint8_t);
    addr += sizeof(uint8_t);
    size -= sizeof(uint8_t);
  }

  while (size >= (int)sizeof(uint32_t))
  {
    dap_write_word_req(addr, *(uint32_t *)data);
    data += sizeof(uint32_t);
    addr += sizeof(uint32_t);
    size -= sizeof(uint32_t);
  }

  while (size)
  {
    dap_write_byte_req(addr, *data);
    data += sizeof(uint8_t);
    addr += sizeof(uint8_t);
    size -= sizeof(uint8_t);
  }

  dap_transfer();
}

//-----------------------------------------------------------------------------
uint32_t dap_read_idcode(void)
{
  if (DAP_INTERFACE_SWD == dap_interface)
  {
    dap_read_idcode_req();
    dap_transfer();
    return dap_response[0];
  }
  else if (DAP_INTERFACE_JTAG == dap_interface)
  {
    uint8_t buf[16];

    buf[0] = ID_DAP_JTAG_IDCODE;
    buf[1] = dap_jtag_index;

    dbg_dap_cmd(buf, sizeof(buf), 2);
    check(DAP_OK == buf[0], "JTAG_IDCODE failed");

    return *((uint32_t *)&buf[1]);
  }

  return 0;
}

//-----------------------------------------------------------------------------
static void dap_jtag_add_req(int tdi, int tms, int tdo)
{
  dap_jtag_request_t req;
  req.tdi = tdi ? 1 : 0;
  req.opt = (tms ? JTAG_SEQUENCE_TMS : 0) | (tdo ? JTAG_SEQUENCE_TDO : 0);
  assert(dap_request_count < TRANSFER_SIZE);
  dap_jtag_request[dap_jtag_request_count++] = req;
}

//-----------------------------------------------------------------------------
void dap_jtag_clk(int tdi, int tms)
{
  dap_jtag_add_req(tdi, tms, 0);
}

//-----------------------------------------------------------------------------
void dap_jtag_clk_read(int tdi, int tms)
{
  dap_jtag_add_req(tdi, tms, 1);
}

//-----------------------------------------------------------------------------
void dap_jtag_flush(void)
{
  uint8_t buf[DBG_MAX_EP_SIZE];
  int tdo_size[DBG_MAX_EP_SIZE / 2];
  int tdo_count, req_count, req_size, index, remaining;

  if (0 == dap_jtag_request_count)
    return;

  memset(dap_jtag_response_buf, 0, sizeof(dap_jtag_response_buf));
  dap_jtag_response_count = 0;

  memset(buf, 0, sizeof(buf));

  index     = 0;
  tdo_count = 0;
  req_count = 0;
  req_size  = 2; // Command and Count
  remaining = dbg_get_packet_size() - req_size - 1;

  while (index < dap_jtag_request_count)
  {
    int count = 0;
    int size;

    for (int i = index; i < dap_jtag_request_count; i++, count++)
    {
      if (dap_jtag_request[i].opt != dap_jtag_request[index].opt)
        break;
    }

    if (count > 64)
      count = 64;

    if (count > (remaining * 8))
      count = remaining * 8;

    buf[req_size] = JTAG_SEQUENCE_COUNT(count) | dap_jtag_request[index].opt;

    for (int i = 0; i < count; i++)
      buf[req_size + 1 + i / 8] |= (dap_jtag_request[index + i].tdi << (i % 8));

    if (dap_jtag_request[index].opt & JTAG_SEQUENCE_TDO)
      tdo_size[tdo_count++] = count;

    size = 1 + (count + 7) / 8;
    req_size += size;
    remaining -= size;

    req_count++;
    index += count;

    if (remaining < 2 || index == dap_jtag_request_count)
    {
      int tdo_index = 1;

      buf[0] = ID_DAP_JTAG_SEQUENCE;
      buf[1] = req_count;

      dbg_dap_cmd(buf, sizeof(buf), req_size);
      check(DAP_OK == buf[0], "JTAG_SEQUENCE failed");

      for (int i = 0; i < tdo_count; i++)
      {
        for (int j = 0; j < tdo_size[i]; j++)
        {
          int bit = (buf[tdo_index + j / 8] & (1 << (j % 8))) ? 1 : 0;
          dap_jtag_response_buf[dap_jtag_response_count / 8] |= (bit << (dap_jtag_response_count % 8));
          dap_jtag_response_count++;
        }

        tdo_index += (tdo_size[i] + 7) / 8;
      }

      memset(buf, 0, sizeof(buf));

      tdo_count = 0;
      req_count = 0;
      req_size  = 2; // Command and Count
      remaining = dbg_get_packet_size() - req_size - 1;
    }
  }

  dap_jtag_request_count = 0;
}

//-----------------------------------------------------------------------------
void dap_jtag_read(int offset, uint8_t *data, int size)
{
  dap_jtag_flush();

  for (int i = 0; i < size; i++)
  {
    int index = offset + i;
    int bit;

    assert(index < dap_jtag_response_count);

    bit = (dap_jtag_response_buf[index / 8] & (1 << (index % 8))) ? 1 : 0;

    if ((i % 8) == 0)
      data[i / 8] = 0;

    data[i / 8] |= (bit << (i % 8));
  }
}

//-----------------------------------------------------------------------------
void dap_jtag_idle(int count)
{
  for (int i = 0; i < count; i++)
    dap_jtag_clk(0, 0);
}

//-----------------------------------------------------------------------------
void dap_jtag_reset(void)
{
  for (int i = 0; i < 16; i++)
    dap_jtag_clk(0, 1);

  dap_jtag_clk(0, 0);
}

//-----------------------------------------------------------------------------
void dap_jtag_write_ir(int ir, int size)
{
  dap_jtag_clk(0, 1);
  dap_jtag_clk(0, 1);
  dap_jtag_clk(0, 0);
  dap_jtag_clk(0, 0);

  for (int i = 0; i < size; i++)
    dap_jtag_clk((ir >> i) & 1, i == (size-1));

  dap_jtag_clk(0, 1);
  dap_jtag_clk(0, 0);
}

//-----------------------------------------------------------------------------
void dap_jtag_write_dr(uint8_t *data, int size)
{
  dap_jtag_clk(0, 1);
  dap_jtag_clk(0, 0);
  dap_jtag_clk(0, 0);

  for (int i = 0; i < size; i++)
    dap_jtag_clk((data[i / 8] >> (i % 8)) & 1, i == (size-1));

  dap_jtag_clk(0, 1);
  dap_jtag_clk(0, 0);
}

//-----------------------------------------------------------------------------
void dap_jtag_read_dr(uint8_t *data, int size)
{
  dap_jtag_clk(0, 1);
  dap_jtag_clk(0, 0);
  dap_jtag_clk(0, 0);

  for (int i = 0; i < size; i++)
    dap_jtag_clk_read(0, i == (size-1));

  dap_jtag_clk(0, 1);
  dap_jtag_clk(0, 0);

  dap_jtag_read(0, data, size);
}

//-----------------------------------------------------------------------------
int dap_jtag_scan_chain(uint32_t *idcode, int size)
{
  int count = 0;

  dap_jtag_reset();

  dap_jtag_clk(1, 1);
  dap_jtag_clk(1, 0);
  dap_jtag_clk(1, 0);

  for (int i = 0; i < size; i++)
  {
    for (int j = 0; j < 32; j++)
      dap_jtag_clk_read(0, 0);

    dap_jtag_read(0, (uint8_t *)&idcode[count], 32);

    if (idcode[count])
      count++;
    else
      break;
  }

  dap_jtag_clk(1, 1);
  dap_jtag_clk(1, 1);
  dap_jtag_clk(1, 0);

  return count;
}


