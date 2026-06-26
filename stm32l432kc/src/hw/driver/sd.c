/*
 * sd.c
 *
 * SPI mode SD card driver.
 */

#include "sd.h"
#include "cli.h"

#ifdef _USE_HW_SD

#include "spi.h"

#ifdef _USE_HW_SPI

#ifndef SD_SPI_CH
#define SD_SPI_CH                    _DEF_SPI1
#endif

#define SD_DUMMY                     0xFFU
#define SD_TOKEN_START_BLOCK         0xFEU
#define SD_TOKEN_MULTI_WRITE         0xFCU
#define SD_TOKEN_STOP_TRAN           0xFDU

#define SD_CMD0                      0U
#define SD_CMD1                      1U
#define SD_CMD8                      8U
#define SD_CMD9                      9U
#define SD_CMD12                     12U
#define SD_CMD16                     16U
#define SD_CMD17                     17U
#define SD_CMD18                     18U
#define SD_CMD24                     24U
#define SD_CMD25                     25U
#define SD_CMD55                     55U
#define SD_CMD58                     58U
#define SD_ACMD23                    (0x80U + 23U)
#define SD_ACMD41                    (0x80U + 41U)

#define SD_INIT_TIMEOUT_MS           1000U
#define SD_READY_TIMEOUT_MS          500U


typedef struct
{
  bool      is_open;
  uint8_t   card_type;
  uint8_t   spi_ch;
  sd_info_t info;
} sd_t;


static sd_t sd;

/*
 * This layer turns raw SPI byte transfers into SD-card block access.
 * CS GPIO is intentionally not controlled here; only SD_SPI_CH is used.
 */
static void sdClear(void);
static bool sdOpen(void);
static uint8_t sdXchg(uint8_t tx_data);
static void sdSendDummyClock(uint32_t count);
static bool sdWaitReady(uint32_t timeout_ms);
static uint8_t sdSendCmd(uint8_t cmd, uint32_t arg);
static bool sdReadDataBlock(uint8_t *p_data, uint32_t length, uint32_t timeout_ms);
static bool sdWriteDataBlock(const uint8_t *p_data, uint8_t token, uint32_t timeout_ms);
static bool sdReadCsd(uint8_t *p_csd);
static bool sdUpdateInfo(void);

#ifdef _USE_HW_CLI
static bool cli_registered = false;
static void cliSd(cli_args_t *args);
static void sdCliInit(void);
static void sdCliPrintInfo(void);
#endif

bool sdInit(void)
{
  bool ret;

#ifdef _USE_HW_CLI
  sdCliInit();
#endif

  if (sd.is_open == true)
  {
    return true;
  }

  sdClear();

  ret = sdOpen();

  if (ret != true)
  {
    sdClear();
  }

  return ret;
}

static void sdClear(void)
{
  memset(&sd, 0, sizeof(sd));
  sd.spi_ch = SD_SPI_CH;
}

