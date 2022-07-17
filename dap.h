// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2013-2022, Alex Taradov <alex@taradov.com>. All rights reserved.

#ifndef _DAP_H_
#define _DAP_H_

/*- Includes ----------------------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>

/*- Definitions -------------------------------------------------------------*/
enum
{
  DAP_INFO_VENDOR        = 0x01,
  DAP_INFO_PRODUCT       = 0x02,
  DAP_INFO_SER_NUM       = 0x03,
  DAP_INFO_CMSIS_DAP_VER = 0x04,
  DAP_INFO_DEVICE_VENDOR = 0x05,
  DAP_INFO_DEVICE_NAME   = 0x06,
  DAP_INFO_BOARD_VENDOR  = 0x07,
  DAP_INFO_BOARD_NAME    = 0x08,
  DAP_INFO_FW_VER        = 0x09,
  DAP_INFO_CAPABILITIES  = 0xf0,
  DAP_INFO_TDT           = 0xf1,
  DAP_INFO_UART_RX_SIZE  = 0xfb,
  DAP_INFO_UART_TX_SIZE  = 0xfc,
  DAP_INFO_SWO_BUF_SIZE  = 0xfd,
  DAP_INFO_PACKET_COUNT  = 0xfe,
  DAP_INFO_PACKET_SIZE   = 0xff,
};

enum
{
  DAP_CAP_SWD            = (1 << 0),
  DAP_CAP_JTAG           = (1 << 1),
  DAP_CAP_SWO_UART       = (1 << 2),
  DAP_CAP_SWO_MANCHESTER = (1 << 3),
  DAP_CAP_ATOMIC_CMD     = (1 << 4),
  DAP_CAP_TDT            = (1 << 5),
  DAP_CAP_SWO_STREAMING  = (1 << 6),
};

enum
{
  DAP_INTERFACE_NONE,
  DAP_INTERFACE_SWD,
  DAP_INTERFACE_JTAG,
};

#define DAP_INVALID_TARGET_ID  0xffffffff

/*- Prototypes --------------------------------------------------------------*/
void dap_set_dp_version(int version);
void dap_set_target_id(uint32_t id);

void dap_led(int index, int state);
void dap_connect(int interf);
void dap_disconnect(void);
void dap_swj_clock(uint32_t clock);
void dap_transfer_configure(uint8_t idle, uint16_t retry, uint16_t match_retry);
void dap_swd_configure(int cfg);
void dap_jtag_configure(int count, int *ir_len);
void dap_jtag_set_index(int index);
int dap_info(int info, uint8_t *data, int size);
void dap_reset_link(void);
void dap_clear_pwrup_req(void);

void dap_reset_target(void);
void dap_reset_target_hw(int state);
void dap_reset_pin(int state);

uint32_t dap_read_reg(uint8_t reg);
void dap_write_reg(uint8_t reg, uint32_t data);
uint8_t dap_read_byte(uint32_t addr);
uint16_t dap_read_half(uint32_t addr);
uint32_t dap_read_word(uint32_t addr);
void dap_write_byte(uint32_t addr, uint8_t data);
void dap_write_half(uint32_t addr, uint16_t data);
void dap_write_word(uint32_t addr, uint32_t data);
void dap_read_block(uint32_t addr, uint8_t *data, int size);
void dap_write_block(uint32_t addr, uint8_t *data, int size);

void dap_read_byte_req(uint32_t addr);
void dap_read_half_req(uint32_t addr);
void dap_read_word_req(uint32_t addr);
void dap_write_byte_req(uint32_t addr, uint32_t data);
void dap_write_half_req(uint32_t addr, uint32_t data);
void dap_write_word_req(uint32_t addr, uint32_t data);
void dap_read_idcode_req(void);
void dap_readback_req(void);
void dap_transfer(void);
uint32_t dap_get_response(int index);

uint32_t dap_read_idcode(void);

void dap_jtag_clk(int tdi, int tms);
void dap_jtag_clk_read(int tdi, int tms);
void dap_jtag_flush(void);
void dap_jtag_read(int offset, uint8_t *data, int size);
void dap_jtag_idle(int count);
void dap_jtag_reset(void);
void dap_jtag_write_ir(int ir, int size);
void dap_jtag_write_dr(uint8_t *data, int size);
void dap_jtag_read_dr(uint8_t *data, int size);
int dap_jtag_scan_chain(uint32_t *idcode, int size);

#endif // _DAP_H_

