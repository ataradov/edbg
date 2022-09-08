// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022, Alex Taradov <alex@taradov.com>. All rights reserved.

/*- Includes ----------------------------------------------------------------*/
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "target.h"
#include "edbg.h"
#include "dap.h"

/*- Definitions -------------------------------------------------------------*/
#define FLASH_ADDR             0x13000000 // XIP_NOCACHE_NOALLOC
#define FLASH_SECTOR_SIZE      4096
#define FLASH_PAGE_SIZE        256

#define RAM_ADDR               0x20000000
#define RAM_SIZE               (256*1024)
#define RAM_HALF_ADDR          (RAM_ADDR + (RAM_SIZE / 2))

#define ROM_REVISION_ADDR      0x13

#define DHCSR                  0xe000edf0
#define DHCSR_DEBUGEN          (1 << 0)
#define DHCSR_HALT             (1 << 1)
#define DHCSR_DBGKEY           (0xa05f << 16)

#define DEMCR                  0xe000edfc
#define DEMCR_VC_CORERESET     (1 << 0)

#define AIRCR                  0xe000ed0c
#define AIRCR_VECTKEY          (0x05fa << 16)
#define AIRCR_SYSRESETREQ      (1 << 2)

#define TARGET_ID_CORE0        0x01002927
#define TARGET_ID_CORE1        0x11002927
#define TARGET_ID_RESCUE       0xf1002927

#define QSPI_CTRLR0            0x18000000
#define QSPI_CTRLR1            0x18000004
#define QSPI_SSIENR            0x18000008
#define QSPI_MWCR              0x1800000C
#define QSPI_SER               0x18000010
#define QSPI_BAUDR             0x18000014
#define QSPI_TXFTLR            0x18000018
#define QSPI_RXFTLR            0x1800001C
#define QSPI_TXFLR             0x18000020
#define QSPI_RXFLR             0x18000024
#define QSPI_SR                0x18000028
#define QSPI_IMR               0x1800002C
#define QSPI_ISR               0x18000030
#define QSPI_RISR              0x18000034
#define QSPI_TXOICR            0x18000038
#define QSPI_RXOICR            0x1800003C
#define QSPI_RXUICR            0x18000040
#define QSPI_MSTICR            0x18000044
#define QSPI_ICR               0x18000048
#define QSPI_DMACR             0x1800004C
#define QSPI_DMATDLR           0x18000050
#define QSPI_DMARDLR           0x18000054
#define QSPI_IDR               0x18000058
#define QSPI_DR0               0x18000060
#define QSPI_RX_SAMPLE_DLY     0x180000F0
#define QSPI_SPI_CTRLR0        0x180000F4
#define QSPI_TXD_DRIVE_EDGE    0x180000F8

#define QSPI_SR_BUSY                       (1 << 0)
#define QSPI_SR_TFNF                       (1 << 1)
#define QSPI_SR_TFE                        (1 << 2)
#define QSPI_SR_RFNE                       (1 << 3)
#define QSPI_SR_RFF                        (1 << 4)
#define QSPI_SR_TXE                        (1 << 5)
#define QSPI_SR_DCOL                       (1 << 6)

#define QSPI_CTRLR0_SPI_FRF_STD            (0 << 21)
#define QSPI_CTRLR0_SPI_FRF_DUAL           (1 << 21)
#define QSPI_CTRLR0_SPI_FRF_QUAD           (2 << 21)

#define QSPI_CTRLR0_TMOD_TX_AND_RX         (0 << 8)
#define QSPI_CTRLR0_TMOD_TX_ONLY           (1 << 8)
#define QSPI_CTRLR0_TMOD_RX_ONLY           (2 << 8)
#define QSPI_CTRLR0_TMOD_EEPROM_READ       (3 << 8)

#define QSPI_CTRLR0_DFS_32(x)              ((x) << 16)

#define QSPI_SSIENR_SSI_EN                 (1 << 0)

#define QSPI_SPI_CTRLR0_TRANS_TYPE_1C1A    (0 << 0) // Command and address both in standard SPI frame format
#define QSPI_SPI_CTRLR0_TRANS_TYPE_1C2A    (1 << 0) // Command in standard SPI format, address in format specified by FRF
#define QSPI_SPI_CTRLR0_TRANS_TYPE_2C2A    (2 << 0) // Command and address both in format specified by FRF (e.g. Dual-SPI)