static bool sdOpen(void)
{
  bool ret = false;
  uint8_t cmd;
  uint8_t ocr[4];
  uint32_t pre_time;

  /*
   * SPI-mode init flow:
   * dummy clock -> CMD0 -> CMD8/ACMD41/CMD58, or CMD1 for old cards -> CSD info.
   */
  if (spiBegin(sd.spi_ch) != true)
  {
    return false;
  }

  spiSetBitWidth(sd.spi_ch, 8);
  spiSetDataMode(sd.spi_ch, SPI_MODE0);

  sdSendDummyClock(10);

  if (sdSendCmd(SD_CMD0, 0) == 1U)
  {
    pre_time = millis();

    if (sdSendCmd(SD_CMD8, 0x1AAU) == 1U)
    {
      for (uint8_t i = 0; i < 4U; i++)
      {
        ocr[i] = sdXchg(SD_DUMMY);
      }

      if ((ocr[2] == 0x01U) && (ocr[3] == 0xAAU))
      {
        while ((millis() - pre_time) < SD_INIT_TIMEOUT_MS)
        {
          if (sdSendCmd(SD_ACMD41, 0x40000000U) == 0U)
          {
            break;
          }
        }

        if (((millis() - pre_time) < SD_INIT_TIMEOUT_MS) &&
            (sdSendCmd(SD_CMD58, 0) == 0U))
        {
          for (uint8_t i = 0; i < 4U; i++)
          {
            ocr[i] = sdXchg(SD_DUMMY);
          }

          sd.card_type = SD_CARD_TYPE_SD2;
          if ((ocr[0] & 0x40U) != 0U)
          {
            sd.card_type |= SD_CARD_TYPE_BLOCK_ADDRESSING;
          }
        }
      }
    }
    else
    {
      if (sdSendCmd(SD_ACMD41, 0) <= 1U)
      {
        sd.card_type = SD_CARD_TYPE_SD1;
        cmd = SD_ACMD41;
      }
      else
      {
        sd.card_type = SD_CARD_TYPE_MMC;
        cmd = SD_CMD1;
      }

      while ((millis() - pre_time) < SD_INIT_TIMEOUT_MS)
      {
        if (sdSendCmd(cmd, 0) == 0U)
        {
          break;
        }
      }

      if (((millis() - pre_time) >= SD_INIT_TIMEOUT_MS) ||
          (sdSendCmd(SD_CMD16, SD_BLOCK_SIZE) != 0U))
      {
        sd.card_type = 0;
      }
    }
  }

  if (sd.card_type != 0U)
  {
    sd.is_open = true;
    ret = sdUpdateInfo();

    if (ret != true)
    {
      sd.is_open = false;
      sd.card_type = 0;
    }
  }

  return ret;
}

bool sdDeInit(void)
{
  sdClear();

  return true;
}

bool sdIsInit(void)
{
  return sd.is_open;
}

bool sdIsDetected(void)
{
  return true;
}

bool sdGetInfo(sd_info_t *p_info)
{
  if ((sd.is_open != true) || (p_info == NULL))
  {
    return false;
  }

  *p_info = sd.info;

  return true;
}

bool sdIsBusy(void)
{
  if (sd.is_open != true)
  {
    return false;
  }

  return sdXchg(SD_DUMMY) != SD_DUMMY;
}

bool sdIsReady(uint32_t timeout_ms)
{
  if (sd.is_open != true)
  {
    return false;
  }

  return sdWaitReady(timeout_ms);
}

bool sdReadBlocks(uint32_t block_addr, uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms)
{
  bool ret = false;

  if ((sd.is_open != true) || (p_data == NULL) || (num_of_blocks == 0U))
  {
    return false;
  }

  if ((sd.card_type & SD_CARD_TYPE_BLOCK_ADDRESSING) == 0U)
  {
    /* Old SDSC/MMC cards use byte addressing instead of block addressing. */
    block_addr *= SD_BLOCK_SIZE;
  }

  if (num_of_blocks == 1U)
  {
    if (sdSendCmd(SD_CMD17, block_addr) == 0U)
    {
      ret = sdReadDataBlock(p_data, SD_BLOCK_SIZE, timeout_ms);
    }
  }
  else if (sdSendCmd(SD_CMD18, block_addr) == 0U)
  {
    ret = true;

    while (num_of_blocks > 0U)
    {
      if (sdReadDataBlock(p_data, SD_BLOCK_SIZE, timeout_ms) != true)
      {
        ret = false;
        break;
      }

      p_data += SD_BLOCK_SIZE;
      num_of_blocks--;
    }

    (void)sdSendCmd(SD_CMD12, 0);
  }

  return ret;
}

