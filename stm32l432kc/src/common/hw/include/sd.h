/*
 * sd.h
 *
 * SPI-mode SD block device driver.
 *
 * Layer role:
 * - spi.c only transfers bytes on the SPI bus.
 * - sd.c speaks the SD-card SPI command protocol on top of spi.c.
 * - FatFs uses this driver as a 512-byte block device through sd_diskio.c.
 *
 * This driver does not format filesystems and does not manage files.
 * This driver also does not control a GPIO CS pin.
 */

#ifndef SRC_COMMON_HW_INCLUDE_SD_H_
#define SRC_COMMON_HW_INCLUDE_SD_H_

#include "hw_def.h"

#ifdef _USE_HW_SD

#define SD_BLOCK_SIZE                  512U

#define SD_CARD_TYPE_MMC               0x01U
#define SD_CARD_TYPE_SD1               0x02U
#define SD_CARD_TYPE_SD2               0x04U
#define SD_CARD_TYPE_SDC               (SD_CARD_TYPE_SD1 | SD_CARD_TYPE_SD2)
#define SD_CARD_TYPE_BLOCK_ADDRESSING  0x08U

typedef struct
{
  uint32_t card_type;
  uint32_t card_version;
  uint32_t card_class;
  uint32_t rel_card_Add;
  uint32_t block_numbers;
  uint32_t block_size;
  uint32_t log_block_numbers;
  uint32_t log_block_size;
  uint32_t card_size;
} sd_info_t;

/* Driver/card state. */
bool sdInit(void);
bool sdDeInit(void);
bool sdIsInit(void);
bool sdIsDetected(void);
bool sdGetInfo(sd_info_t *p_info);
bool sdIsBusy(void);
bool sdIsReady(uint32_t timeout_ms);

/* FatFs-facing 512-byte block access. block_addr is a logical sector number. */
bool sdReadBlocks(uint32_t block_addr, uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms);
bool sdWriteBlocks(uint32_t block_addr, const uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms);
bool sdEraseBlocks(uint32_t start_addr, uint32_t end_addr);

static inline bool sdReadBlock(uint32_t block_addr, uint8_t *p_data, uint32_t timeout_ms)
{
  return sdReadBlocks(block_addr, p_data, 1U, timeout_ms);
}

static inline bool sdWriteBlock(uint32_t block_addr, const uint8_t *p_data, uint32_t timeout_ms)
{
  return sdWriteBlocks(block_addr, p_data, 1U, timeout_ms);
}

static inline uint32_t sdGetBlockSize(void)
{
  return SD_BLOCK_SIZE;
}

#endif

#endif /* SRC_COMMON_HW_INCLUDE_SD_H_ */