#define QSPI_SPI_CTRLR0_ADDR_L(x)          ((x) << 2)

#define QSPI_SPI_CTRLR0_INST_L_NONE        (0 << 8)
#define QSPI_SPI_CTRLR0_INST_L_4B          (1 << 8)
#define QSPI_SPI_CTRLR0_INST_L_8B          (2 << 8)
#define QSPI_SPI_CTRLR0_INST_L_16B         (3 << 8)

#define QSPI_SPI_CTRLR0_XIP_CMD(x)         ((x) << 24)

#define QSPI_SPI_CTRLR0_WAIT_CYCLES(x)     ((x) << 11)

#define QSPI_DMACR_RDMAE                   (1 << 0)
#define QSPI_DMACR_TDMAE                   (1 << 1)

#define GPIO_QSPI_SCLK_CTRL                0x40018004
#define GPIO_QSPI_SS_CTRL                  0x4001800C
#define GPIO_QSPI_SD0_CTRL                 0x40018014
#define GPIO_QSPI_SD1_CTRL                 0x4001801C
#define GPIO_QSPI_SD2_CTRL                 0x40018024
#define GPIO_QSPI_SD3_CTRL                 0x4001802C

#define GPIO_QSPI_xx_CTRL_OUTOVER_NORMAL   (0 << 8)
#define GPIO_QSPI_xx_CTRL_OUTOVER_INVERT   (1 << 8)
#define GPIO_QSPI_xx_CTRL_OUTOVER_LOW      (2 << 8)
#define GPIO_QSPI_xx_CTRL_OUTOVER_HIGH     (3 << 8)

#define GPIO_QSPI_xx_CTRL_FUNCSEL(x)       ((x) << 0)

#define RESETS_RESET_CLR                   (0x4000C000 + 0x3000)

#define RESETS_RESET_DMA                   (1 << 2)
#define RESETS_RESET_IO_QSPI               (1 << 6)
#define RESETS_RESET_PADS_QSPI             (1 << 9)

#define PADS_QSPI_SCLK                     0x40020004
#define PADS_QSPI_SD0                      0x40020008
#define PADS_QSPI_SD1                      0x4002000C
#define PADS_QSPI_SD2                      0x40020010
#define PADS_QSPI_SD3                      0x40020014
#define PADS_QSPI_SS                       0x40020018

#define PADS_QSPI_xx_SLEWFAST              (1 << 0)
#define PADS_QSPI_xx_SCHMITT               (1 << 1)
#define PADS_QSPI_xx_PDE                   (1 << 2)
#define PADS_QSPI_xx_PUE                   (1 << 3)
#define PADS_QSPI_xx_DRIVE_2mA             (0 << 4)
#define PADS_QSPI_xx_DRIVE_4mA             (1 << 4)
#define PADS_QSPI_xx_DRIVE_8mA             (2 << 4)
#define PADS_QSPI_xx_DRIVE_12mA            (3 << 4)
#define PADS_QSPI_xx_IE                    (1 << 6)
#define PADS_QSPI_xx_OD                    (1 << 7)

#define PADS_QSPI_DEFAULT                  (PADS_QSPI_xx_IE | PADS_QSPI_xx_DRIVE_4mA | PADS_QSPI_xx_SCHMITT)

#define SPI_FIFO_SIZE                      16

// Using Alias 1, so TRANS_COUNT write triggers the transfer
#define DMA_CH0_CTRL                       0x50000010
#define DMA_CH0_READ_ADDR                  0x50000014
#define DMA_CH0_WRITE_ADDR                 0x50000018
#define DMA_CH0_TRANS_COUNT                0x5000001c

#define DMA_CH1_CTRL                       0x50000050
#define DMA_CH1_READ_ADDR                  0x50000054
#define DMA_CH1_WRITE_ADDR                 0x50000058
#define DMA_CH1_TRANS_COUNT                0x5000005c

