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
  DAP_INFO_FW_VER        = 0x04,
  DAP_INFO_DEVICE_VENDOR = 0x05,
  DAP_INFO_DEVICE_NAME   = 0x06,
  DAP_INFO_CAPABILITIES  = 0xf0,
  DAP_INFO_TDT           = 0xf1,
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

/*- Prototypes --------------------------------------------------------------*/
void dap_led(int index, int state);
void dap_connect(void);
void dap_disconnect(void);
void dap_swj_clock(uint32_t clock);
void dap_transfer_configure(uint8_t idle, uint16_t count, uint16_t retry);
void dap_swd_configure(uint8_t cfg);
int dap_info(int info, uint8_t *data, int size);
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
void dap_reset_link(void);
uint32_t dap_read_idcode(void);

#endif // _DAP_H_

