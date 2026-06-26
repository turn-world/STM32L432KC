/**
  ******************************************************************************
  * @file    sd_diskio.c
  * @brief   SD Disk I/O driver for FatFs.
  ******************************************************************************
  */

#include "ff_gen_drv.h"
#include "sd_diskio.h"
#include "sd.h"


#define SD_TIMEOUT              (10U * 1000U)
#define SD_DEFAULT_BLOCK_SIZE   512U


static volatile DSTATUS Stat = STA_NOINIT;

static DSTATUS SD_CheckStatus(BYTE lun);
DSTATUS SD_initialize(BYTE lun);
DSTATUS SD_status(BYTE lun);
DRESULT SD_read(BYTE lun, BYTE *buff, LBA_t sector, UINT count);
#if _USE_WRITE == 1
DRESULT SD_write(BYTE lun, const BYTE *buff, LBA_t sector, UINT count);
#endif
#if _USE_IOCTL == 1
DRESULT SD_ioctl(BYTE lun, BYTE cmd, void *buff);
#endif

const Diskio_drvTypeDef SD_Driver =
{
  SD_initialize,
  SD_status,
  SD_read,
#if _USE_WRITE == 1
  SD_write,
#endif
#if _USE_IOCTL == 1
  SD_ioctl,
#endif
};

static DSTATUS SD_CheckStatus(BYTE lun)
{
  (void)lun;

#ifdef _USE_HW_SD
  Stat = 0;

  if (sdIsInit() != true)
  {
    Stat |= STA_NOINIT;
  }
  if (sdIsDetected() != true)
  {
    Stat |= STA_NODISK;
  }
  if (sdIsReady(10) != true)
  {
    Stat |= STA_NOINIT;
  }
#else
  Stat = STA_NOINIT | STA_NODISK;
#endif

  return Stat;
}

DSTATUS SD_initialize(BYTE lun)
{
  (void)lun;

#ifdef _USE_HW_SD
  if (sdIsInit() != true)
  {
    (void)sdInit();
  }
#endif

  return SD_CheckStatus(lun);
}

DSTATUS SD_status(BYTE lun)
{
  return SD_CheckStatus(lun);
}

DRESULT SD_read(BYTE lun, BYTE *buff, LBA_t sector, UINT count)
{
  (void)lun;

#ifdef _USE_HW_SD
  if (sdReadBlocks((uint32_t)sector, buff, count, SD_TIMEOUT) == true)
  {
    return RES_OK;
  }
#else
  (void)buff;
  (void)sector;
  (void)count;
#endif

  return RES_NOTRDY;
}

#if _USE_WRITE == 1
DRESULT SD_write(BYTE lun, const BYTE *buff, LBA_t sector, UINT count)
{
  (void)lun;

#ifdef _USE_HW_SD
  if (sdWriteBlocks((uint32_t)sector, (const uint8_t *)buff, count, SD_TIMEOUT) == true)
  {
    return RES_OK;
  }
#else
  (void)buff;
  (void)sector;
  (void)count;
#endif

  return RES_NOTRDY;
}
#endif

#if _USE_IOCTL == 1
DRESULT SD_ioctl(BYTE lun, BYTE cmd, void *buff)
{
  (void)lun;

  if (SD_CheckStatus(lun) & STA_NOINIT)
  {
    return RES_NOTRDY;
  }

#ifdef _USE_HW_SD
  sd_info_t sd_info;

  switch (cmd)
  {
    case CTRL_SYNC:
      if (sdIsReady(SD_TIMEOUT) == true)
      {
        return RES_OK;
      }
      break;

    case GET_SECTOR_COUNT:
      if ((buff != 0) && (sdGetInfo(&sd_info) == true))
      {
        *(DWORD *)buff = sd_info.log_block_numbers;
        return RES_OK;
      }
      break;

    case GET_SECTOR_SIZE:
      if ((buff != 0) && (sdGetInfo(&sd_info) == true))
      {
        *(WORD *)buff = sd_info.log_block_size;
        return RES_OK;
      }
      break;

    case GET_BLOCK_SIZE:
      if ((buff != 0) && (sdGetInfo(&sd_info) == true))
      {
        *(DWORD *)buff = sd_info.log_block_size / SD_DEFAULT_BLOCK_SIZE;
        return RES_OK;
      }
      break;

    default:
      break;
  }
#else
  (void)cmd;
  (void)buff;
#endif

  return RES_PARERR;
}
#endif