#define DMA_CHx_CTRL_EN                    (1 << 0)
#define DMA_CHx_CTRL_HIGH_PRIORITY         (1 << 1)
#define DMA_CHx_CTRL_DATA_SIZE_BYTE        (0 << 2)
#define DMA_CHx_CTRL_DATA_SIZE_HALF        (1 << 2)
#define DMA_CHx_CTRL_DATA_SIZE_WORD        (2 << 2)
#define DMA_CHx_CTRL_INCR_READ             (1 << 4)
#define DMA_CHx_CTRL_INCR_WRITE            (1 << 5)
#define DMA_CHx_CTRL_RING_SIZE(x)          ((x) << 6)
#define DMA_CHx_CTRL_RING_SEL              (1 << 10)
#define DMA_CHx_CTRL_CHAIN_TO(x)           ((x) << 11)
#define DMA_CHx_CTRL_TREQ_SEL(x)           ((x) << 15)
#define DMA_CHx_CTRL_IRQ_QUIET             (1 << 21)
#define DMA_CHx_CTRL_BSWAP                 (1 << 22)
#define DMA_CHx_CTRL_SNIFF_EN              (1 << 23)
#define DMA_CHx_CTRL_AHB_ERROR             (1 << 31)
#define DMA_CHx_CTRL_READ_ERROR            (1 << 30)
#define DMA_CHx_CTRL_WRITE_ERROR           (1 << 29)
#define DMA_CHx_CTRL_BUSY                  (1 << 24)

#define DMA_DREQ_XIP_SSITX                 38
#define DMA_DREQ_XIP_SSIRX                 39

#define FLASH_CMD_PAGE_PROGRAM     0x02
#define FLASH_CMD_READ_DATA        0x03
#define FLASH_CMD_READ_STATUS      0x05
#define FLASH_CMD_WRITE_ENABLE     0x06
#define FLASH_CMD_SECTOR_ERASE     0x20
#define FLASH_CMD_READ_SFDP        0x5a
#define FLASH_CMD_READ_JEDEC_ID    0x9f
#define FLASH_CMD_CHIP_ERASE       0xc7

#define STATUS_INTERVAL            4 // sectors

/*- Variables ---------------------------------------------------------------*/
static target_options_t target_options;
static int flash_cmd_sector_erase = FLASH_CMD_SECTOR_ERASE;
static int flash_cmd_read_data = FLASH_CMD_READ_DATA;
static int flash_wait_cycles = 0;
static bool flash_quad_mode = false;

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
static void spi_select_req(int ss)
{
  int value = ss ? GPIO_QSPI_xx_CTRL_OUTOVER_HIGH : GPIO_QSPI_xx_CTRL_OUTOVER_LOW;
  dap_write_word_req(GPIO_QSPI_SS_CTRL, value);
}

//-----------------------------------------------------------------------------
static void spi_select(int ss)
{
  spi_select_req(ss);
  dap_transfer();
}

//-----------------------------------------------------------------------------
static void spi_transfer(uint8_t *data, int size, int rx_skip)
{
  assert(rx_skip <= size);

  dap_write_block(RAM_ADDR, data, size);

  dap_write_word_req(DMA_CH0_WRITE_ADDR, RAM_HALF_ADDR);
  dap_write_word_req(DMA_CH0_TRANS_COUNT, size);

  dap_write_word_req(DMA_CH1_READ_ADDR, RAM_ADDR);
  dap_write_word_req(DMA_CH1_TRANS_COUNT, size);

  dap_transfer();

  while (dap_read_word(DMA_CH0_CTRL) & DMA_CHx_CTRL_BUSY);

  dap_read_block(RAM_HALF_ADDR + rx_skip, data, size - rx_skip);
}

