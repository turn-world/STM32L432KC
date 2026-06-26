/*
 * fatfs.h
 *
 *  Created on: Jun 24, 2026
 *      Author: young
 */

#ifndef SRC_COMMON_HW_INCLUDE_FATFS_H_
#define SRC_COMMON_HW_INCLUDE_FATFS_H_

#include "hw_def.h"


#ifdef _USE_HW_FATFS

#include "ff.h"

bool fatfsInit(void);
bool fatfsIsInit(void);
FRESULT fatfsGetLastResult(void);

extern FATFS SDFatFs;
extern char SDPath[4];


#endif

#endif /* SRC_COMMON_HW_INCLUDE_FATFS_H_ */
