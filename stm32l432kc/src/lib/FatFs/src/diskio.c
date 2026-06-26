/*-----------------------------------------------------------------------*/
/* FatFs low-level disk I/O bridge for STM32 ff_gen_drv drivers          */
/*-----------------------------------------------------------------------*/

#include "ff.h"
#include "diskio.h"
#include "ff_gen_drv.h"
#include <stdbool.h>


extern Disk_drvTypeDef disk;


static bool diskIsValid(BYTE pdrv)
{
  return (pdrv < FF_VOLUMES) && (disk.drv[pdrv] != 0);
}

DSTATUS disk_status(BYTE pdrv)
{
  if (diskIsValid(pdrv) != true)
  {
    return STA_NOINIT;
  }

  return disk.drv[pdrv]->disk_status(disk.lun[pdrv]);
}

DSTATUS disk_initialize(BYTE pdrv)
{
  DSTATUS status;

  if (diskIsValid(pdrv) != true)
  {
    return STA_NOINIT;
  }

  if (disk.is_initialized[pdrv] == 0U)
  {
    status = disk.drv[pdrv]->disk_initialize(disk.lun[pdrv]);
    if ((status & STA_NOINIT) == 0U)
    {
      disk.is_initialized[pdrv] = 1U;
    }
  }
  else
  {
    status = disk.drv[pdrv]->disk_status(disk.lun[pdrv]);
  }

  return status;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
  if ((diskIsValid(pdrv) != true) || (buff == 0) || (count == 0U))
  {
    return RES_PARERR;
  }

  if (disk.is_initialized[pdrv] == 0U)
  {
    return RES_NOTRDY;
  }

  return disk.drv[pdrv]->disk_read(disk.lun[pdrv], buff, sector, count);
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
  if ((diskIsValid(pdrv) != true) || (buff == 0) || (count == 0U))
  {
    return RES_PARERR;
  }

  if (disk.is_initialized[pdrv] == 0U)
  {
    return RES_NOTRDY;
  }

  return disk.drv[pdrv]->disk_write(disk.lun[pdrv], buff, sector, count);
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
  if (diskIsValid(pdrv) != true)
  {
    return RES_PARERR;
  }

  if (disk.is_initialized[pdrv] == 0U)
  {
    return RES_NOTRDY;
  }

  return disk.drv[pdrv]->disk_ioctl(disk.lun[pdrv], cmd, buff);
}

#if FF_FS_READONLY == 0 && FF_FS_NORTC == 0
DWORD get_fattime(void)
{
  return ((DWORD)(2026U - 1980U) << 25) |
         ((DWORD)6U << 21) |
         ((DWORD)24U << 16);
}
#endif