//-----------------------------------------------------------------------------
static void spi_normal_mode(void)
{
  dap_write_word_req(QSPI_SSIENR, 0);
  dap_write_word_req(QSPI_BAUDR, 2);
  dap_write_word_req(QSPI_CTRLR0, QSPI_CTRLR0_SPI_FRF_STD | QSPI_CTRLR0_TMOD_TX_AND_RX | QSPI_CTRLR0_DFS_32(8-1));
  dap_write_word_req(QSPI_SER, 1);
  dap_write_word_req(QSPI_SSIENR, QSPI_SSIENR_SSI_EN);

  // Enable DMA
  dap_write_word_req(QSPI_DMACR, QSPI_DMACR_RDMAE | QSPI_DMACR_TDMAE);

  // Configure RX Channel
  dap_write_word_req(DMA_CH0_READ_ADDR, QSPI_DR0);
  dap_write_word_req(DMA_CH0_CTRL, DMA_CHx_CTRL_EN | DMA_CHx_CTRL_DATA_SIZE_BYTE |
      DMA_CHx_CTRL_INCR_WRITE | DMA_CHx_CTRL_CHAIN_TO(0) |
      DMA_CHx_CTRL_TREQ_SEL(DMA_DREQ_XIP_SSIRX) | DMA_CHx_CTRL_HIGH_PRIORITY);

  // Configure TX Channel
  dap_write_word_req(DMA_CH1_WRITE_ADDR, QSPI_DR0);
  dap_write_word_req(DMA_CH1_CTRL, DMA_CHx_CTRL_EN | DMA_CHx_CTRL_DATA_SIZE_BYTE |
      DMA_CHx_CTRL_INCR_READ | DMA_CHx_CTRL_CHAIN_TO(1) |
      DMA_CHx_CTRL_TREQ_SEL(DMA_DREQ_XIP_SSITX));

  dap_transfer();
}

//-----------------------------------------------------------------------------
static void spi_xip_mode(void)
{
  dap_write_word_req(GPIO_QSPI_SS_CTRL, GPIO_QSPI_xx_CTRL_OUTOVER_NORMAL);
  dap_write_word_req(QSPI_SSIENR, 0);
  dap_write_word_req(QSPI_CTRLR0, (flash_quad_mode ? QSPI_CTRLR0_SPI_FRF_QUAD : QSPI_CTRLR0_SPI_FRF_STD) |
      QSPI_CTRLR0_TMOD_EEPROM_READ | QSPI_CTRLR0_DFS_32(32-1));
  dap_write_word_req(QSPI_SPI_CTRLR0, QSPI_SPI_CTRLR0_XIP_CMD(flash_cmd_read_data) |
    QSPI_SPI_CTRLR0_ADDR_L(24 / 4) | QSPI_SPI_CTRLR0_INST_L_8B | QSPI_SPI_CTRLR0_TRANS_TYPE_1C1A |
    QSPI_SPI_CTRLR0_WAIT_CYCLES(flash_wait_cycles));
  dap_write_word_req(QSPI_SSIENR, QSPI_SSIENR_SSI_EN);

  dap_write_word_req(QSPI_DMACR, 0);

  dap_transfer();
}