bool sdWriteBlocks(uint32_t block_addr, const uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms)
{
  bool ret = false;

  if ((sd.is_open != true) || (p_data == NULL) || (num_of_blocks == 0U))
  {
    return false;
  }

  if ((sd.card_type & SD_CARD_TYPE_BLOCK_ADDRESSING) == 0U)
  {
    /* Old SDSC/MMC cards use byte addressing instead of block addressing. */
    block_addr *= SD_BLOCK_SIZE;
  }

  if (num_of_blocks == 1U)
  {
    if (sdSendCmd(SD_CMD24, block_addr) == 0U)
    {
      ret = sdWriteDataBlock(p_data, SD_TOKEN_START_BLOCK, timeout_ms);
    }
  }
  else
  {
    if ((sd.card_type & SD_CARD_TYPE_SDC) != 0U)
    {
      (void)sdSendCmd(SD_ACMD23, num_of_blocks);
    }

    if (sdSendCmd(SD_CMD25, block_addr) == 0U)
    {
      ret = true;

      while (num_of_blocks > 0U)
      {
        if (sdWriteDataBlock(p_data, SD_TOKEN_MULTI_WRITE, timeout_ms) != true)
        {
          ret = false;
          break;
        }

        p_data += SD_BLOCK_SIZE;
        num_of_blocks--;
      }

      if (sdWriteDataBlock(NULL, SD_TOKEN_STOP_TRAN, timeout_ms) != true)
      {
        ret = false;
      }
    }
  }

  return ret;
}

bool sdEraseBlocks(uint32_t start_addr, uint32_t end_addr)
{
  (void)start_addr;
  (void)end_addr;

  return false;
}

static uint8_t sdXchg(uint8_t tx_data)
{
  return spiTransfer8(sd.spi_ch, tx_data);
}

static void sdSendDummyClock(uint32_t count)
{
  while (count > 0U)
  {
    (void)sdXchg(SD_DUMMY);
    count--;
  }
}

static bool sdWaitReady(uint32_t timeout_ms)
{
  uint32_t pre_time;

  pre_time = millis();

  do
  {
    if (sdXchg(SD_DUMMY) == SD_DUMMY)
    {
      return true;
    }
  } while ((millis() - pre_time) < timeout_ms);

  return false;
}

static uint8_t sdSendCmd(uint8_t cmd, uint32_t arg)
{
  uint8_t res;
  uint8_t crc = 0x01U;

  if ((cmd & 0x80U) != 0U)
  {
    cmd &= 0x7FU;
    res = sdSendCmd(SD_CMD55, 0);

    if (res > 1U)
    {
      return res;
    }
  }

  if (sdWaitReady(SD_READY_TIMEOUT_MS) != true)
  {
    return 0xFFU;
  }

  if (cmd == SD_CMD12)
  {
    (void)sdXchg(SD_DUMMY);
  }

  (void)sdXchg((uint8_t)(0x40U | cmd));
  (void)sdXchg((uint8_t)(arg >> 24));
  (void)sdXchg((uint8_t)(arg >> 16));
  (void)sdXchg((uint8_t)(arg >> 8));
  (void)sdXchg((uint8_t)arg);

  if (cmd == SD_CMD0)
  {
    crc = 0x95U;
  }
  else if (cmd == SD_CMD8)
  {
    crc = 0x87U;
  }

  (void)sdXchg(crc);

  for (uint8_t i = 0; i < 10U; i++)
  {
    res = sdXchg(SD_DUMMY);
    if ((res & 0x80U) == 0U)
    {
      return res;
    }
  }

  return 0xFFU;
}

static bool sdReadDataBlock(uint8_t *p_data, uint32_t length, uint32_t timeout_ms)
{
  uint8_t token;
  uint32_t pre_time;

  pre_time = millis();

  do
  {
    token = sdXchg(SD_DUMMY);
    if (token != SD_DUMMY)
    {
      break;
    }
  } while ((millis() - pre_time) < timeout_ms);

  if (token != SD_TOKEN_START_BLOCK)
  {
    return false;
  }

  while (length > 0U)
  {
    *p_data = sdXchg(SD_DUMMY);
    p_data++;
    length--;
  }

  (void)sdXchg(SD_DUMMY);
  (void)sdXchg(SD_DUMMY);

  return true;
}

static bool sdWriteDataBlock(const uint8_t *p_data, uint8_t token, uint32_t timeout_ms)
{
  uint8_t resp;

  if (sdWaitReady(timeout_ms) != true)
  {
    return false;
  }

  (void)sdXchg(token);

  if (token == SD_TOKEN_STOP_TRAN)
  {
    return true;
  }

  for (uint32_t i = 0; i < SD_BLOCK_SIZE; i++)
  {
    (void)sdXchg(p_data[i]);
  }

  (void)sdXchg(SD_DUMMY);
  (void)sdXchg(SD_DUMMY);

  resp = sdXchg(SD_DUMMY);

  return (resp & 0x1FU) == 0x05U;
}