//-----------------------------------------------------------------------------
static void flash_prepare(void)
{
  uint8_t buf[4];

  memset(buf, 0, sizeof(buf));

  // Exit XIP mode. There is no universal way, so we do the best we can using
  // the same procedure as used by the RP2040 ROM code.

  dap_write_word_req(RESETS_RESET_CLR, RESETS_RESET_PADS_QSPI | RESETS_RESET_IO_QSPI | RESETS_RESET_DMA);
  dap_write_word_req(GPIO_QSPI_SD0_CTRL, GPIO_QSPI_xx_CTRL_OUTOVER_NORMAL);
  dap_write_word_req(GPIO_QSPI_SD1_CTRL, GPIO_QSPI_xx_CTRL_OUTOVER_NORMAL);
  dap_write_word_req(GPIO_QSPI_SD2_CTRL, GPIO_QSPI_xx_CTRL_OUTOVER_NORMAL);
  dap_write_word_req(GPIO_QSPI_SD3_CTRL, GPIO_QSPI_xx_CTRL_OUTOVER_NORMAL);
  dap_write_word_req(GPIO_QSPI_SCLK_CTRL, GPIO_QSPI_xx_CTRL_OUTOVER_NORMAL);
  dap_write_word_req(GPIO_QSPI_SS_CTRL, GPIO_QSPI_xx_CTRL_OUTOVER_NORMAL);
  dap_transfer();

  spi_normal_mode();

  dap_write_word_req(PADS_QSPI_SD0, PADS_QSPI_DEFAULT | PADS_QSPI_xx_OD | PADS_QSPI_xx_PDE);
  dap_write_word_req(PADS_QSPI_SD1, PADS_QSPI_DEFAULT | PADS_QSPI_xx_OD | PADS_QSPI_xx_PDE);
  dap_write_word_req(PADS_QSPI_SD2, PADS_QSPI_DEFAULT | PADS_QSPI_xx_OD | PADS_QSPI_xx_PDE);
  dap_write_word_req(PADS_QSPI_SD3, PADS_QSPI_DEFAULT | PADS_QSPI_xx_OD | PADS_QSPI_xx_PDE);
  dap_transfer();

  spi_select(1);
  spi_transfer(buf, 4, 4);

  dap_write_word_req(PADS_QSPI_SD0, PADS_QSPI_DEFAULT | PADS_QSPI_xx_OD | PADS_QSPI_xx_PUE);
  dap_write_word_req(PADS_QSPI_SD1, PADS_QSPI_DEFAULT | PADS_QSPI_xx_OD | PADS_QSPI_xx_PUE);
  dap_write_word_req(PADS_QSPI_SD2, PADS_QSPI_DEFAULT | PADS_QSPI_xx_OD | PADS_QSPI_xx_PUE);
  dap_write_word_req(PADS_QSPI_SD3, PADS_QSPI_DEFAULT | PADS_QSPI_xx_OD | PADS_QSPI_xx_PUE);
  dap_transfer();

  spi_select(0);
  spi_transfer(buf, 4, 4);
  spi_select(1);

  dap_write_word_req(PADS_QSPI_SD0, PADS_QSPI_DEFAULT | PADS_QSPI_xx_PDE);
  dap_write_word_req(PADS_QSPI_SD1, PADS_QSPI_DEFAULT | PADS_QSPI_xx_PDE);
  dap_write_word_req(PADS_QSPI_SD2, PADS_QSPI_DEFAULT | PADS_QSPI_xx_PUE);
  dap_write_word_req(PADS_QSPI_SD3, PADS_QSPI_DEFAULT | PADS_QSPI_xx_PUE);
  dap_transfer();

  buf[0] = 0xff;
  buf[1] = 0xff;

  spi_select(0);
  spi_transfer(buf, 2, 2);
  spi_select(1);
}

//----------------------------------------------------------------------------
static int flash_get_size(void)
{
  uint8_t buf[128];
  int flash_size = 0;

  memset(buf, 0, sizeof(buf));

  buf[0] = FLASH_CMD_READ_SFDP;

  spi_select(0);
  spi_transfer(buf, 5 + 16, 5);
  spi_select(1);

  if (buf[0] == 'S' && buf[1] == 'F' && buf[2] == 'D' && buf[3] == 'P' && buf[8] == 0)
  {
    uint32_t w1, w2, w3;

    buf[0] = FLASH_CMD_READ_SFDP;
    buf[1] = buf[14];
    buf[2] = buf[13];
    buf[3] = buf[12];
    buf[4] = 0;

    spi_select(0);
    spi_transfer(buf, 5 + 16*4, 5);
    spi_select(1);

    w1 = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
    w2 = (buf[7] << 24) | (buf[6] << 16) | (buf[5] << 8) | buf[4];
    w3 = (buf[11] << 24) | (buf[10] << 16) | (buf[9] << 8) | buf[8];

    if ((w1 & 0x3) != 0x1)
      error_exit("4 KB erase is not supported");

    flash_cmd_sector_erase = (w1 >> 8) & 0xff;

    if (((w1 >> 17) & 0x3) != 0x0)
      error_exit("flash must support only 3-byte addressing");

    if (w1 & (1 << 22))
      flash_quad_mode = true;

    flash_cmd_read_data = (w3 >> 24) & 0xff;
    flash_wait_cycles   = (w3 >> 16) & 0x1f;

    if (w2 & 0x80000000)
      flash_size = (1ull << (w2 & 0x7fffffff)) / 8;
    else
      flash_size = (w2 + 1) / 8;
  }
  else
  {
    warning("no SFDP information found, using JEDEC ID as a fallback to detect the flash size");

    buf[0] = FLASH_CMD_READ_JEDEC_ID;

    spi_select(0);
    spi_transfer(buf, 4, 1);
    spi_select(1);

    flash_size = (1 << buf[2]);
  }

  if (flash_size < 256 || flash_size > 1024*1024*1024)
    flash_size = 0;

  return flash_size;
}

//-----------------------------------------------------------------------------
static bool flash_is_busy(void)
{
  spi_select_req(0);
  dap_write_word_req(QSPI_DR0, FLASH_CMD_READ_STATUS);
  dap_write_word_req(QSPI_DR0, 0);
  dap_read_word_req(QSPI_DR0);
  dap_read_word_req(QSPI_DR0);
  spi_select_req(1);
  dap_transfer();

  return (dap_get_response(4) & 1);
}

//-----------------------------------------------------------------------------
static void flash_write_enable(void)
{
  spi_select_req(0);
  dap_write_word_req(QSPI_DR0, FLASH_CMD_WRITE_ENABLE);
  dap_read_word_req(QSPI_DR0);
  spi_select_req(1);
  dap_transfer();
}

//-----------------------------------------------------------------------------
static void flash_erase_sector(int addr)
{
  uint8_t buf[4] = { flash_cmd_sector_erase, (addr >> 16) & 0xff, (addr >> 8) & 0xff, addr & 0xff };

  flash_write_enable();

  spi_select(0);
  spi_transfer(buf, 4, 4);
  spi_select(1);

  while (flash_is_busy());
}

//-----------------------------------------------------------------------------
static void flash_program_page(int addr, uint8_t *data)
{
  uint8_t buf[4 + FLASH_PAGE_SIZE];

  buf[0] = FLASH_CMD_PAGE_PROGRAM;
  buf[1] = addr >> 16;
  buf[2] = addr >> 8;
  buf[3] = addr;

  memcpy(&buf[4], data, FLASH_PAGE_SIZE);

  flash_write_enable();

  spi_select(0);
  spi_transfer(buf, sizeof(buf), sizeof(buf));
  spi_select(1);

  while (flash_is_busy());
}

//-----------------------------------------------------------------------------
static void target_select(target_options_t *options)
{
  uint32_t idr;
  int flash_size, rev;

  dap_set_dp_version(2);

  dap_set_target_id(TARGET_ID_RESCUE);
  dap_reset_link();
  dap_clear_pwrup_req();

  dap_set_target_id(TARGET_ID_CORE0);
  dap_reset_link();

  // Stop the core
  dap_write_word(DHCSR, DHCSR_DBGKEY | DHCSR_DEBUGEN | DHCSR_HALT);
  dap_write_word(DEMCR, DEMCR_VC_CORERESET);
  dap_write_word(AIRCR, AIRCR_VECTKEY | AIRCR_SYSRESETREQ);

  idr = dap_read_word(QSPI_IDR);

  check(idr == 0x51535049, "QSPI controller not found");

  rev = dap_read_byte(ROM_REVISION_ADDR);

  if (rev == 1 || rev == 2 || rev == 3)
    verbose("Target: RP2040 (Rev B%d)\n", rev-1);
  else
    error_exit("unknown target device (ROM revision = %d)", rev);

  flash_prepare();

  flash_size = flash_get_size();

  if (flash_size > 1024*1024)
    verbose("Flash size: %d MB\n", flash_size / (1024*1024));
  else if (flash_size > 0)
    verbose("Flash size: %d KB\n", flash_size / 1024);
  else
    error_exit("unknown flash device");

  check(flash_size <= 16*1024*1024, "flash size larger than 16 MB is not supported");

  target_options = *options;
  target_check_options(&target_options, flash_size, FLASH_SECTOR_SIZE);
}

//-----------------------------------------------------------------------------
static void target_deselect(void)
{
  dap_write_word(DEMCR, 0);
  dap_write_word(AIRCR, AIRCR_VECTKEY | AIRCR_SYSRESETREQ);

  target_free_options(&target_options);
}

//-----------------------------------------------------------------------------
static void target_erase(void)
{
  uint8_t buf[4];
  int cnt = 0;

  buf[0] = FLASH_CMD_CHIP_ERASE;

  flash_write_enable();

  spi_select(0);
  spi_transfer(buf, 1, 1);
  spi_select(1);

  while (flash_is_busy())
  {
    sleep_ms(100);

    if ((cnt++ % 10) == 0)
      verbose(".");
  }
}