static bool sdReadCsd(uint8_t *p_csd)
{
  if (sdSendCmd(SD_CMD9, 0) != 0U)
  {
    return false;
  }

  return sdReadDataBlock(p_csd, 16U, SD_READY_TIMEOUT_MS);
}

static bool sdUpdateInfo(void)
{
  uint8_t csd[16];
  uint32_t csize;
  uint32_t sector_count;

  if (sdReadCsd(csd) != true)
  {
    return false;
  }

  if ((csd[0] >> 6) == 1U)
  {
    /* SDHC/SDXC CSD v2.0 capacity calculation. */
    csize = (uint32_t)csd[9] |
            ((uint32_t)csd[8] << 8) |
            ((uint32_t)(csd[7] & 0x3FU) << 16);
    sector_count = (csize + 1U) << 10;
  }
  else
  {
    uint32_t n;

    /* SDSC/MMC CSD v1.x capacity calculation. */
    n = (uint32_t)((csd[5] & 0x0FU) +
                   ((csd[10] & 0x80U) >> 7) +
                   ((csd[9] & 0x03U) << 1) + 2U);
    csize = (uint32_t)(csd[8] >> 6) |
            ((uint32_t)csd[7] << 2) |
            ((uint32_t)(csd[6] & 0x03U) << 10);
    sector_count = (csize + 1U) << (n - 9U);
  }

  memset(&sd.info, 0, sizeof(sd.info));
  sd.info.card_type = sd.card_type;
  sd.info.card_version = (sd.card_type & SD_CARD_TYPE_SD2) ? 2U : 1U;
  sd.info.block_numbers = sector_count;
  sd.info.block_size = SD_BLOCK_SIZE;
  sd.info.log_block_numbers = sector_count;
  sd.info.log_block_size = SD_BLOCK_SIZE;
  sd.info.card_size = sector_count / 2048U;

  return sector_count > 0U;
}

#ifdef _USE_HW_CLI

static void sdCliInit(void)
{
  if (cli_registered != true)
  {
    cli_registered = cliAdd("sd", cliSd);
  }
}

static void sdCliPrintInfo(void)
{
  sd_info_t info;

  cliPrintf("sd init     : %d\n", sdIsInit());
  cliPrintf("sd detected : %d\n", sdIsDetected());

  if (sdIsInit() == true)
  {
    cliPrintf("sd ready    : %d\n", sdIsReady(10));
    cliPrintf("sd busy     : %d\n", sdIsBusy());

    if (sdGetInfo(&info) == true)
    {
      cliPrintf("card type   : 0x%02X\n", (unsigned int)info.card_type);
      cliPrintf("card ver    : %lu\n", (unsigned long)info.card_version);
      cliPrintf("sector cnt  : %lu\n", (unsigned long)info.log_block_numbers);
      cliPrintf("sector size : %lu\n", (unsigned long)info.log_block_size);
      cliPrintf("card size   : %lu MB\n", (unsigned long)info.card_size);
    }
  }
}

static void cliSd(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "init") == true)
  {
    cliPrintf("sd init : %d\n", sdInit());
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    sdCliPrintInfo();
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "read") == true)
  {
    static uint8_t read_buf[SD_BLOCK_SIZE];
    uint32_t sector;

    sector = (uint32_t)args->getData(1);

    if (sdReadBlocks(sector, read_buf, 1, 1000) == true)
    {
      cliPrintf("sd read sector %lu ok\n", (unsigned long)sector);

      for (uint32_t i = 0; i < 64U; i++)
      {
        if ((i % 16U) == 0U)
        {
          cliPrintf("\n%04lX : ", (unsigned long)i);
        }
        cliPrintf("%02X ", (unsigned int)read_buf[i]);
      }
      cliPrintf("\n");
    }
    else
    {
      cliPrintf("sd read sector %lu fail\n", (unsigned long)sector);
    }

    ret = true;
  }

  if (ret != true)
  {
    cliPrintf("sd init\n");
    cliPrintf("sd info\n");
    cliPrintf("sd read sector\n");
  }
}

#endif

#endif
#endif