//-----------------------------------------------------------------------------
static void target_lock(void)
{
  error_exit("locking is not supported for this target");
}

//-----------------------------------------------------------------------------
static void target_unlock(void)
{
  error_exit("unlocking is not supported for this target");
}

//-----------------------------------------------------------------------------
static void target_program(void)
{
  uint32_t addr = target_options.offset;
  uint32_t offs = 0;
  uint32_t number_of_pages;
  uint8_t *buf = target_options.file_data;
  uint32_t size = target_options.file_size;

  number_of_pages = (size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;

  for (uint32_t page = 0; page < number_of_pages; page++)
  {
    if (0 == (addr % FLASH_SECTOR_SIZE))
      flash_erase_sector(addr);

    flash_program_page(addr, &buf[offs]);

    addr += FLASH_PAGE_SIZE;
    offs += FLASH_PAGE_SIZE;

    if (0 == (addr % (FLASH_SECTOR_SIZE * STATUS_INTERVAL)))
      verbose(".");
  }
}

//-----------------------------------------------------------------------------
static void target_verify(void)
{
  uint32_t addr = FLASH_ADDR + target_options.offset;
  uint32_t block_size;
  uint32_t offs = 0;
  uint8_t *bufb;
  uint8_t *bufa = target_options.file_data;
  uint32_t size = target_options.file_size;
  int sector = 0;

  bufb = buf_alloc(FLASH_SECTOR_SIZE);

  spi_xip_mode();

  while (size)
  {
    dap_read_block(addr, bufb, FLASH_SECTOR_SIZE);

    block_size = (size > FLASH_SECTOR_SIZE) ? FLASH_SECTOR_SIZE : size;

    for (int i = 0; i < (int)block_size; i++)
    {
      if (bufa[offs + i] != bufb[i])
      {
        verbose("\nat address 0x%x expected 0x%02x, read 0x%02x\n",
            addr + i - FLASH_ADDR, bufa[offs + i], bufb[i]);
        buf_free(bufb);
        error_exit("verification failed");
      }
    }

    addr += FLASH_SECTOR_SIZE;
    offs += FLASH_SECTOR_SIZE;
    size -= block_size;

    if (0 == (sector++ % STATUS_INTERVAL))
      verbose(".");
  }

  buf_free(bufb);
}

//-----------------------------------------------------------------------------
static void target_read(void)
{
  uint32_t addr = FLASH_ADDR + target_options.offset;
  uint32_t offs = 0;
  uint8_t *buf = target_options.file_data;
  uint32_t size = target_options.size;
  int sector = 0;

  spi_xip_mode();

  while (size)
  {
    dap_read_block(addr, &buf[offs], FLASH_SECTOR_SIZE);

    addr += FLASH_SECTOR_SIZE;
    offs += FLASH_SECTOR_SIZE;
    size -= FLASH_SECTOR_SIZE;

    if (0 == (sector++ % STATUS_INTERVAL))
      verbose(".");
  }

  save_file(target_options.name, buf, target_options.size);
}

//-----------------------------------------------------------------------------
static int target_fuse_read(int section, uint8_t *data)
{
  error_exit("no fuses supported for this target");
  (void)section;
  (void)data;
  return 0;
}

//-----------------------------------------------------------------------------
static void target_fuse_write(int section, uint8_t *data)
{
  error_exit("no fuses supported for this target");
  (void)section;
  (void)data;
}

//-----------------------------------------------------------------------------
static char *target_enumerate(int i)
{
  if (i == 0)
    return "rp2040";

  return NULL;
}

//-----------------------------------------------------------------------------
static char target_help[] =
  "Fuses:\n"
  "  This target has no fuses.\n";

//-----------------------------------------------------------------------------
target_ops_t target_rpi_rp2040_ops =
{
  .select    = target_select,
  .deselect  = target_deselect,
  .erase     = target_erase,
  .lock      = target_lock,
  .unlock    = target_unlock,
  .program   = target_program,
  .verify    = target_verify,
  .read      = target_read,
  .fread     = target_fuse_read,
  .fwrite    = target_fuse_write,
  .enumerate = target_enumerate,
  .help      = target_help,
};


